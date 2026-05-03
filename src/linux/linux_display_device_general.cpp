/**
 * @file src/linux/linux_display_device_general.cpp
 * @brief LinuxDisplayDevice ctor/dtor + general queries.
 *
 * Phase 4 wires evdi (virtual display creation) + cosmic-randr (compositor
 * config) together behind the LinuxDisplayDeviceInterface. Active virtual
 * displays are stashed in m_active so destroy can find them by id.
 */
#include "display_device/linux/linux_display_device.h"
#include "display_device/linux/cosmic_randr_layer.h"
#include "display_device/linux/evdi_layer.h"
#include "display_device/logging.h"

#include <map>
#include <mutex>

namespace display_device {

  namespace {
    // Process-wide bookkeeping for virtual displays we created.
    // Keyed by device_id (== connector name).
    struct ActiveRegistry {
      std::mutex m_mutex;
      std::map<std::string, evdi::VirtualDisplay> m_active;
    };

    ActiveRegistry &registry() {
      static ActiveRegistry r;
      return r;
    }
  }  // namespace

  LinuxDisplayDevice::LinuxDisplayDevice() {
    // Sweep any orphan evdi platform devices left from a prior crash.
    evdi::removeAllPlatformDevices();
  }

  LinuxDisplayDevice::~LinuxDisplayDevice() {
    // Tear down any virtual displays still tracked.
    std::lock_guard<std::mutex> lk(registry().m_mutex);
    for (auto &[id, vd] : registry().m_active) {
      (void) evdi::destroyVirtualDisplay(vd);
    }
    registry().m_active.clear();
    evdi::removeAllPlatformDevices();
  }

  bool LinuxDisplayDevice::isApiAccessAvailable() const {
    return evdi::ensureModuleLoaded() && evdi::isApiUsable();
  }

  EnumeratedDeviceList LinuxDisplayDevice::enumAvailableDevices() const {
    // Phase 4 NOTE: parsing cosmic-randr output reliably enough to populate
    // every EnumeratedDevice::Info field is finicky and the format isn't
    // stable across cosmic versions. Apollo's encoder-probing logic in
    // src/video.cpp uses two paths:
    //   - empty list  -> assume "OS doesn't support libdisplaydevice", fall
    //                   back to direct KMS probing (works on Linux/Cosmic)
    //   - non-empty   -> require at least one device with m_info populated
    //
    // Returning a non-empty list with partial info trips the failure path.
    // For now we return empty so Apollo's KMS-direct probing handles it.
    // Virtual display creation/destruction (the actual differentiating
    // feature) flows through the explicit createVirtualDisplay /
    // destroyVirtualDisplay calls, NOT through enumeration.
    //
    // Phase 5 future work: implement a real wlr-output-management Wayland
    // client and populate this properly.
    return {};
  }

  std::string LinuxDisplayDevice::getDisplayName(const std::string &device_id) const {
    // On Linux the device_id IS the connector name; no translation needed.
    return device_id;
  }

  std::string LinuxDisplayDevice::createVirtualDisplay(const VkmsVirtualDisplayConfig &cfg) {
    auto vd = evdi::createVirtualDisplay(cfg);
    if (!vd || vd->m_connector_name.empty()) {
      DD_LOG(error) << "createVirtualDisplay: evdi creation failed";
      return {};
    }
    const std::string id = vd->m_connector_name;
    {
      std::lock_guard<std::mutex> lk(registry().m_mutex);
      registry().m_active[id] = std::move(*vd);
    }
    DD_LOG(info) << "Virtual display registered: " << id;
    return id;
  }

  bool LinuxDisplayDevice::destroyVirtualDisplay(const std::string &device_id) {
    evdi::VirtualDisplay vd;
    {
      std::lock_guard<std::mutex> lk(registry().m_mutex);
      auto it = registry().m_active.find(device_id);
      if (it == registry().m_active.end()) {
        DD_LOG(warning) << "destroyVirtualDisplay: id not in registry: " << device_id;
        return false;
      }
      vd = std::move(it->second);
      registry().m_active.erase(it);
    }
    return evdi::destroyVirtualDisplay(vd);
  }

}  // namespace display_device
