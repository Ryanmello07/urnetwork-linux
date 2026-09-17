// Classifies Linux physical-path snapshots into reconnecting topology changes
// and estimator-only quality changes. The parser is header-only so the daemon
// and the dependency-free unit suite exercise the same rules.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace urnw {

struct LinuxNetworkQualitySnapshot {
  std::string interface_name;
  int carrier = -1;
  int wireless_signal_level = -1;
  std::uint64_t speed_bucket_mbps = 0;

  bool valid() const { return !interface_name.empty(); }
};

enum class LinuxNetworkChange { None, Quality, Path };

class LinuxNetworkQualityTracker {
 public:
  LinuxNetworkChange Observe(const LinuxNetworkQualitySnapshot& next) {
    if (!initialized_) {
      initialized_ = true;
      current_ = next;
      return LinuxNetworkChange::None;
    }
    const bool path = current_.valid() != next.valid() ||
                      current_.interface_name != next.interface_name ||
                      current_.carrier != next.carrier;
    const bool quality =
        current_.wireless_signal_level != next.wireless_signal_level ||
        current_.speed_bucket_mbps != next.speed_bucket_mbps;
    current_ = next;
    if (path) return LinuxNetworkChange::Path;
    return quality ? LinuxNetworkChange::Quality : LinuxNetworkChange::None;
  }

  void Reset() {
    initialized_ = false;
    current_ = {};
  }

 private:
  bool initialized_ = false;
  LinuxNetworkQualitySnapshot current_;
};

inline std::uint64_t LinuxNetworkSpeedBucket(std::uint64_t speedMbps) {
  if (speedMbps == 0) return 0;
  std::uint64_t bucket = 1;
  while (bucket <= std::numeric_limits<std::uint64_t>::max() / 2 &&
         bucket * 2 <= speedMbps)
    bucket *= 2;
  return bucket;
}

inline std::string LinuxDefaultRouteInterface(const std::string& routes,
                                              const std::string& excluded) {
  std::istringstream lines(routes);
  std::string line;
  std::string best;
  std::uint64_t bestMetric = std::numeric_limits<std::uint64_t>::max();
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string iface, destination, gateway, flagsText;
    std::uint64_t refCount = 0, use = 0, metric = 0;
    if (!(fields >> iface >> destination >> gateway >> flagsText >> refCount >> use >> metric))
      continue;
    std::uint64_t flags = 0;
    std::istringstream(flagsText) >> std::hex >> flags;
    if (iface == excluded || destination != "00000000" || (flags & 1) == 0) continue;
    if (metric < bestMetric || (metric == bestMetric && iface < best)) {
      best = iface;
      bestMetric = metric;
    }
  }
  return best;
}

inline int LinuxWirelessSignalLevel(const std::string& wireless,
                                    const std::string& interfaceName) {
  std::istringstream lines(wireless);
  std::string line;
  while (std::getline(lines, line)) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string iface = line.substr(0, colon);
    const auto first = iface.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    const auto last = iface.find_last_not_of(" \t");
    iface = iface.substr(first, last - first + 1);
    if (iface != interfaceName) continue;
    std::istringstream fields(line.substr(colon + 1));
    std::string status;
    double link = 0;
    if (!(fields >> status >> link)) return -1;
    return std::clamp(static_cast<int>(link / 14.0), 0, 4);
  }
  return -1;
}

}  // namespace urnw
