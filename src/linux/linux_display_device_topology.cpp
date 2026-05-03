/**
 * @file src/linux/linux_display_device_topology.cpp
 * @brief Topology = which outputs are enabled and how they're grouped.
 *
 * Phase 4 implementation:
 *   - getCurrentTopology: query cosmic-randr; enabled outputs each become
 *     their own single-element group (we don't model mirrored sets yet).
 *   - setTopology: enable/disable cosmic-randr outputs to match the desired
 *     state.
 */
#include "display_device/linux/cosmic_randr_layer.h"
#include "display_device/linux/linux_display_device.h"
#include "display_device/logging.h"

#include <algorithm>
#include <set>

namespace display_device {

  ActiveTopology LinuxDisplayDevice::getCurrentTopology() const {
    ActiveTopology topo;
    for (const auto &o : cosmic::listOutputs()) {
      if (o.m_enabled) {
        topo.push_back({o.m_name});
      }
    }
    return topo;
  }

  bool LinuxDisplayDevice::isTopologyValid(const ActiveTopology &topology) const {
    if (topology.empty()) {
      return false;
    }
    // Reject empty groups.
    for (const auto &group : topology) {
      if (group.empty()) {
        return false;
      }
    }
    return true;
  }

  bool LinuxDisplayDevice::isTopologyTheSame(const ActiveTopology &lhs,
                                             const ActiveTopology &rhs) const {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    auto canonicalise = [](ActiveTopology t) {
      for (auto &g : t) {
        std::sort(g.begin(), g.end());
      }
      std::sort(t.begin(), t.end());
      return t;
    };
    return canonicalise(lhs) == canonicalise(rhs);
  }

  bool LinuxDisplayDevice::setTopology(const ActiveTopology &new_topology) {
    if (!cosmic::isAvailable()) {
      DD_LOG(error) << "setTopology: cosmic-randr unavailable";
      return false;
    }
    // Build the set of devices to enable.
    std::set<std::string> wanted;
    for (const auto &group : new_topology) {
      for (const auto &id : group) {
        wanted.insert(id);
      }
    }
    // Compare to current.
    auto current_outputs = cosmic::listOutputs();
    std::set<std::string> currently_enabled;
    for (const auto &o : current_outputs) {
      if (o.m_enabled) {
        currently_enabled.insert(o.m_name);
      }
    }
    // Apply diffs.
    bool ok = true;
    for (const auto &name : wanted) {
      if (!currently_enabled.count(name)) {
        if (!cosmic::enableOutput(name)) {
          DD_LOG(warning) << "Failed to enable output: " << name;
          ok = false;
        }
      }
    }
    for (const auto &name : currently_enabled) {
      if (!wanted.count(name)) {
        if (!cosmic::disableOutput(name)) {
          DD_LOG(warning) << "Failed to disable output: " << name;
          ok = false;
        }
      }
    }
    return ok;
  }

}  // namespace display_device
