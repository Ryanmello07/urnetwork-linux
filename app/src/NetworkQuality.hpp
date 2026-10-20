// Classifies Linux physical-path snapshots into reconnecting topology changes
// and estimator-only quality changes. The parser is header-only so the daemon
// and the dependency-free unit suite exercise the same rules.
//
// WHAT A PATH IS. Per family, the way the SDK's next socket would leave this
// machine: the interface it would leave through, the default route's next hop
// on that interface, and the source address the kernel would bind (for IPv6,
// its /64, so a privacy address rotating inside one network is not a new
// network). The daemon measures the source with the egress witness's own probe
// (TunnelHost ReadNetworkPath): a datagram socket connect()ed to an address
// routed nowhere, which runs the kernel's real route lookup and sends nothing.
// That is what makes the snapshot follow the kernel's decision rather than a
// reading of /proc/net/route that can disagree with it -- a linkdown route the
// kernel still uses, a policy rule, equal metrics.
//
// WHEN A PATH CHANGE IS A KICK. connect's kick now cancels a dial in flight and
// re-dials at once, so every kick costs a dial, and a kick at the wrong moment
// costs two. The tracker therefore kicks once, at the moment there is somewhere
// new to dial:
//   * LOSS NEVER KICKS. A family going away, or the whole machine going
//     offline, leaves nothing to re-dial over; the kick comes when a path
//     returns. A carrier dropping under a route the kernel still uses changes
//     no path at all (NetworkManager keeps a cable's routes for a few seconds
//     after it is pulled), so the kick waits for the route to move.
//   * A NEW OR MOVED PATH KICKS AT ONCE, on the first tick that sees it.
//   * A FAMILY THAT ONLY APPEARS beside one that is unchanged rides along: the
//     transports' working path did not move.
//   * A SAME-IDENTITY REJOIN KICKS: back from an offline gap on the very same
//     interface, next hop and address, but with the interface's carrier having
//     come up again in between (carrier_up_count), which is how two networks
//     that hand out the same addressing look. A carrier that comes back without
//     a gap is a roam inside one network and does not kick.
//   * A HOLDOFF (LinuxNetworkPathSettings) follows every kick: a change inside
//     it (a dual-stack join whose IPv6 route lands a second after its IPv4 one)
//     is re-evaluated each tick and fires once when the holdoff ends, if the
//     path still differs from the one kicked to. A flapping link costs one kick
//     per holdoff, never one per flap.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "TunnelPolicy.hpp"

namespace urnw {

// One family's way off the tunnel. Empty (`present()` false) when the family
// has no route at all.
struct LinuxFamilyPath {
  std::string interface_name;
  // The default route's next hop on that interface, as /proc prints it; "" for
  // an on-link default (a point-to-point modem link) or an unreadable table.
  std::string gateway;
  // The bound source address: verbatim for IPv4, its /64 for IPv6.
  std::string source;
  // The interface's carrier_up_count (carrier_changes on kernels before 4.16):
  // how many times its carrier has come up. 0 when neither is readable.
  std::uint64_t carrier_ups = 0;

  bool present() const { return !source.empty(); }
};

// The same way off the tunnel: the same interface, next hop and address. The
// carrier counter is not identity; it is how a rejoin to the same identity is
// told apart from a blip (see the file comment).
inline bool SameLinuxPathIdentity(const LinuxFamilyPath& a, const LinuxFamilyPath& b) {
  return a.interface_name == b.interface_name && a.gateway == b.gateway && a.source == b.source;
}

struct LinuxNetworkQualitySnapshot {
  LinuxFamilyPath v4;
  LinuxFamilyPath v6;
  // The interface the readings below describe: IPv4's when it has a path,
  // IPv6's otherwise, "" when offline.
  std::string interface_name;
  int carrier = -1;
  int wireless_signal_level = -1;
  std::uint64_t speed_bucket_mbps = 0;

  bool online() const { return v4.present() || v6.present(); }
};

enum class LinuxNetworkChange { None, Quality, Path };

// The tracker's timing, with its default here; the daemon builds its tracker
// from HostSettings (daemon/HostSettings.hpp).
struct LinuxNetworkPathSettings {
  // Milliseconds after a kick that a further change waits before it fires
  // (once, if the path still differs then). 2000: two reaper ticks, long enough
  // for a dual-stack join's second family to land, short against the dial a
  // kick costs. 0: no holdoff, every tick whose path differs from the last kick
  // kicks.
  std::int64_t holdoff_millis = 2000;
};

class LinuxNetworkQualityTracker {
 public:
  LinuxNetworkQualityTracker() = default;
  explicit LinuxNetworkQualityTracker(const LinuxNetworkPathSettings& settings)
      : settings_(settings) {}

  const LinuxNetworkPathSettings& settings() const { return settings_; }

  // The baseline a device's first dials used, taken right after the device is
  // created: the first kick it gets is for a path that differs from this one.
  void Rebase(const LinuxNetworkQualitySnapshot& snapshot) {
    initialized_ = true;
    kicked_ = snapshot;
    last_ = snapshot;
    sawOffline_ = !snapshot.online();
    forceKick_ = false;
    holdUntilMillis_ = std::numeric_limits<std::int64_t>::min();
  }

  // Kick on the next tick that has a path, whether or not it changed: the
  // daemon was suspended, and every connection the transports held may be gone
  // with nothing about the network to show for it.
  void ForceKickOnNextPath() { forceKick_ = true; }

  // Forget everything: the next observation is a baseline, as after Rebase.
  void Reset() {
    initialized_ = false;
    kicked_ = {};
    last_ = {};
    sawOffline_ = false;
    forceKick_ = false;
    holdUntilMillis_ = std::numeric_limits<std::int64_t>::min();
  }

  LinuxNetworkChange Observe(const LinuxNetworkQualitySnapshot& next, std::int64_t nowMillis) {
    if (!initialized_) {
      Rebase(next);
      return LinuxNetworkChange::None;
    }
    // Quality: the radio or link speed of the same interface. Never a kick.
    const bool quality = !next.interface_name.empty() &&
                         next.interface_name == last_.interface_name &&
                         (next.wireless_signal_level != last_.wireless_signal_level ||
                          next.speed_bucket_mbps != last_.speed_bucket_mbps);
    const LinuxNetworkChange quiet =
        quality ? LinuxNetworkChange::Quality : LinuxNetworkChange::None;
    last_ = next;
    if (!next.online()) {
      sawOffline_ = true;
      return quiet;
    }
    const bool rejoined =
        sawOffline_ && (Rejoined(kicked_.v4, next.v4) || Rejoined(kicked_.v6, next.v6));
    const bool due = forceKick_ || !kicked_.online() || Moved(kicked_.v4, next.v4) ||
                     Moved(kicked_.v6, next.v6) || rejoined;
    if (!due) {
      Absorb(&kicked_.v4, next.v4);
      Absorb(&kicked_.v6, next.v6);
      sawOffline_ = false;
      return quiet;
    }
    // Inside the holdoff: nothing is recorded, so the next tick asks again and
    // the change fires once the holdoff ends, if it still stands.
    if (nowMillis < holdUntilMillis_) return quiet;
    kicked_ = next;
    sawOffline_ = false;
    forceKick_ = false;
    holdUntilMillis_ = nowMillis + settings_.holdoff_millis;
    return LinuxNetworkChange::Path;
  }

  // The path the transports were last kicked onto (or the device's baseline).
  const LinuxNetworkQualitySnapshot& kicked() const { return kicked_; }

  // The last path observed (or rebased on) had no usable route in any family:
  // what the dead-tunnel failsafe holds its no-exit rules on (TunnelWatchdog.hpp).
  // False before the first reading, so an unknown network never reads offline.
  bool offline() const { return initialized_ && !last_.online(); }

 private:
  // A family the transports could be using went away or now leads elsewhere.
  static bool Moved(const LinuxFamilyPath& kicked, const LinuxFamilyPath& next) {
    return kicked.present() && (!next.present() || !SameLinuxPathIdentity(kicked, next));
  }

  // Back on the very same identity with the carrier having come up again.
  static bool Rejoined(const LinuxFamilyPath& kicked, const LinuxFamilyPath& next) {
    return kicked.present() && next.present() && SameLinuxPathIdentity(kicked, next) &&
           next.carrier_ups > kicked.carrier_ups;
  }

  // What a quiet tick folds into the baseline: a family that appeared beside
  // an unchanged one, and the carrier counter of an unchanged family (a roam),
  // so a later gap is measured from now.
  static void Absorb(LinuxFamilyPath* kicked, const LinuxFamilyPath& next) {
    if (!kicked->present()) {
      *kicked = next;
    } else if (SameLinuxPathIdentity(*kicked, next)) {
      kicked->carrier_ups = next.carrier_ups;
    }
  }

  LinuxNetworkPathSettings settings_;
  bool initialized_ = false;
  LinuxNetworkQualitySnapshot kicked_;
  LinuxNetworkQualitySnapshot last_;
  bool sawOffline_ = false;
  bool forceKick_ = false;
  std::int64_t holdUntilMillis_ = std::numeric_limits<std::int64_t>::min();
};

// How long this machine has been suspended, from two readings of
// CLOCK_BOOTTIME minus CLOCK_MONOTONIC: the first clock runs through a
// suspend and the second does not, so their difference grows by exactly the
// time spent asleep. A main loop that was merely stalled (a long nft run, a
// bring-up holding the lock) moves both clocks together and reads as nothing.
struct LinuxSuspendSettings {
  // Milliseconds of sleep between two readings that count as a resume. 1000:
  // above clock-read jitter, below any suspend worth a kick. 0 turns the
  // detector off (a zero bar would read every tick as a resume and kick on
  // each one).
  std::int64_t threshold_millis = 1000;
};

class LinuxSuspendDetector {
 public:
  LinuxSuspendDetector() = default;
  explicit LinuxSuspendDetector(const LinuxSuspendSettings& settings) : settings_(settings) {}

  // `suspendedMillis`: CLOCK_BOOTTIME - CLOCK_MONOTONIC now. True when it grew
  // by the settings' threshold or more since the previous reading (never with a
  // threshold of 0).
  bool Observe(std::int64_t suspendedMillis) {
    if (!initialized_) {
      initialized_ = true;
      last_ = suspendedMillis;
      return false;
    }
    const bool resumed = settings_.threshold_millis > 0 &&
                         suspendedMillis - last_ >= settings_.threshold_millis;
    last_ = suspendedMillis;
    return resumed;
  }

 private:
  LinuxSuspendSettings settings_;
  bool initialized_ = false;
  std::int64_t last_ = 0;
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

// The next hop of `interfaceName`'s lowest-metric IPv4 default route in
// /proc/net/route (main table), as printed there ("0101A8C0"); "" without
// one, or for an on-link default (gateway 00000000). Equal metrics take the
// smallest next hop, so the answer cannot flip with the table's order.
inline std::string LinuxDefaultRouteGateway(const std::string& routes,
                                            const std::string& interfaceName) {
  std::istringstream lines(routes);
  std::string line;
  std::string best;
  bool found = false;
  std::uint64_t bestMetric = std::numeric_limits<std::uint64_t>::max();
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string iface, destination, gateway, flagsText;
    std::uint64_t refCount = 0, use = 0, metric = 0;
    if (!(fields >> iface >> destination >> gateway >> flagsText >> refCount >> use >> metric))
      continue;
    std::uint64_t flags = 0;
    std::istringstream(flagsText) >> std::hex >> flags;
    if (iface != interfaceName || destination != "00000000" || (flags & 1) == 0) continue;
    if (!found || metric < bestMetric || (metric == bestMetric && gateway < best)) {
      found = true;
      best = gateway;
      bestMetric = metric;
    }
  }
  return found && best != "00000000" ? best : std::string();
}

// The IPv6 twin, over /proc/net/ipv6_route: the next hop of `interfaceName`'s
// lowest-metric default route that is up and not a reject route; "" without
// one, or for an on-link default (next hop all zeros). That file lists every
// routing table; the interface filter keeps the tun's capture table out.
inline std::string LinuxDefaultRouteGatewayV6(const std::string& routes,
                                              const std::string& interfaceName) {
  constexpr std::uint64_t kRejectFlag = 0x0200;  // RTF_REJECT: unreachable/prohibit
  constexpr std::uint64_t kUpFlag = 0x0001;      // RTF_UP
  std::istringstream lines(routes);
  std::string line;
  std::string best;
  bool found = false;
  std::uint64_t bestMetric = std::numeric_limits<std::uint64_t>::max();
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string destination, destinationLength, source, sourceLength, nextHop, metricText,
        refCount, use, flagsText, iface;
    if (!(fields >> destination >> destinationLength >> source >> sourceLength >> nextHop >>
          metricText >> refCount >> use >> flagsText >> iface)) {
      continue;
    }
    if (iface != interfaceName) continue;
    if (destination != std::string(32, '0') || destinationLength != "00") continue;
    std::uint64_t flags = 0;
    std::uint64_t metric = 0;
    std::istringstream(flagsText) >> std::hex >> flags;
    std::istringstream(metricText) >> std::hex >> metric;
    if ((flags & kUpFlag) == 0 || (flags & kRejectFlag) != 0) continue;
    if (!found || metric < bestMetric || (metric == bestMetric && nextHop < best)) {
      found = true;
      best = nextHop;
      bestMetric = metric;
    }
  }
  return found && best != std::string(32, '0') ? best : std::string();
}

// An IPv6 address's /64 as one stable string ("2001:db8:1:2::/64"; a zero
// hextet stays "0" -- it is an identity to compare, never shown), "" when
// `address` is not an IPv6 literal. A temporary (privacy) address rotates
// inside its /64; the network it belongs to does not.
inline std::string LinuxIpv6Prefix64(const std::string& address) {
  Ipv6Bytes bytes;
  if (!ParseIpv6Literal(address, &bytes)) return std::string();
  static constexpr char kHex[] = "0123456789abcdef";
  std::string text;
  for (int group = 0; group < 4; ++group) {
    const unsigned value =
        (static_cast<unsigned>(bytes.b[2 * group]) << 8) | bytes.b[2 * group + 1];
    std::string hextet;
    for (int shift = 12; shift >= 0; shift -= 4) {
      const unsigned digit = (value >> shift) & 0xfu;
      if (hextet.empty() && digit == 0 && shift != 0) continue;
      hextet += kHex[digit];
    }
    text += hextet;
    text += ':';
  }
  return text + ":/64";
}

// A bound source that can leave this machine at all: never a link-local
// address (IPv6 fe80:: while SLAAC is still running, IPv4 169.254 from
// link-local autoconfiguration) and never loopback. Either reads as "no path
// yet", so a dual-stack join kicks for the address the transports can use,
// not for the placeholder before it.
inline bool LinuxSourceLeavesTheLink(const std::string& source) {
  Ipv6Bytes bytes;
  if (ParseIpv6Literal(source, &bytes)) {
    if (bytes.b[0] == 0xfe && (bytes.b[1] & 0xc0) == 0x80) return false;  // fe80::/10
    bool loopback = bytes.b[15] == 1;
    for (int i = 0; i < 15 && loopback; ++i) loopback = bytes.b[i] == 0;
    return !loopback;
  }
  if (!IsIpv4Literal(source)) return false;
  return source.rfind("169.254.", 0) != 0 && source.rfind("127.", 0) != 0;
}

// What a path change is logged as: the interfaces, never an address.
inline std::string DescribeLinuxNetworkPath(const LinuxNetworkQualitySnapshot& snapshot) {
  const auto family = [](const char* name, const LinuxFamilyPath& path) {
    return std::string(name) + " " + (path.present() ? path.interface_name : std::string("none"));
  };
  return family("v4", snapshot.v4) + ", " + family("v6", snapshot.v6);
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
