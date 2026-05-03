/**
 * @file src/linux/settings_manager_revert.cpp
 * @brief Revert path: undo what applySettings did.
 *
 * Reads the persisted revert-state blob and:
 *   1. Re-enables outputs we disabled.
 *   2. Restores original modes for outputs we changed.
 *   3. Destroys any virtual display we created.
 *   4. Clears persistence.
 */
#include "display_device/linux/cosmic_randr_layer.h"
#include "display_device/linux/linux_display_device.h"
#include "display_device/linux/settings_manager.h"
#include "display_device/logging.h"

#include <nlohmann/json.hpp>

namespace display_device {

  namespace {
    using RevertResult = SettingsManagerInterface::RevertResult;
    using json = nlohmann::json;
  }

  RevertResult LinuxSettingsManager::revertSettings() {
    if (!m_display_device) {
      return RevertResult::ApiTemporarilyUnavailable;
    }

    if (!m_persistence) {
      // Nothing to do.
      return RevertResult::Ok;
    }

    auto data_opt = m_persistence->load();
    if (!data_opt.has_value()) {
      DD_LOG(warning) << "revertSettings: failed to load persistence";
      return RevertResult::ApiTemporarilyUnavailable;
    }
    if (data_opt->empty()) {
      // No state to revert.
      return RevertResult::Ok;
    }

    json revert;
    try {
      revert = json::parse(std::string(data_opt->begin(), data_opt->end()));
    }
    catch (const std::exception &e) {
      DD_LOG(error) << "revertSettings: parse error: " << e.what();
      // Clear corrupt state so we don't loop on it.
      (void) m_persistence->clear();
      return RevertResult::Ok;
    }

    bool failures = false;

    // 1. Re-enable previously-disabled outputs.
    if (revert.contains("disabled_others") && revert["disabled_others"].is_array()) {
      for (const auto &name : revert["disabled_others"]) {
        if (!cosmic::enableOutput(name.get<std::string>())) {
          DD_LOG(warning) << "revertSettings: failed to re-enable " << name;
          failures = true;
        }
      }
    }

    // 2. Restore original modes.
    if (revert.contains("original_modes") && revert["original_modes"].is_object()) {
      DeviceDisplayModeMap modes;
      for (auto it = revert["original_modes"].begin(); it != revert["original_modes"].end(); ++it) {
        const auto &v = it.value();
        DisplayMode m;
        m.m_resolution = {v.value("width", 0u), v.value("height", 0u)};
        m.m_refresh_rate = {v.value("refresh_num", 60u), v.value("refresh_den", 1u)};
        modes[it.key()] = m;
      }
      if (!modes.empty()) {
        (void) m_display_device->setDisplayModes(modes);
      }
    }

    // 3. Destroy any virtual display we created.
    if (revert.contains("created_virtual") && revert["created_virtual"].is_string()) {
      const auto id = revert["created_virtual"].get<std::string>();
      if (!m_display_device->destroyVirtualDisplay(id)) {
        DD_LOG(warning) << "revertSettings: destroyVirtualDisplay failed for " << id;
        // Not fatal - device may already be gone.
      }
    }

    // 4. Clear persistence.
    if (!m_persistence->clear()) {
      DD_LOG(warning) << "revertSettings: failed to clear persistence";
      return RevertResult::PersistenceSaveFailed;
    }

    return failures ? RevertResult::TopologyIsInvalid : RevertResult::Ok;
  }

}  // namespace display_device
