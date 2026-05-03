/**
 * @file src/linux/settings_manager_general.cpp
 * @brief LinuxSettingsManager construction + simple delegated queries.
 *        Apply/Revert live in their own files for clarity.
 */
#include "display_device/linux/settings_manager.h"

#include <utility>

namespace display_device {

  LinuxSettingsManager::LinuxSettingsManager(
      std::shared_ptr<LinuxDisplayDeviceInterface> display_device,
      std::shared_ptr<SettingsPersistenceInterface> persistence):
      m_display_device(std::move(display_device)),
      m_persistence(std::move(persistence)) {
  }

  EnumeratedDeviceList LinuxSettingsManager::enumAvailableDevices() const {
    if (!m_display_device) {
      return {};
    }
    return m_display_device->enumAvailableDevices();
  }

  std::string LinuxSettingsManager::getDisplayName(const std::string &device_id) const {
    if (!m_display_device) {
      return {};
    }
    return m_display_device->getDisplayName(device_id);
  }

  bool LinuxSettingsManager::resetPersistence() {
    if (!m_persistence) {
      return true;  // nothing to reset
    }
    return m_persistence->clear();
  }

}  // namespace display_device
