/**
 * @file src/linux/include/display_device/linux/linux_display_device.h
 * @brief Concrete LinuxDisplayDevice that calls into vkms_layer + wlr_output_layer.
 */
#pragma once

#include "display_device/linux/linux_display_device_interface.h"

namespace display_device {

  class LinuxDisplayDevice: public LinuxDisplayDeviceInterface {
  public:
    LinuxDisplayDevice();
    ~LinuxDisplayDevice() override;

    // LinuxDisplayDeviceInterface impl
    [[nodiscard]] bool isApiAccessAvailable() const override;
    [[nodiscard]] EnumeratedDeviceList enumAvailableDevices() const override;
    [[nodiscard]] std::string getDisplayName(const std::string &device_id) const override;
    [[nodiscard]] ActiveTopology getCurrentTopology() const override;
    [[nodiscard]] bool isTopologyValid(const ActiveTopology &topology) const override;
    [[nodiscard]] bool isTopologyTheSame(const ActiveTopology &lhs, const ActiveTopology &rhs) const override;
    [[nodiscard]] bool setTopology(const ActiveTopology &new_topology) override;
    [[nodiscard]] DeviceDisplayModeMap getCurrentDisplayModes(const std::set<std::string> &device_ids) const override;
    [[nodiscard]] bool setDisplayModes(const DeviceDisplayModeMap &modes) override;
    [[nodiscard]] bool isPrimary(const std::string &device_id) const override;
    [[nodiscard]] bool setAsPrimary(const std::string &device_id) override;
    [[nodiscard]] HdrStateMap getCurrentHdrStates(const std::set<std::string> &device_ids) const override;
    [[nodiscard]] bool setHdrStates(const HdrStateMap &states) override;
    [[nodiscard]] std::string createVirtualDisplay(const VkmsVirtualDisplayConfig &cfg) override;
    [[nodiscard]] bool destroyVirtualDisplay(const std::string &device_id) override;
  };

}  // namespace display_device
