/**
 * @file src/linux/include/display_device/linux/settings_manager.h
 * @brief Linux SettingsManager bridging to the platform interface.
 */
#pragma once

// system includes
#include <memory>

// local includes
#include "display_device/linux/linux_display_device_interface.h"
#include "display_device/settings_manager_interface.h"
#include "display_device/settings_persistence_interface.h"

namespace display_device {

  class LinuxSettingsManager: public SettingsManagerInterface {
  public:
    explicit LinuxSettingsManager(
        std::shared_ptr<LinuxDisplayDeviceInterface> display_device,
        std::shared_ptr<SettingsPersistenceInterface> persistence);

    // SettingsManagerInterface
    [[nodiscard]] EnumeratedDeviceList enumAvailableDevices() const override;
    [[nodiscard]] std::string getDisplayName(const std::string &device_id) const override;
    [[nodiscard]] ApplyResult applySettings(const SingleDisplayConfiguration &config) override;
    [[nodiscard]] RevertResult revertSettings() override;
    [[nodiscard]] bool resetPersistence() override;

  private:
    std::shared_ptr<LinuxDisplayDeviceInterface> m_display_device;
    std::shared_ptr<SettingsPersistenceInterface> m_persistence;
  };

}  // namespace display_device
