/**
 * @file src/linux/types.cpp
 * @brief Comparators for Linux-specific types.
 */
#include "display_device/linux/types.h"

namespace display_device {

  bool operator==(const LinuxDeviceId &lhs, const LinuxDeviceId &rhs) {
    return lhs.m_drm_path == rhs.m_drm_path
           && lhs.m_connector_name == rhs.m_connector_name
           && lhs.m_configfs_path == rhs.m_configfs_path
           && lhs.m_is_virtual == rhs.m_is_virtual;
  }

  bool operator==(const DisplayMode &lhs, const DisplayMode &rhs) {
    return lhs.m_resolution == rhs.m_resolution && lhs.m_refresh_rate == rhs.m_refresh_rate;
  }

}  // namespace display_device
