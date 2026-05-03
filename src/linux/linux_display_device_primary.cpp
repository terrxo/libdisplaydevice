/**
 * @file src/linux/linux_display_device_primary.cpp
 * @brief Primary display + HDR state operations on Cosmic.
 *
 * Notes:
 *  - "Primary" on Cosmic is implicit by output position - the leftmost,
 *    topmost output is treated as primary. We approximate "set as primary"
 *    by moving the chosen output to position (0,0). This is imperfect but
 *    matches Cosmic's actual semantics.
 *  - HDR is intentionally not implemented in v1. Cosmic doesn't expose a
 *    public HDR control protocol yet. Returning unknown/disabled is safe.
 */
#include "display_device/linux/cosmic_randr_layer.h"
#include "display_device/linux/linux_display_device.h"
#include "display_device/logging.h"

namespace display_device {

  bool LinuxDisplayDevice::isPrimary(const std::string &device_id) const {
    auto outputs = cosmic::listOutputs();
    for (const auto &o : outputs) {
      if (o.m_name == device_id) {
        return o.m_primary || (o.m_position.m_x == 0 && o.m_position.m_y == 0);
      }
    }
    return false;
  }

  bool LinuxDisplayDevice::setAsPrimary(const std::string &device_id) {
    if (!cosmic::isAvailable()) {
      return false;
    }
    if (!cosmic::setPosition(device_id, 0, 0)) {
      DD_LOG(warning) << "setAsPrimary: failed to position " << device_id << " at origin";
      return false;
    }
    return true;
  }

  HdrStateMap LinuxDisplayDevice::getCurrentHdrStates(const std::set<std::string> &device_ids) const {
    HdrStateMap result;
    for (const auto &id : device_ids) {
      result[id] = std::nullopt;
    }
    return result;
  }

  bool LinuxDisplayDevice::setHdrStates(const HdrStateMap & /*states*/) {
    return true;  // no-op success
  }

}  // namespace display_device
