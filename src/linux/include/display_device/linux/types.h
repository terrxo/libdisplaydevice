/**
 * @file src/linux/include/display_device/linux/types.h
 * @brief Linux-specific display device types.
 */
#pragma once

// system includes
#include <map>
#include <set>
#include <string>
#include <vector>

// local includes
#include "display_device/types.h"

namespace display_device {

  /**
   * @brief Identifier for a Linux display device.
   *
   * On Linux we identify a device by its DRM connector path
   * (e.g. "/sys/class/drm/card0-Virtual-1") plus the connector name
   * extracted from it (e.g. "Virtual-1"). The configfs path for vkms
   * devices we create is also tracked so we can clean up on revert.
   */
  struct LinuxDeviceId {
    std::string m_drm_path;        ///< /sys/class/drm/card<N>-<connector>
    std::string m_connector_name;  ///< e.g. "HDMI-A-1", "Virtual-1"
    std::string m_configfs_path;   ///< /sys/kernel/config/vkms/<name>, empty for real displays
    bool m_is_virtual {false};     ///< true if created by us via vkms

    friend bool operator==(const LinuxDeviceId &lhs, const LinuxDeviceId &rhs);
  };

  /**
   * @brief Single entry of the display topology.
   *
   * Mirrors the Windows definition: a list of device IDs that form a single
   * "logical display" group (typically one entry per active output, but a
   * group with multiple device IDs represents a mirrored set).
   */
  using SingleDisplayTopology = std::vector<std::string>;

  /**
   * @brief Active topology = list of display groups.
   */
  using ActiveTopology = std::vector<SingleDisplayTopology>;

  /**
   * @brief Display mode (resolution + refresh rate).
   */
  struct DisplayMode {
    Resolution m_resolution {};
    Rational m_refresh_rate {};

    friend bool operator==(const DisplayMode &lhs, const DisplayMode &rhs);
  };

  /**
   * @brief Map of device-id to current display mode.
   */
  using DeviceDisplayModeMap = std::map<std::string, DisplayMode>;

  /**
   * @brief Map of device-id to HDR state.
   */
  using HdrStateMap = std::map<std::string, std::optional<HdrState>>;

  /**
   * @brief Configuration request to create a vkms virtual display.
   */
  struct VkmsVirtualDisplayConfig {
    std::string m_name;             ///< friendly name, becomes part of configfs path
    Resolution m_resolution {};
    Rational m_refresh_rate {};
    bool m_primary {false};
    /// Orientation: 0/90/180/270 degrees clockwise. The compositor handles
    /// rotation in its output configuration; vkms itself produces a
    /// rectangular framebuffer, so for portrait we just declare a
    /// resolution like 1440x2560 directly.
    unsigned int m_rotation_deg {0};
  };

}  // namespace display_device
