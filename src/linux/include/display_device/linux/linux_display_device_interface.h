/**
 * @file src/linux/include/display_device/linux/linux_display_device_interface.h
 * @brief Mirror of WinDisplayDeviceInterface for Linux.
 */
#pragma once

// system includes
#include <set>

// local includes
#include "display_device/linux/types.h"

namespace display_device {

  /**
   * @brief Higher level abstracted API for interacting with Linux display
   *        devices (real DRM outputs and our vkms virtual displays).
   *
   * Method semantics intentionally mirror WinDisplayDeviceInterface so that
   * the cross-platform SettingsManager wrapper can be (almost) unchanged.
   */
  class LinuxDisplayDeviceInterface {
  public:
    virtual ~LinuxDisplayDeviceInterface() = default;

    /// Whether the API (DRM/Wayland/configfs/helper) is reachable right now.
    [[nodiscard]] virtual bool isApiAccessAvailable() const = 0;

    /// Enumerate all known displays (real DRM connectors + our vkms ones).
    [[nodiscard]] virtual EnumeratedDeviceList enumAvailableDevices() const = 0;

    /// Friendly display name for a device id.
    [[nodiscard]] virtual std::string getDisplayName(const std::string &device_id) const = 0;

    /// Snapshot of which displays are currently active and grouped.
    [[nodiscard]] virtual ActiveTopology getCurrentTopology() const = 0;

    /// Sanity-check a topology before applying.
    [[nodiscard]] virtual bool isTopologyValid(const ActiveTopology &topology) const = 0;

    /// Loose equality for topologies (compositor may normalize them).
    [[nodiscard]] virtual bool isTopologyTheSame(const ActiveTopology &lhs, const ActiveTopology &rhs) const = 0;

    /// Apply a new topology (enable/disable/group displays).
    [[nodiscard]] virtual bool setTopology(const ActiveTopology &new_topology) = 0;

    /// Current modes for the requested device ids.
    [[nodiscard]] virtual DeviceDisplayModeMap getCurrentDisplayModes(const std::set<std::string> &device_ids) const = 0;

    /// Set modes (resolution + refresh) on the requested devices.
    [[nodiscard]] virtual bool setDisplayModes(const DeviceDisplayModeMap &modes) = 0;

    /// Whether the device is currently primary.
    [[nodiscard]] virtual bool isPrimary(const std::string &device_id) const = 0;

    /// Make a device the primary output.
    [[nodiscard]] virtual bool setAsPrimary(const std::string &device_id) = 0;

    /// HDR states for the requested devices. May be empty/unknown on Linux.
    [[nodiscard]] virtual HdrStateMap getCurrentHdrStates(const std::set<std::string> &device_ids) const = 0;

    /// Set HDR states. May be a no-op on Linux compositors that don't expose HDR control.
    [[nodiscard]] virtual bool setHdrStates(const HdrStateMap &states) = 0;

    // ---- Linux-specific extensions for vkms ----

    /// Create a new virtual display backed by vkms.
    /// Returns the new device id on success, or empty string on failure.
    [[nodiscard]] virtual std::string createVirtualDisplay(const VkmsVirtualDisplayConfig &cfg) = 0;

    /// Destroy a virtual display previously created by createVirtualDisplay.
    [[nodiscard]] virtual bool destroyVirtualDisplay(const std::string &device_id) = 0;
  };

}  // namespace display_device
