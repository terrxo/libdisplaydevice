/**
 * @file tools/test_evdi_phase2.cpp
 * @brief Standalone smoke test for the Phase 2 evdi_layer implementation.
 *
 * Creates a virtual display, leaves it up for a few seconds so you can
 * inspect it via `wlr-randr` / `cosmic-randr list`, then tears it down.
 *
 * Usage:
 *   ./test_evdi_phase2 [width=1440] [height=2560] [refresh=60]
 */
#include "display_device/linux/evdi_layer.h"
#include "display_device/linux/types.h"
#include "display_device/logging.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
  unsigned int W = (argc > 1) ? std::strtoul(argv[1], nullptr, 10) : 1440;
  unsigned int H = (argc > 2) ? std::strtoul(argv[2], nullptr, 10) : 2560;
  unsigned int R = (argc > 3) ? std::strtoul(argv[3], nullptr, 10) : 60;

  std::cout << "[test] Building config: " << W << "x" << H << "@" << R << "Hz\n";

  display_device::VkmsVirtualDisplayConfig cfg;
  cfg.m_name = "apollo-test";
  cfg.m_resolution = {W, H};
  cfg.m_refresh_rate = {R, 1};

  std::cout << "[test] Building EDID...\n";
  auto edid = display_device::evdi::buildEdid(cfg.m_resolution, cfg.m_refresh_rate);
  if (edid.empty()) {
    std::cerr << "[test] buildEdid returned empty\n";
    return 1;
  }
  std::cout << "[test] EDID size: " << edid.size() << " bytes; checksum: 0x"
            << std::hex << static_cast<int>(edid[127]) << std::dec << "\n";
  // Verify checksum
  unsigned int sum = 0;
  for (auto b : edid) sum += b;
  if ((sum & 0xff) != 0) {
    std::cerr << "[test] EDID checksum invalid! sum mod 256 = " << (sum & 0xff) << "\n";
    return 1;
  }
  std::cout << "[test] EDID checksum valid.\n";

  std::cout << "[test] Ensuring evdi module loaded...\n";
  if (!display_device::evdi::ensureModuleLoaded()) {
    std::cerr << "[test] FAILED: evdi module not available\n";
    return 1;
  }
  std::cout << "[test] evdi module OK.\n";

  std::cout << "[test] Creating virtual display...\n";
  auto vd = display_device::evdi::createVirtualDisplay(cfg);
  if (!vd) {
    std::cerr << "[test] FAILED: createVirtualDisplay returned nullopt\n";
    return 1;
  }
  std::cout << "[test] SUCCESS! Virtual display created:\n"
            << "       card_index    = " << vd->m_card_index << "\n"
            << "       drm_card_path = " << vd->m_drm_card_path << "\n"
            << "       connector     = " << vd->m_connector_name << "\n";

  std::cout << "[test] Sleeping 8s. Run `wlr-randr` or "
               "`ls /sys/class/drm/` in another terminal to verify the display.\n";
  std::this_thread::sleep_for(std::chrono::seconds(8));

  std::cout << "[test] Destroying virtual display...\n";
  if (!display_device::evdi::destroyVirtualDisplay(*vd)) {
    std::cerr << "[test] WARN: destroyVirtualDisplay returned false\n";
  }

  std::cout << "[test] Cleanup: removing all evdi platform devices...\n";
  display_device::evdi::removeAllPlatformDevices();

  std::cout << "[test] Done.\n";
  return 0;
}
