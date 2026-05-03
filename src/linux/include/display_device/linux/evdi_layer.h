/**
 * @file src/linux/include/display_device/linux/evdi_layer.h
 * @brief Thin wrapper around libevdi for runtime virtual display creation.
 *
 * Why evdi instead of vkms:
 *   - vkms's configfs userspace API is gated by CONFIG_DRM_VKMS_CONFIGFS,
 *     which Pop!_OS / mainstream distros currently do NOT enable.
 *   - evdi is purpose-built for runtime virtual display lifecycle, used in
 *     production by DisplayLink, available as DKMS via DisplayLink/evdi
 *     upstream. Multiple instances supported. Userspace lib `libevdi`
 *     provides a clean C API: open/connect/disconnect/close.
 *
 * Lifecycle for a single virtual display:
 *   1. evdi_add_device()                 -> creates platform device, /dev/dri/cardN
 *   2. evdi_open(N)                      -> obtain handle
 *   3. evdi_connect(h, edid, ...)        -> hot-plugs the connector into the
 *                                            DRM device with our chosen modes
 *   4. (compositor sees output, draws)
 *   5. (Sunshine KMS-captures /dev/dri/cardN, untouched by evdi)
 *   6. evdi_disconnect(h)                -> hot-unplug
 *   7. evdi_close(h)                     -> release handle (device persists)
 *   8. (optional) writes to /sys/devices/evdi/remove_all to remove platform devs
 *
 * Note on consumers: evdi normally expects a userspace consumer to call
 * evdi_grab_pixels() and ack frames. For Sunshine our pixel consumer is the
 * KMS capture path, NOT evdi. Empirically (see DisplayLink samples) the
 * compositor still commits frames to the DRM output even without an evdi
 * consumer; we simply don't register any evdi_buffer.
 *
 * Permissions: opening /dev/dri/cardN typically needs the user to be in
 * the "video" group (already true for the apollo user on Pop!_OS).
 * Adding/removing platform devices via /sys/devices/evdi/{add,remove_all}
 * needs root - handled by a small setuid helper or polkit rule.
 */
#pragma once

// system includes
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// local includes
#include "display_device/linux/types.h"

namespace display_device::evdi {

  /// Opaque handle wrapping libevdi's evdi_handle plus our bookkeeping.
  struct VirtualDisplay {
    int m_card_index {-1};                ///< /dev/dri/cardN
    std::string m_drm_card_path;          ///< /sys/class/drm/cardN
    std::string m_connector_name;         ///< e.g. "DVI-I-1"
    void *m_handle {nullptr};             ///< actually evdi_handle (libevdi)
    VkmsVirtualDisplayConfig m_config {}; ///< what we asked for
  };

  /// Ensure the evdi kernel module is loaded. Idempotent; needs root.
  [[nodiscard]] bool ensureModuleLoaded();

  /// Whether libevdi can talk to /dev/dri at all (basic sanity).
  [[nodiscard]] bool isApiUsable();

  /**
   * @brief Build a minimal EDID 1.4 blob for the given mode.
   *
   * 128 bytes, single detailed timing block with our requested
   * width/height/refresh, valid checksum byte. Generic Apollo-branded
   * manufacturer ID. Reference: VESA E-EDID 1.4.
   */
  [[nodiscard]] std::vector<std::uint8_t> buildEdid(const Resolution &resolution,
                                                   const Rational &refresh);

  /**
   * @brief Add a new evdi platform device and connect it with our EDID.
   *
   * Steps internally:
   *   1. echo 1 > /sys/devices/evdi/add (via setuid helper or root cap)
   *   2. read updated count, find new /dev/dri/cardN by diffing against
   *      a snapshot taken before step 1
   *   3. evdi_open(N)
   *   4. evdi_connect(handle, edid_blob, edid_size, sku_area_limit)
   *
   * @return populated VirtualDisplay on success, std::nullopt on failure.
   */
  [[nodiscard]] std::optional<VirtualDisplay> createVirtualDisplay(const VkmsVirtualDisplayConfig &cfg);

  /// Disconnect + close + best-effort remove the platform device.
  [[nodiscard]] bool destroyVirtualDisplay(VirtualDisplay &vd);

  /// Remove all evdi platform devices (cleanup on startup after crash).
  void removeAllPlatformDevices();

  /// Read /sys/devices/evdi/count.
  [[nodiscard]] int currentDeviceCount();

}  // namespace display_device::evdi
