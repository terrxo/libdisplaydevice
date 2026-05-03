/**
 * @file src/linux/evdi_layer.cpp
 * @brief Real implementation of the libevdi wrapper for Apollo's
 *        Linux virtual display backend.
 *
 * EDID generation:
 *   We build a 128-byte EDID 1.4 blob with one Detailed Timing Descriptor
 *   (DTD) for the requested resolution/refresh. Timings use simplified
 *   CVT-RB (Coordinated Video Timings, Reduced Blanking):
 *     Hblank = 80 px (front 8, sync 32, back 40)
 *     Vblank = 28 ln (front 3, sync 5, back 20)
 *     pclk   = (W + Hblank) * (H + Vblank) * R   Hz
 *   Reference: VESA E-EDID 1.4, CVT-RB 1.0.
 *
 * Privileged ops:
 *   /sys/devices/evdi/{add,remove_all} require root. Apollo invokes the
 *   small setuid helper binary `apollo-evdi-helper` for these operations.
 */
#include "display_device/linux/evdi_layer.h"

// system includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

// local includes
#include "display_device/logging.h"

#if defined(LIBDD_HAVE_LIBEVDI)
extern "C" {
  #include <evdi_lib.h>
}
#endif

namespace display_device::evdi {

  namespace {
    constexpr const char *kEvdiSysfsRoot = "/sys/devices/evdi";
    constexpr const char *kEvdiSysfsCount = "/sys/devices/evdi/count";
    constexpr const char *kSysModuleEvdi = "/sys/module/evdi";
    constexpr const char *kDrmRoot = "/sys/class/drm";
    constexpr const char *kHelperBin = "/usr/local/bin/apollo-evdi-helper";

    /// fork+exec the privileged helper. argv[1] is one of {modprobe,add,remove-all}.
    /// Returns true on exit code 0.
    bool runHelper(const char *cmd) {
      pid_t pid = fork();
      if (pid < 0) {
        DD_LOG(error) << "fork failed";
        return false;
      }
      if (pid == 0) {
        execl(kHelperBin, kHelperBin, cmd, (char *) nullptr);
        // execl only returns on failure
        DD_LOG(error) << "execl " << kHelperBin << " failed";
        _exit(127);
      }
      int status = 0;
      if (waitpid(pid, &status, 0) < 0) {
        DD_LOG(error) << "waitpid failed";
        return false;
      }
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    /// Snapshot the set of /dev/dri/cardN devices currently present.
    std::set<int> snapshotCards() {
      namespace fs = std::filesystem;
      std::set<int> cards;
      std::error_code ec;
      for (const auto &entry : fs::directory_iterator("/dev/dri", ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("card", 0) == 0) {
          try {
            cards.insert(std::stoi(name.substr(4)));
          }
          catch (...) {}
        }
      }
      return cards;
    }

    /// Find the first connector path under /sys/class/drm for cardN.
    std::string findEvdiConnector(int card_index) {
      namespace fs = std::filesystem;
      std::error_code ec;
      const std::string prefix = "card" + std::to_string(card_index) + "-";
      for (const auto &entry : fs::directory_iterator(kDrmRoot, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
          return name.substr(prefix.size());  // e.g. "DVI-I-1"
        }
      }
      return {};
    }

    /// Encode a 3-letter PnP manufacturer code into 2 EDID bytes (big-endian).
    /// Each letter is 1=A..26=Z (1..26), packed as 5 bits each.
    void encodeManufacturer(const char (&abc)[4], std::uint8_t out[2]) {
      const std::uint16_t a = static_cast<std::uint16_t>(abc[0] - 'A' + 1) & 0x1f;
      const std::uint16_t b = static_cast<std::uint16_t>(abc[1] - 'A' + 1) & 0x1f;
      const std::uint16_t c = static_cast<std::uint16_t>(abc[2] - 'A' + 1) & 0x1f;
      const std::uint16_t packed = (a << 10) | (b << 5) | c;
      out[0] = static_cast<std::uint8_t>((packed >> 8) & 0xff);
      out[1] = static_cast<std::uint8_t>(packed & 0xff);
    }

    /// Patch a 13-byte string (right-padded with 0x20, ending with 0x0a if shorter
    /// than 13) into the data area of an EDID Display-Descriptor block.
    /// dst must point to the descriptor data area (offset+5 from descriptor start).
    void writeDescriptorString(std::uint8_t *dst, const std::string &s) {
      for (size_t i = 0; i < 13; ++i) {
        if (i < s.size()) {
          dst[i] = static_cast<std::uint8_t>(s[i]);
        } else if (i == s.size()) {
          dst[i] = 0x0a;
        } else {
          dst[i] = 0x20;
        }
      }
    }
  }  // namespace

  bool ensureModuleLoaded() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(kSysModuleEvdi, ec)) {
      return true;
    }
    DD_LOG(info) << "evdi module not loaded, invoking helper to modprobe";
    if (!runHelper("modprobe")) {
      DD_LOG(error) << "Failed to load evdi module via helper. "
                       "Is " << kHelperBin << " installed setuid root?";
      return false;
    }
    // Wait briefly for sysfs to appear.
    for (int i = 0; i < 20; ++i) {
      if (fs::exists(kSysModuleEvdi, ec) && fs::exists(kEvdiSysfsRoot, ec)) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
  }

  bool isApiUsable() {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(kSysModuleEvdi, ec) && fs::exists(kEvdiSysfsRoot, ec);
  }

  int currentDeviceCount() {
    std::ifstream f(kEvdiSysfsCount);
    if (!f) {
      return -1;
    }
    int n {-1};
    f >> n;
    return n;
  }

  std::vector<std::uint8_t> buildEdid(const Resolution &res, const Rational &refresh) {
    // Sanity: clamp inputs to reasonable ranges to avoid overflow in pclk math.
    const std::uint32_t W = std::clamp<std::uint32_t>(res.m_width, 320u, 16384u);
    const std::uint32_t H = std::clamp<std::uint32_t>(res.m_height, 240u, 16384u);
    const std::uint32_t denom = refresh.m_denominator ? refresh.m_denominator : 1;
    std::uint32_t R = refresh.m_numerator / denom;
    if (R == 0) {
      R = 60;
    }
    R = std::clamp<std::uint32_t>(R, 24u, 240u);

    // CVT-RB simplified timings.
    constexpr std::uint32_t HBLANK = 80;
    constexpr std::uint32_t VBLANK = 28;
    constexpr std::uint32_t HSYNC_OFFSET = 8;
    constexpr std::uint32_t HSYNC_PW = 32;
    constexpr std::uint32_t VSYNC_OFFSET = 3;
    constexpr std::uint32_t VSYNC_PW = 5;

    const std::uint32_t htotal = W + HBLANK;
    const std::uint32_t vtotal = H + VBLANK;
    const std::uint64_t pclk_hz = static_cast<std::uint64_t>(htotal) * vtotal * R;
    const std::uint32_t pclk_10khz = static_cast<std::uint32_t>(pclk_hz / 10000ULL);
    if (pclk_10khz == 0 || pclk_10khz > 65535) {
      DD_LOG(error) << "EDID pixel clock out of range: " << pclk_10khz << " (10kHz units)";
      return {};
    }

    std::vector<std::uint8_t> edid(128, 0x00);

    // Header
    static constexpr std::uint8_t kHeader[8] = {
      0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    std::memcpy(edid.data(), kHeader, sizeof(kHeader));

    // Manufacturer ID (3-letter PnP code) - "APO" for Apollo
    encodeManufacturer({'A', 'P', 'O', 0}, &edid[8]);
    // Product code (LE): 0x0001
    edid[10] = 0x01;
    edid[11] = 0x00;
    // Serial number (LE): 0
    edid[12] = edid[13] = edid[14] = edid[15] = 0;
    // Week of manufacture: model year flag
    edid[16] = 0xff;
    // Year of manufacture (offset from 1990): 35 -> 2025
    edid[17] = 35;
    // EDID version 1.4
    edid[18] = 0x01;
    edid[19] = 0x04;

    // Video input definition: digital, 8 bpc, DisplayPort
    edid[20] = 0xa5;
    // Max horizontal/vertical image size (cm): 53 x 30 (~24")
    edid[21] = 53;
    edid[22] = 30;
    // Display gamma: 2.2 -> (gamma * 100) - 100 = 120
    edid[23] = 120;
    // Feature support: digital, sRGB, preferred timing in DTD #1, continuous
    edid[24] = 0x06;

    // Chromaticity coordinates: standard sRGB (placeholders)
    static constexpr std::uint8_t kChroma[10] = {
      0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26, 0x0f, 0x50, 0x54};
    std::memcpy(&edid[25], kChroma, sizeof(kChroma));

    // Established timings (legacy): none
    edid[35] = edid[36] = edid[37] = 0;

    // Standard timings: 8 entries x 2 bytes; 0x01 0x01 = unused
    for (size_t i = 38; i < 54; i += 2) {
      edid[i] = 0x01;
      edid[i + 1] = 0x01;
    }

    // ---- Detailed Timing Descriptor #1 (offsets 54..71) ----
    {
      auto *d = &edid[54];
      d[0] = static_cast<std::uint8_t>(pclk_10khz & 0xff);
      d[1] = static_cast<std::uint8_t>((pclk_10khz >> 8) & 0xff);
      d[2] = static_cast<std::uint8_t>(W & 0xff);
      d[3] = static_cast<std::uint8_t>(HBLANK & 0xff);
      d[4] = static_cast<std::uint8_t>(((W >> 8) << 4) & 0xf0) |
             static_cast<std::uint8_t>((HBLANK >> 8) & 0x0f);
      d[5] = static_cast<std::uint8_t>(H & 0xff);
      d[6] = static_cast<std::uint8_t>(VBLANK & 0xff);
      d[7] = static_cast<std::uint8_t>(((H >> 8) << 4) & 0xf0) |
             static_cast<std::uint8_t>((VBLANK >> 8) & 0x0f);
      d[8] = static_cast<std::uint8_t>(HSYNC_OFFSET & 0xff);
      d[9] = static_cast<std::uint8_t>(HSYNC_PW & 0xff);
      d[10] = static_cast<std::uint8_t>(((VSYNC_OFFSET & 0x0f) << 4) |
                                        (VSYNC_PW & 0x0f));
      d[11] = 0;  // high bits of sync values (all zero for our small offsets)
      d[12] = 0;  // image size mm low - 0 = unspecified
      d[13] = 0;
      d[14] = 0;
      d[15] = 0;  // h border
      d[16] = 0;  // v border
      // Flags: digital separate sync, +H polarity, +V polarity
      d[17] = 0x1e;
    }

    // ---- Descriptor #2 (offsets 72..89): Display product name ----
    {
      auto *d = &edid[72];
      d[0] = 0x00;
      d[1] = 0x00;
      d[2] = 0x00;
      d[3] = 0xfc;  // Display Product Name tag
      d[4] = 0x00;
      writeDescriptorString(&d[5], "Apollo VD");
    }

    // ---- Descriptor #3 (offsets 90..107): Display Range Limits ----
    {
      auto *d = &edid[90];
      d[0] = 0x00;
      d[1] = 0x00;
      d[2] = 0x00;
      d[3] = 0xfd;  // Display Range Limits tag
      d[4] = 0x00;
      d[5] = 24;    // V min Hz
      d[6] = 240;   // V max Hz (cap)
      d[7] = 24;    // H min kHz
      d[8] = 240;   // H max kHz (cap)
      d[9] = 240;   // pixel clock max in 10MHz units (2400 MHz - generous)
      d[10] = 0x01; // GTF default
      d[11] = 0x0a;
      d[12] = 0x20;
      d[13] = 0x20;
      d[14] = 0x20;
      d[15] = 0x20;
      d[16] = 0x20;
      d[17] = 0x20;
    }

    // ---- Descriptor #4 (offsets 108..125): Dummy descriptor ----
    {
      auto *d = &edid[108];
      d[0] = 0x00;
      d[1] = 0x00;
      d[2] = 0x00;
      d[3] = 0x10;  // Dummy descriptor tag
      d[4] = 0x00;
      // padded with zeros (already zero from initialisation)
    }

    // Extension block count
    edid[126] = 0;

    // Checksum: sum of all 128 bytes mod 256 must == 0
    std::uint8_t sum = 0;
    for (size_t i = 0; i < 127; ++i) {
      sum = static_cast<std::uint8_t>(sum + edid[i]);
    }
    edid[127] = static_cast<std::uint8_t>(0x100 - sum);

    return edid;
  }

  std::optional<VirtualDisplay> createVirtualDisplay(const VkmsVirtualDisplayConfig &cfg) {
#if !defined(LIBDD_HAVE_LIBEVDI)
    DD_LOG(error) << "evdi backend built without libevdi - reconfigure with libevdi-dev";
    (void) cfg;
    return std::nullopt;
#else
    if (!ensureModuleLoaded()) {
      DD_LOG(error) << "evdi module not available";
      return std::nullopt;
    }

    // Step 1: snapshot existing /dev/dri/cardN devices
    const auto before = snapshotCards();

    // Step 2: ask the helper to add a new platform device
    if (!runHelper("add")) {
      DD_LOG(error) << "Helper failed to add evdi platform device";
      return std::nullopt;
    }

    // Step 3: poll for a new card to appear (DRM hotplug is async)
    int new_card = -1;
    for (int attempt = 0; attempt < 40; ++attempt) {
      const auto after = snapshotCards();
      std::set<int> diff;
      std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                          std::inserter(diff, diff.begin()));
      if (!diff.empty()) {
        new_card = *diff.begin();
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (new_card < 0) {
      DD_LOG(error) << "Timed out waiting for evdi DRM card to appear";
      return std::nullopt;
    }
    DD_LOG(info) << "evdi: new card index = " << new_card;

    // Step 4: open via libevdi
    auto status = evdi_check_device(new_card);
    if (status != AVAILABLE) {
      DD_LOG(error) << "evdi_check_device(" << new_card << ") not AVAILABLE: " << status;
      return std::nullopt;
    }
    evdi_handle handle = evdi_open(new_card);
    if (handle == EVDI_INVALID_HANDLE) {
      DD_LOG(error) << "evdi_open(" << new_card << ") failed";
      return std::nullopt;
    }

    // Step 5: build EDID and connect
    const auto edid = buildEdid(cfg.m_resolution, cfg.m_refresh_rate);
    if (edid.empty()) {
      DD_LOG(error) << "buildEdid produced empty blob";
      evdi_close(handle);
      return std::nullopt;
    }
    // sku_area_limit: max width*height supported. Use 8K^2 to be permissive.
    const std::uint32_t sku_area = 7680u * 4320u;
    evdi_connect(handle, edid.data(), static_cast<unsigned int>(edid.size()), sku_area);
    DD_LOG(info) << "evdi: connected card" << new_card << " "
                 << cfg.m_resolution.m_width << "x" << cfg.m_resolution.m_height
                 << "@" << (cfg.m_refresh_rate.m_numerator /
                            std::max(1u, cfg.m_refresh_rate.m_denominator)) << "Hz";

    // Step 6: locate the connector name (now that connect happened the
    // connector should be exposed by sysfs)
    std::string connector;
    for (int attempt = 0; attempt < 20; ++attempt) {
      connector = findEvdiConnector(new_card);
      if (!connector.empty()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (connector.empty()) {
      DD_LOG(warning) << "evdi: connector for card" << new_card << " not visible yet";
    }

    VirtualDisplay vd;
    vd.m_card_index = new_card;
    vd.m_drm_card_path = std::string("/sys/class/drm/card") + std::to_string(new_card);
    vd.m_connector_name = connector;
    vd.m_handle = handle;
    vd.m_config = cfg;
    return vd;
#endif
  }

  bool destroyVirtualDisplay(VirtualDisplay &vd) {
#if !defined(LIBDD_HAVE_LIBEVDI)
    (void) vd;
    return false;
#else
    if (vd.m_handle != nullptr) {
      auto h = static_cast<evdi_handle>(vd.m_handle);
      evdi_disconnect(h);
      evdi_close(h);
      vd.m_handle = nullptr;
    }
    // We don't remove the platform device individually - evdi has no
    // per-device remove sysfs. Use removeAllPlatformDevices() at session
    // end if you want a clean slate.
    return true;
#endif
  }

  void removeAllPlatformDevices() {
    if (!isApiUsable()) {
      return;
    }
    if (!runHelper("remove-all")) {
      DD_LOG(warning) << "Helper failed to remove all evdi platform devices";
    }
  }

}  // namespace display_device::evdi
