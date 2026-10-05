// The daemon's support diagnostics, as the lines it writes through
// urnet::logAppInfo (daemon/SupportDiagnostics.cpp). That is the sdk's glog,
// which is what "send feedback with logs" uploads (DeviceLocal.UploadLogs zips
// the daemon's glog files); the journal and the log ring that DaemonLogf feeds
// are not in that upload. Android writes its Private DNS mode and whitelist
// probe the same way (Sdk.logAppInfo).
//
// What a line may carry. Only what a support reply needs and nothing that
// identifies a person or a network: modes, states and counts as fixed tokens,
// a two-letter country, and interface names with any MAC address taken out.
// Never an address (a resolver, a gateway, a tunnel peer), a uid, a path, an
// operator code or name, an id or a credential. Every value a host supplies
// passes through a filter here, so a value nobody anticipated becomes "other"
// rather than text in the log. The sdk then bounds and sanitizes the
// line again (at most 1024 bytes, control characters turned into spaces).
//
// Pure, so the unit suite checks every line and filter.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>

#include "NetworkCountry.hpp"
#include "TunnelPolicy.hpp"

namespace urnw {
namespace diag {

// The tags (the sdk keeps [A-Za-z0-9._-], at most 32): "[app][<tag>] <line>".
inline constexpr const char* kTagDns = "dns";
inline constexpr const char* kTagDnsLink = "dns-link";
inline constexpr const char* kTagKillSwitch = "kill-switch";
inline constexpr const char* kTagSplitTunnel = "split-tunnel";
inline constexpr const char* kTagNetworkCountry = "network-country";
inline constexpr const char* kTagConnectivity = "connectivity";
inline constexpr const char* kTagTunnel = "tunnel";

// ---- filters ---------------------------------------------------------------

// A word as a token: lower case [a-z0-9_-], 1 to 32 of them, a space becoming
// '-' (the daemon's own reasons are phrases: "dns override lost"). Anything
// else (empty, too long, another character) is "other", so neither an address
// (a '.' or a ':') nor a path (a '/') can pass.
inline std::string Token(const std::string& value) {
  if (value.empty() || value.size() > 32) return "other";
  std::string token;
  for (const char c : value) {
    const char lower = ('A' <= c && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    if (('a' <= lower && lower <= 'z') || ('0' <= lower && lower <= '9') || lower == '_' ||
        lower == '-') {
      token += lower;
    } else if (lower == ' ') {
      token += '-';
    } else {
      return "other";
    }
  }
  return token;
}

inline const char* YesNo(bool value) { return value ? "yes" : "no"; }

// A yes/no setting a host printed: itself, "default" when unset, else "other".
inline std::string YesNoSetting(const std::string& value) {
  if (value.empty()) return "default";
  if (value == "yes" || value == "no") return value;
  return "other";
}

// An interface name fit for the log. A kernel name is at most 15 bytes; one
// that is longer or carries anything but [A-Za-z0-9._@-] is "other". systemd's
// MAC naming policy builds a name from the hardware address (enx…, wlx…: two
// letters, 'x' and twelve hex digits), which identifies the machine, so such a
// name keeps its prefix only: "wlx[mac]".
inline std::string SafeInterfaceName(const std::string& name) {
  if (name.empty()) return "none";
  if (name.size() > 15) return "other";
  for (const char c : name) {
    const bool ok = ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') ||
                    c == '.' || c == '_' || c == '@' || c == '-';
    if (!ok) return "other";
  }
  if (name.size() == 15 && name[2] == 'x') {
    bool mac = true;
    for (size_t i = 3; i < name.size(); ++i) {
      const char c = name[i];
      mac = mac && (('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F'));
    }
    if (mac) return name.substr(0, 3) + "[mac]";
  }
  return name;
}

// A systemd-resolved DNSOverTLS or DNSSEC setting as resolvectl prints it:
// the setting itself, "default" where none is set (the link follows the global
// one), and "other" for a value this build does not know.
inline std::string ResolvedMode(const std::string& value) {
  if (value.empty() || value == "n/a") return "default";
  for (const char* known : {"yes", "no", "opportunistic", "allow-downgrade"}) {
    if (value == known) return value;
  }
  return "other";
}

// ---- systemd-resolved, read with resolvectl --------------------------------

// What one `resolvectl <verb>` printed for all links: `dnsovertls`,
// `dnssec`, `dns` and `default-route` each print "Global: <value>" (where the
// verb has a global value) and one "Link <index> (<name>): <value>" per link.
struct ResolvectlListing {
  bool has_global = false;
  std::string global;
  std::map<std::string, std::string> links;  // link name -> value, trimmed
};

inline std::string TrimSpaces(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos) return std::string();
  const size_t last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}

inline ResolvectlListing ParseResolvectlListing(const std::string& output) {
  ResolvectlListing listing;
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.compare(0, 7, "Global:") == 0) {
      listing.has_global = true;
      listing.global = TrimSpaces(line.substr(7));
      continue;
    }
    if (line.compare(0, 5, "Link ") != 0) continue;
    const size_t open = line.find(" (");
    const size_t close = line.find("):", open == std::string::npos ? 0 : open);
    if (open == std::string::npos || close == std::string::npos) continue;
    listing.links[line.substr(open + 2, close - open - 2)] = TrimSpaces(line.substr(close + 2));
  }
  return listing;
}

// How many servers a `resolvectl dns` value lists (space separated).
inline int CountWords(const std::string& value) {
  std::istringstream words(value);
  std::string word;
  int count = 0;
  while (words >> word) ++count;
  return count;
}

// The DNS host: which tier the tunnel takes (the preflight's rule), whether
// systemd-resolved runs and is consulted, and its global DNS-over-TLS and
// DNSSEC modes. A strict mode (yes) on a link the tunnel does not control can
// make names fail while connected, the Linux face of Android's Private DNS
// conflict.
struct DnsHostFacts {
  bool resolved_running = false;
  bool resolvectl_present = false;
  bool nss_resolve_before_dns = false;
  bool resolv_conf_points_at_resolved = false;
  bool resolvconf_present = false;
  bool resolvconf_is_resolvectl = false;
  bool resolved_read = false;  // the two modes below were read
  std::string dns_over_tls;    // resolvectl dnsovertls, global
  std::string dnssec;          // resolvectl dnssec, global
};

inline const char* DnsTier(const DnsHostFacts& facts) {
  if (facts.resolved_running && facts.resolvectl_present) return "systemd-resolved";
  if (facts.resolvconf_present && !facts.resolvconf_is_resolvectl) return "resolvconf";
  return "direct-file";
}

inline std::string DnsHostLine(const char* moment, const DnsHostFacts& facts) {
  std::string line = std::string("at=") + moment + " tier=" + DnsTier(facts) +
                     " resolved=" + (facts.resolved_running ? "running" : "stopped") +
                     " nss-resolve-first=" + YesNo(facts.nss_resolve_before_dns) +
                     " resolv.conf-stub=" + YesNo(facts.resolv_conf_points_at_resolved);
  if (facts.resolved_read) {
    line += " dot=" + ResolvedMode(facts.dns_over_tls) + " dnssec=" + ResolvedMode(facts.dnssec);
  }
  return line;
}

// One link's DNS as systemd-resolved holds it: the tunnel's, or the link the
// default route leaves through. Counts, never the servers themselves.
struct DnsLinkFacts {
  std::string role;  // "tunnel" or "default-route"
  std::string name;
  bool known = false;  // resolved lists the link
  int servers = 0;
  std::string dns_over_tls;
  std::string dnssec;
  std::string default_route;  // resolvectl default-route: "yes", "no" or ""
};

inline DnsLinkFacts DnsLinkFrom(const std::string& role, const std::string& name,
                                const ResolvectlListing& dns, const ResolvectlListing& dot,
                                const ResolvectlListing& dnssec,
                                const ResolvectlListing& defaultRoute) {
  DnsLinkFacts facts;
  facts.role = role;
  facts.name = name;
  const auto servers = dns.links.find(name);
  facts.known = servers != dns.links.end() || dot.links.count(name) != 0;
  if (servers != dns.links.end()) facts.servers = CountWords(servers->second);
  if (const auto it = dot.links.find(name); it != dot.links.end()) facts.dns_over_tls = it->second;
  if (const auto it = dnssec.links.find(name); it != dnssec.links.end()) facts.dnssec = it->second;
  if (const auto it = defaultRoute.links.find(name); it != defaultRoute.links.end()) {
    facts.default_route = it->second;
  }
  return facts;
}

inline std::string DnsLinkLine(const DnsLinkFacts& facts) {
  std::string line = "role=" + Token(facts.role) + " link=" + SafeInterfaceName(facts.name);
  if (!facts.known) return line + " resolved=unlisted";
  return line + " servers=" + std::to_string(facts.servers) +
         " dot=" + ResolvedMode(facts.dns_over_tls) + " dnssec=" + ResolvedMode(facts.dnssec) +
         " default-route=" + YesNoSetting(facts.default_route);
}

// A DNS override event mid-session: lost and re-applied (or not), or a
// systemd-resolved restart.
inline std::string DnsOverrideLine(const char* event, bool applied, const char* backend) {
  return std::string("override=") + event + " applied=" + YesNo(applied) +
         " backend=" + Token(backend);
}

// ---- the kill switch and the nftables table --------------------------------

struct KillSwitchFacts {
  bool requested = false;     // the user's toggle
  std::string state;          // the table in force: off, connecting, armed, connected
  std::string published;      // what status says: off, armed, connected, failed
  bool floor = false;         // the block-everything floor is in force
  bool ipv6_blocked = false;  // off-tunnel IPv6 is refused
  bool dns_floor = false;     // off-tunnel :53 is closed
  bool exclusion = false;     // a per-app exclusion (urnetwork-exclude) is in force
  bool applied = true;        // the last nft transaction took
};

inline std::string KillSwitchLine(const KillSwitchFacts& facts) {
  return std::string("requested=") + YesNo(facts.requested) + " table=" + Token(facts.state) +
         " status=" + Token(facts.published) + " floor=" + YesNo(facts.floor) +
         " ipv6-blocked=" + YesNo(facts.ipv6_blocked) + " dns-floor=" + YesNo(facts.dns_floor) +
         " exclusion=" + YesNo(facts.exclusion) + " nft=" + (facts.applied ? "ok" : "failed");
}

// At daemon start: nft on the host, and a floor carried over from a daemon
// that died while armed (the machine is blocked until it is lifted).
inline std::string KillSwitchStartLine(bool nftPresent, bool armedFloorCarriedOver) {
  return std::string("at=start nft=") + (nftPresent ? "present" : "missing") +
         " floor=" + (armedFloorCarriedOver ? "carried-over" : "none");
}

// ---- split tunnel (urnetwork-exclude) ---------------------------------------

// The cgroup v2 hierarchy of /proc/self/cgroup, which per-app exclusion needs
// alone (IsCgroupV2Only, the daemon's own test): "unified" (only the 0:: line),
// "hybrid" (v1 controllers beside it), "none" (no 0:: line).
inline std::string SplitTunnelLine(const std::string& procSelfCgroup) {
  const bool only = IsCgroupV2Only(procSelfCgroup);
  const bool found = procSelfCgroup.compare(0, 3, "0::") == 0 ||
                     procSelfCgroup.find("\n0::") != std::string::npos;
  const char* hierarchy = only ? "unified" : (found ? "hybrid" : "none");
  return std::string("cgroup-v2=") + hierarchy +
         " exclusion=" + (only ? "available" : "unavailable");
}

// ---- the network country (NetworkCountry.hpp) --------------------------------

inline std::string NetworkCountryLine(const NetworkCountryReading& reading) {
  return "country=" + (reading.country_code.empty() ? std::string("none")
                                                    : Token(reading.country_code)) +
         " source=" + ToString(reading.source) +
         " interface=" + SafeInterfaceName(reading.interface_name);
}

// ---- NetworkManager connectivity -------------------------------------------

// NMConnectivityState. "unknown" is also what a disabled connectivity check
// reports.
inline const char* NetworkManagerConnectivityName(uint32_t state) {
  switch (state) {
    case 0: return "unknown";
    case 1: return "none";
    case 2: return "portal";
    case 3: return "limited";
    case 4: return "full";
  }
  return "other";
}

// `running`: NetworkManager owns its bus name. `state` is meaningful only when
// it answered.
inline std::string ConnectivityLine(const char* moment, bool running, bool answered,
                                    uint32_t state) {
  const char* value =
      !running ? "absent" : (answered ? NetworkManagerConnectivityName(state) : "unreadable");
  return std::string("at=") + moment + " networkmanager=" + value;
}

// ---- the tunnel --------------------------------------------------------------

struct TunnelUpFacts {
  std::string dns_backend;  // the tier holding DNS: systemd-resolved, resolvconf, direct-file, none
  bool dns_applied = false;
  bool ipv6_captured = false;
  bool egress_protected = false;
  bool socket_marker = false;  // the cgroup-BPF socket marker carried the exclusion
};

inline std::string TunnelUpLine(const TunnelUpFacts& facts) {
  return "up dns=" + Token(facts.dns_backend) + " dns-applied=" + YesNo(facts.dns_applied) +
         " ipv6=" + (facts.ipv6_captured ? "captured" : "host-disabled") +
         " egress-protected=" + YesNo(facts.egress_protected) +
         " egress=" + (facts.socket_marker ? "socket-marker" : "nft-belt");
}

// A failed start or a session that ended without being asked to: the
// machine-readable code (ctl::kCode*) and the stop reason, never the prose.
inline std::string TunnelEndedLine(const char* what, const std::string& reason,
                                   const std::string& code) {
  return std::string(what) + " reason=" + (reason.empty() ? std::string("none") : Token(reason)) +
         " code=" + (code.empty() ? std::string("none") : Token(code));
}

// ---- the repeat gate ---------------------------------------------------------

// One line per key (a tag, or a tag and a link role) is remembered: a line
// that differs from the key's last one is written, the same line again only
// after `repeatMillis`. A reconnect loop or a flapping link then costs a line
// per change, not a line per attempt.
class LineGate {
 public:
  explicit LineGate(int64_t repeatMillis) : repeatMillis_(repeatMillis) {}

  bool Admit(const std::string& key, const std::string& line, int64_t nowMillis) {
    auto it = last_.find(key);
    if (it != last_.end() && it->second.line == line &&
        nowMillis - it->second.atMillis < repeatMillis_) {
      return false;
    }
    last_[key] = Last{line, nowMillis};
    return true;
  }

 private:
  struct Last {
    std::string line;
    int64_t atMillis = 0;
  };
  int64_t repeatMillis_;
  std::map<std::string, Last> last_;
};

}  // namespace diag
}  // namespace urnw
