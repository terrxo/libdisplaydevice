/**
 * @file src/linux/include/display_device/linux/cosmic_randr_layer.h
 * @brief Pragmatic Phase 3: shell out to `cosmic-randr` for output
 *        configuration on Cosmic Wayland.
 *
 * Why cosmic-randr instead of a wlr-output-management protocol client:
 *   - cosmic-randr ships with cosmic-comp on Pop!_OS 24.04.
 *   - It's the tool that already speaks the right Wayland protocol
 *     against the running compositor.
 *   - Apollo runs as the user's process inside the Cosmic session, so
 *     it inherits WAYLAND_DISPLAY / XDG_RUNTIME_DIR and the invocation
 *     "just works".
 *   - Avoids ~600 LoC of Wayland protocol bindings + scanner + event loop.
 *   - Subprocess overhead is fine (display reconfig is a rare event).
 *
 * Trade-off:
 *   - Cosmic-only. For other Wayland compositors we'd need a real
 *     wlr-output-management client. Worth doing in a future Phase 3.5.
 *
 * Output identifier convention:
 *   `cosmic-randr` identifies outputs by their connector name (e.g.
 *   `HDMI-A-1`, `DVI-I-1`). For evdi-created displays, the connector
 *   name is what `findEvdiConnector()` returns.
 */
#pragma once

// system includes
#include <optional>
#include <string>
#include <vector>

// local includes
#include "display_device/linux/types.h"

namespace display_device::cosmic {

  struct OutputInfo {
    std::string m_name;         ///< connector name like HDMI-A-1, DVI-I-1
    bool m_enabled {false};
    Resolution m_current_resolution {};
    Rational m_current_refresh {};
    Point m_position {};
    bool m_primary {false};
  };

  /// Whether `cosmic-randr` is available on PATH.
  [[nodiscard]] bool isAvailable();

  /// List all heads currently known to the compositor.
  /// Returns empty list if cosmic-randr is unavailable or the call fails.
  [[nodiscard]] std::vector<OutputInfo> listOutputs();

  /// Enable an output (it must already be known to the compositor).
  [[nodiscard]] bool enableOutput(const std::string &name);

  /// Disable an output.
  [[nodiscard]] bool disableOutput(const std::string &name);

  /// Set position of an output. Cosmic uses logical coordinates.
  [[nodiscard]] bool setPosition(const std::string &name, int x, int y);

  /// Set the mode (resolution + refresh) for an output.
  [[nodiscard]] bool setMode(const std::string &name,
                             unsigned int width,
                             unsigned int height,
                             unsigned int refresh_hz);

}  // namespace display_device::cosmic
