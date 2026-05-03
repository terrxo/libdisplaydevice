/**
 * @file src/linux/settings_manager_apply.cpp
 * @brief Apply path: take a SingleDisplayConfiguration and make it real.
 *
 * Strategy:
 *   1. If the requested device_id isn't a real existing device, we treat
 *      it as a request for a new virtual display and create one via evdi.
 *      The connector name returned by evdi becomes the actual device_id.
 *   2. Apply the requested mode (cosmic-randr `mode`).
 *   3. If DevicePreparation::EnsurePrimary, move it to (0,0).
 *   4. If DevicePreparation::EnsureOnlyDisplay, disable all other outputs.
 *   5. Persist a small revert-state blob via SettingsPersistenceInterface.
 *
 * The revert-state JSON describes:
 *   - The set of outputs that were disabled (so we can re-enable them).
 *   - The virtual display we created (so we can destroy it).
 *   - The previous mode of any output we changed (best-effort).
 */
#include "display_device/linux/cosmic_randr_layer.h"
#include "display_device/linux/evdi_layer.h"
#include "display_device/linux/linux_display_device.h"
#include "display_device/linux/settings_manager.h"
#include "display_device/logging.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace display_device {

  namespace {
    using ApplyResult = SettingsManagerInterface::ApplyResult;
    using json = nlohmann::json;

    /// Serialise a small revert-state. We deliberately keep this simple
    /// and human-inspectable; full Windows-style topology archive is
    /// overkill for our current scope.
    std::vector<std::uint8_t> serialiseRevertState(const json &j) {
      const auto s = j.dump();
      return std::vector<std::uint8_t>(s.begin(), s.end());
    }
  }  // namespace

  ApplyResult LinuxSettingsManager::applySettings(const SingleDisplayConfiguration &config) {
    if (!m_display_device || !m_display_device->isApiAccessAvailable()) {
      DD_LOG(warning) << "applySettings: API not available";
      return ApplyResult::ApiTemporarilyUnavailable;
    }

    json revert;
    revert["created_virtual"] = nullptr;
    revert["disabled_others"] = json::array();
    revert["original_modes"] = json::object();
    revert["original_position"] = json::object();

    // ---- Step 1: resolve target device id ----
    std::string target_id = config.m_device_id;
    auto known = m_display_device->enumAvailableDevices();
    auto known_has = [&](const std::string &id) {
      return std::any_of(known.begin(), known.end(),
                         [&](const auto &d) { return d.m_device_id == id; });
    };

    if (target_id.empty() || !known_has(target_id)) {
      // No matching real device. Treat as a request to create a virtual one.
      VkmsVirtualDisplayConfig vcfg;
      vcfg.m_name = target_id.empty() ? "apollo-vd" : target_id;
      vcfg.m_resolution = config.m_resolution.value_or(Resolution {1920, 1080});
      // Convert FloatingPoint refresh_rate to a Rational.
      if (config.m_refresh_rate.has_value()) {
        if (auto *r = std::get_if<Rational>(&*config.m_refresh_rate)) {
          vcfg.m_refresh_rate = *r;
        } else if (auto *d = std::get_if<double>(&*config.m_refresh_rate)) {
          vcfg.m_refresh_rate = Rational {static_cast<unsigned int>(*d * 1000), 1000};
        }
      } else {
        vcfg.m_refresh_rate = Rational {60, 1};
      }
      target_id = m_display_device->createVirtualDisplay(vcfg);
      if (target_id.empty()) {
        DD_LOG(error) << "applySettings: failed to create virtual display";
        return ApplyResult::DevicePrepFailed;
      }
      revert["created_virtual"] = target_id;
      DD_LOG(info) << "applySettings: created virtual display " << target_id;
    }

    // ---- Step 2: apply mode ----
    if (config.m_resolution.has_value()) {
      DeviceDisplayModeMap modes;
      DisplayMode m;
      m.m_resolution = *config.m_resolution;
      if (config.m_refresh_rate.has_value()) {
        if (auto *r = std::get_if<Rational>(&*config.m_refresh_rate)) {
          m.m_refresh_rate = *r;
        } else if (auto *d = std::get_if<double>(&*config.m_refresh_rate)) {
          m.m_refresh_rate = Rational {static_cast<unsigned int>(*d * 1000), 1000};
        }
      } else {
        m.m_refresh_rate = Rational {60, 1};
      }
      // Capture original mode for revert.
      auto orig = m_display_device->getCurrentDisplayModes({target_id});
      auto it = orig.find(target_id);
      if (it != orig.end()) {
        revert["original_modes"][target_id] = {
            {"width", it->second.m_resolution.m_width},
            {"height", it->second.m_resolution.m_height},
            {"refresh_num", it->second.m_refresh_rate.m_numerator},
            {"refresh_den", it->second.m_refresh_rate.m_denominator},
        };
      }
      modes[target_id] = m;
      if (!m_display_device->setDisplayModes(modes)) {
        DD_LOG(warning) << "applySettings: setDisplayModes failed (continuing)";
        // Not fatal - mode may already match.
      }
    }

    // ---- Step 3: device preparation ----
    using Prep = SingleDisplayConfiguration::DevicePreparation;
    switch (config.m_device_prep) {
      case Prep::VerifyOnly:
        break;
      case Prep::EnsureActive:
        if (!m_display_device->setTopology(ActiveTopology {{target_id}})) {
          // setTopology is permissive; proceed
        }
        break;
      case Prep::EnsurePrimary:
        if (!m_display_device->setAsPrimary(target_id)) {
          DD_LOG(warning) << "applySettings: setAsPrimary failed";
          return ApplyResult::PrimaryDevicePrepFailed;
        }
        break;
      case Prep::EnsureOnlyDisplay: {
        // Disable every other output, keep this one.
        auto outputs = cosmic::listOutputs();
        for (const auto &o : outputs) {
          if (o.m_name != target_id && o.m_enabled) {
            if (cosmic::disableOutput(o.m_name)) {
              revert["disabled_others"].push_back(o.m_name);
            }
          }
        }
        if (!m_display_device->setAsPrimary(target_id)) {
          DD_LOG(warning) << "EnsureOnlyDisplay: setAsPrimary failed";
        }
        break;
      }
    }

    // ---- Step 4: persist revert state ----
    if (m_persistence) {
      if (!m_persistence->store(serialiseRevertState(revert))) {
        DD_LOG(warning) << "applySettings: failed to persist revert state";
        return ApplyResult::PersistenceSaveFailed;
      }
    }

    DD_LOG(info) << "applySettings: target=" << target_id
                 << " prep=" << static_cast<int>(config.m_device_prep) << " OK";
    return ApplyResult::Ok;
  }

}  // namespace display_device
