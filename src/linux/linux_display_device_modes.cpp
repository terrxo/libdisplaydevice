/**
 * @file src/linux/linux_display_device_modes.cpp
 * @brief Display mode get/set via cosmic-randr.
 */
#include "display_device/linux/cosmic_randr_layer.h"
#include "display_device/linux/linux_display_device.h"
#include "display_device/logging.h"

#include <algorithm>

namespace display_device {

  DeviceDisplayModeMap LinuxDisplayDevice::getCurrentDisplayModes(const std::set<std::string> &device_ids) const {
    DeviceDisplayModeMap result;
    auto outputs = cosmic::listOutputs();
    for (const auto &o : outputs) {
      if (device_ids.count(o.m_name) == 0) {
        continue;
      }
      DisplayMode m;
      m.m_resolution = o.m_current_resolution;
      m.m_refresh_rate = o.m_current_refresh.m_numerator
                            ? o.m_current_refresh
                            : Rational {60, 1};
      result[o.m_name] = m;
    }
    return result;
  }

  bool LinuxDisplayDevice::setDisplayModes(const DeviceDisplayModeMap &modes) {
    if (!cosmic::isAvailable()) {
      DD_LOG(error) << "setDisplayModes: cosmic-randr unavailable";
      return false;
    }
    bool ok = true;
    for (const auto &[id, mode] : modes) {
      const auto denom = std::max(1u, mode.m_refresh_rate.m_denominator);
      const unsigned int hz = mode.m_refresh_rate.m_numerator / denom;
      if (!cosmic::setMode(id, mode.m_resolution.m_width, mode.m_resolution.m_height, hz ? hz : 60)) {
        DD_LOG(warning) << "setDisplayModes: failed to set " << id
                        << " to " << mode.m_resolution.m_width
                        << "x" << mode.m_resolution.m_height << "@" << hz;
        ok = false;
      }
    }
    return ok;
  }

}  // namespace display_device
