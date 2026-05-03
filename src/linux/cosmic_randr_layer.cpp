/**
 * @file src/linux/cosmic_randr_layer.cpp
 * @brief Subprocess wrapper around `cosmic-randr`.
 */
#include "display_device/linux/cosmic_randr_layer.h"

// system includes
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

// local includes
#include "display_device/logging.h"

namespace display_device::cosmic {

  namespace {
    constexpr const char *kCosmicRandr = "cosmic-randr";

    /// Find cosmic-randr in PATH (cached).
    const std::string &cosmicRandrPath() {
      static std::string cached_path = []() -> std::string {
        // Quick PATH probe: try the known locations first.
        for (const auto *p : {"/usr/bin/cosmic-randr", "/usr/local/bin/cosmic-randr"}) {
          if (std::filesystem::exists(p)) {
            return p;
          }
        }
        return {};
      }();
      return cached_path;
    }

    /// Run cosmic-randr with the given args. Captures stdout. Returns
    /// (exit_code, stdout_bytes). If exec fails, exit_code is -1.
    std::pair<int, std::string> runCommand(const std::vector<std::string> &args) {
      const auto &path = cosmicRandrPath();
      if (path.empty()) {
        return {-1, {}};
      }

      // Build argv
      std::vector<char *> argv;
      argv.reserve(args.size() + 2);
      argv.push_back(const_cast<char *>(path.c_str()));
      for (const auto &a : args) {
        argv.push_back(const_cast<char *>(a.c_str()));
      }
      argv.push_back(nullptr);

      int pipefd[2];
      if (pipe(pipefd) < 0) {
        return {-1, {}};
      }

      pid_t pid = fork();
      if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {-1, {}};
      }
      if (pid == 0) {
        // child
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
          _exit(127);
        }
        close(pipefd[1]);
        execv(path.c_str(), argv.data());
        _exit(127);
      }
      // parent
      close(pipefd[1]);
      std::string out;
      std::array<char, 4096> buf {};
      ssize_t n;
      while ((n = read(pipefd[0], buf.data(), buf.size())) > 0) {
        out.append(buf.data(), n);
      }
      close(pipefd[0]);
      int status = 0;
      waitpid(pid, &status, 0);
      int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      return {code, std::move(out)};
    }
  }  // namespace

  bool isAvailable() {
    return !cosmicRandrPath().empty();
  }

  std::vector<OutputInfo> listOutputs() {
    // `cosmic-randr list` emits human-readable output. We do a best-effort
    // parse: each output starts with the name, with indented lines for
    // mode/position/etc. This is intentionally lenient because the format
    // can change between cosmic versions; failures are non-fatal.
    std::vector<OutputInfo> result;
    auto [code, out] = runCommand({"list"});
    if (code != 0) {
      DD_LOG(debug) << "cosmic-randr list failed with code " << code;
      return result;
    }
    OutputInfo cur;
    bool have = false;
    auto flush = [&]() {
      if (have && !cur.m_name.empty()) {
        result.push_back(cur);
      }
      cur = {};
      have = false;
    };
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
      if (line.empty()) {
        continue;
      }
      // Output name lines have no leading whitespace.
      if (line[0] != ' ' && line[0] != '\t') {
        flush();
        // Name typically followed by a colon or space.
        auto end = line.find_first_of(": \t");
        cur.m_name = (end == std::string::npos) ? line : line.substr(0, end);
        have = true;
        continue;
      }
      // Indented lines: scrape interesting fields.
      if (line.find("enabled: true") != std::string::npos
          || line.find("Enabled: true") != std::string::npos) {
        cur.m_enabled = true;
      }
      if (line.find("primary: true") != std::string::npos
          || line.find("Primary: true") != std::string::npos) {
        cur.m_primary = true;
      }
      // Resolution like "current: 2560x1440@59.96"
      auto at = line.find('@');
      auto x = line.find('x');
      if (at != std::string::npos && x != std::string::npos && x < at) {
        try {
          // Walk back from x to grab width
          size_t w_start = x;
          while (w_start > 0 && (std::isdigit(static_cast<unsigned char>(line[w_start - 1])))) {
            --w_start;
          }
          // Walk forward from x for height
          size_t h_end = x + 1;
          while (h_end < line.size() && std::isdigit(static_cast<unsigned char>(line[h_end]))) {
            ++h_end;
          }
          unsigned int W = std::stoul(line.substr(w_start, x - w_start));
          unsigned int H = std::stoul(line.substr(x + 1, h_end - x - 1));
          if (W > 0 && H > 0 && cur.m_current_resolution.m_width == 0) {
            cur.m_current_resolution = {W, H};
          }
        }
        catch (...) {
          // Parse errors are non-fatal.
        }
      }
    }
    flush();
    return result;
  }

  bool enableOutput(const std::string &name) {
    auto [code, _] = runCommand({"enable", name});
    return code == 0;
  }

  bool disableOutput(const std::string &name) {
    auto [code, _] = runCommand({"disable", name});
    return code == 0;
  }

  bool setPosition(const std::string &name, int x, int y) {
    auto [code, _] = runCommand({"position", name, std::to_string(x), std::to_string(y)});
    return code == 0;
  }

  bool setMode(const std::string &name, unsigned int width, unsigned int height, unsigned int refresh_hz) {
    auto [code, _] = runCommand({
        "mode",
        name,
        std::to_string(width),
        std::to_string(height),
        "--refresh",
        std::to_string(refresh_hz),
    });
    return code == 0;
  }

}  // namespace display_device::cosmic
