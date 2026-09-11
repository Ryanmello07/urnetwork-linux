// Pure tunnel-interface policy shared by the Linux daemon and its unit tests.
//
// The tunnel is DUAL-STACK (connect/IPV6.md C2): remote providers carry v4
// and v6, so the platform tunnel advertises one address of each family, a
// capture route set for each family, and resolvers of each family. The
// predicate below is the fail-closed floor Tunnel::Open enforces BEFORE any
// device exists: a configuration missing either half is refused outright
// rather than brought up as a silent split tunnel (v6 leaving in the clear
// while the UI says Connected is exactly the leak the v6 half exists to stop).
//
// Everything here is plain C++17 with no syscalls, so the address checks and
// the capture set are testable on any developer machine (tests/TunnelPolicyTest.cpp).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace urnw {

// Mirrors sdk.GetDefaultTunnelMtu / connect.DefaultTunnelMtu: the INTERFACE
// MTU. It is the IPv6 minimum link MTU, because Linux disables IPv6 on an
// interface whose MTU is below 1280, and the v6 half of the tunnel needs the
// link to exist. The packet-size contract is separate and smaller
// (connect.DefaultMtu = 1100, the largest packet a provider builds), so one
// full encrypted tunnel packet plus the UR envelope still fits one initial H3
// QUIC DATAGRAM.
inline constexpr int kIpv6MinimumMtu = 1280;
inline constexpr int kTunnelMtu = kIpv6MinimumMtu;

struct TunnelConfig {
  std::string name = "urnet0";
  std::string local_addr_v4 = "169.254.2.1";
  int prefix_v4 = 24;
  // The v6 half. The device draws the address from the SDK's fixed ULA /48
  // (sdk tunnel_address_ipv6.go); TunnelHost substitutes a fixed ULA when the
  // device reports an unusable one, exactly as it does for the v4 address.
  std::string local_addr_v6 = "fd00:7572:6e65:ffff::2";
  int prefix_v6 = 64;
  int mtu = kTunnelMtu;
  std::vector<std::string> dns_servers_v4;
  std::vector<std::string> dns_servers_v6;
  // FORK ADDITION (not upstream). Refuse to install the capture routes unless
  // the daemon's own sockets are demonstrably steered around them. Only a dev
  // run (URNETWORK_ALLOW_UNPROTECTED_EGRESS=1) may clear it, and clearing it is
  // logged loudly, because the alternative is a tunnel that comes up and
  // carries nothing while the UI says Connected. IsDualStackTunnelConfig
  // deliberately ignores this field.
  bool require_egress_protection = true;
};

// Whether nftables may use its socket-cgroup expression for the daemon's
// egress exemption. Some otherwise-capable kernels (notably Docker Desktop's
// LinuxKit kernel) ship cgroup BPF but omit CONFIG_NFT_SOCKET. In that case a
// proven socket-creation mark is sufficient for a floorless tunnel, but it is
// not sufficient for a crash-safe kill-switch floor or for a DNS helper that
// lives in another cgroup.
enum class NftCgroupMode {
  CgroupAndMark,
  MarkOnly,
  Refuse,
};

// Pure policy boundary for the runtime kernel probe. Refusal is intentional:
// omitting an unavailable expression must never silently weaken the states
// whose safety depends on matching another cgroup or surviving daemon death.
constexpr NftCgroupMode SelectNftCgroupMode(bool socketMarkerProven,
                                            bool cgroupSocketMatchSupported,
                                            bool blockFloor,
                                            bool helperDnsRequired) {
  if (cgroupSocketMatchSupported) return NftCgroupMode::CgroupAndMark;
  if (!socketMarkerProven || blockFloor || helperDnsRequired) {
    return NftCgroupMode::Refuse;
  }
  return NftCgroupMode::MarkOnly;
}

constexpr bool IsIpv4Literal(std::string_view address) {
  if (address.empty()) return false;

  int octets = 0;
  int digits = 0;
  int value = 0;
  for (const char c : address) {
    if (c == '.') {
      if (digits == 0 || value > 255 || octets == 3) return false;
      ++octets;
      digits = 0;
      value = 0;
      continue;
    }
    if (c < '0' || c > '9' || digits == 3) return false;
    value = value * 10 + (c - '0');
    ++digits;
  }
  return octets == 3 && digits != 0 && value <= 255;
}

// ---- IPv6 literals ---------------------------------------------------------

// A parsed IPv6 address: the 16 network-order bytes.
struct Ipv6Bytes {
  unsigned char b[16] = {};
};

namespace tunnelpolicy_detail {

constexpr int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// One hextet ("0" .. "ffff") into its value; -1 when it is not one.
constexpr int ParseHextet(std::string_view group) {
  if (group.empty() || group.size() > 4) return -1;
  int value = 0;
  for (const char c : group) {
    const int digit = HexValue(c);
    if (digit < 0) return -1;
    value = value * 16 + digit;
  }
  return value;
}

// A dotted-quad tail ("1.2.3.4") into two hextets. false when it is not one.
constexpr bool ParseEmbeddedIpv4(std::string_view tail, int* high, int* low) {
  if (!IsIpv4Literal(tail)) return false;
  int octets[4] = {0, 0, 0, 0};
  int index = 0;
  int value = 0;
  for (const char c : tail) {
    if (c == '.') {
      octets[index++] = value;
      value = 0;
      continue;
    }
    value = value * 10 + (c - '0');
  }
  octets[index] = value;
  *high = octets[0] * 256 + octets[1];
  *low = octets[2] * 256 + octets[3];
  return true;
}

}  // namespace tunnelpolicy_detail

// Parses a textual IPv6 address (RFC 4291 §2.2: hextets, one "::", an
// optional embedded dotted-quad tail). No brackets, no zone id, no prefix
// length: a value carrying any of those is not an address and is refused.
// Returns false without touching *out when the text is not an address.
constexpr bool ParseIpv6Literal(std::string_view address, Ipv6Bytes* out) {
  using namespace tunnelpolicy_detail;
  if (address.empty() || address.size() > 45) return false;

  int head[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // hextets before "::"
  int tail[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // hextets after "::"
  int headCount = 0;
  int tailCount = 0;
  bool compressed = false;

  size_t pos = 0;
  // a leading "::" (a leading ':' that is not "::" is malformed)
  if (address[0] == ':') {
    if (address.size() < 2 || address[1] != ':') return false;
    compressed = true;
    pos = 2;
    if (pos == address.size()) {
      // "::" alone: all zeros
      *out = Ipv6Bytes{};
      return true;
    }
  }

  while (pos < address.size()) {
    // the next group runs to the next ':' or the end
    size_t end = address.find(':', pos);
    if (end == std::string_view::npos) end = address.size();
    const std::string_view group = address.substr(pos, end - pos);
    if (group.empty()) return false;  // ":::" or a trailing single ':'

    int* target = compressed ? tail : head;
    int* count = compressed ? &tailCount : &headCount;

    if (group.find('.') != std::string_view::npos) {
      // an embedded dotted quad is only legal as the LAST group
      if (end != address.size()) return false;
      int high = 0;
      int low = 0;
      if (!ParseEmbeddedIpv4(group, &high, &low)) return false;
      if (headCount + tailCount + 2 > 8) return false;
      target[(*count)++] = high;
      target[(*count)++] = low;
      pos = end;
      break;
    }

    const int value = ParseHextet(group);
    if (value < 0) return false;
    if (headCount + tailCount + 1 > 8) return false;
    target[(*count)++] = value;

    if (end == address.size()) {
      pos = end;
      break;
    }
    // at a ':': either a single separator or the "::"
    pos = end + 1;
    if (pos < address.size() && address[pos] == ':') {
      if (compressed) return false;  // a second "::"
      compressed = true;
      ++pos;
      if (pos == address.size()) break;  // a trailing "::"
    } else if (pos == address.size()) {
      return false;  // a trailing single ':'
    }
  }

  const int total = headCount + tailCount;
  if (compressed) {
    // "::" stands for at least one zero group
    if (total > 7) return false;
  } else if (total != 8) {
    return false;
  }

  int groups[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < headCount; ++i) groups[i] = head[i];
  for (int i = 0; i < tailCount; ++i) groups[8 - tailCount + i] = tail[i];
  for (int i = 0; i < 8; ++i) {
    out->b[2 * i] = static_cast<unsigned char>(groups[i] >> 8);
    out->b[2 * i + 1] = static_cast<unsigned char>(groups[i] & 0xff);
  }
  return true;
}

constexpr bool IsIpv6Literal(std::string_view address) {
  Ipv6Bytes ignored;
  return ParseIpv6Literal(address, &ignored);
}

// Whether `address` lies inside `prefix` ("2001:db8::/32"). false when either
// is malformed, or when the prefix length is outside 0..128.
constexpr bool Ipv6PrefixContains(std::string_view prefix, std::string_view address) {
  const size_t slash = prefix.find('/');
  if (slash == std::string_view::npos) return false;
  int length = 0;
  const std::string_view lengthText = prefix.substr(slash + 1);
  if (lengthText.empty() || lengthText.size() > 3) return false;
  for (const char c : lengthText) {
    if (c < '0' || c > '9') return false;
    length = length * 10 + (c - '0');
  }
  if (length > 128) return false;
  Ipv6Bytes network;
  Ipv6Bytes candidate;
  if (!ParseIpv6Literal(prefix.substr(0, slash), &network)) return false;
  if (!ParseIpv6Literal(address, &candidate)) return false;
  int remaining = length;
  for (int i = 0; i < 16 && remaining > 0; ++i) {
    const int bits = remaining >= 8 ? 8 : remaining;
    const unsigned char mask = static_cast<unsigned char>(0xff << (8 - bits));
    if ((network.b[i] & mask) != (candidate.b[i] & mask)) return false;
    remaining -= bits;
  }
  return true;
}

// Whether `address` is a unique local address (fc00::/7), which is what the
// tunnel's own v6 address must be: private, so nothing on the internet can be
// confused with it, and outside the capture set, so the tun's own /64 never
// competes with a capture route.
constexpr bool IsIpv6UniqueLocal(std::string_view address) {
  return Ipv6PrefixContains("fc00::/7", address);
}

// ---- the dual-stack configuration floor ------------------------------------

// The v4 half: a dotted-quad address with a prefix that cannot swallow the
// default route (/0 on the tun means a CONNECTED route covering the whole
// space, captured without any of the policy routing that is supposed to decide
// it), and every v4 resolver a dotted quad.
constexpr bool IsIpv4HalfValid(const TunnelConfig& config) {
  if (!IsIpv4Literal(config.local_addr_v4) || config.prefix_v4 < 1 || config.prefix_v4 > 32) {
    return false;
  }
  for (const auto& server : config.dns_servers_v4) {
    if (!IsIpv4Literal(server)) return false;
  }
  return true;
}

// The v6 half: a ULA address (never "::", never multicast, never a global
// address the tun would then claim) with a prefix of at least /1 for the same
// reason as v4, and every v6 resolver a v6 literal. An empty resolver list is
// legal for either half: a tunnel without resolvers of that family is a tunnel
// whose DNS the other family carries, which the nft DNS floor and
// status.dns_detail both describe.
constexpr bool IsIpv6HalfValid(const TunnelConfig& config) {
  if (!IsIpv6UniqueLocal(config.local_addr_v6) || config.prefix_v6 < 1 ||
      config.prefix_v6 > 128) {
    return false;
  }
  for (const auto& server : config.dns_servers_v6) {
    if (!IsIpv6Literal(server)) return false;
  }
  return true;
}

// THE FLOOR. Both halves, or nothing: a configuration that can only carry one
// family is refused before the device is opened, so the tunnel never comes up
// as a split tunnel by accident.
inline bool IsDualStackTunnelConfig(const TunnelConfig& config) {
  return IsIpv4HalfValid(config) && IsIpv6HalfValid(config);
}

// ---- the v6 capture set ----------------------------------------------------

// The v6 split-default capture set: the whole IPv6 space MINUS the three
// prefixes a tunnel cannot carry — link-local fe80::/10 (NDP, RS/RA, the link
// itself), ULA fc00::/7 (the LAN analogue of RFC1918, and the family the tun's
// own address lives in) and multicast ff00::/8 (MLD, solicited-node DAD; a tun
// has no link to multicast on). The same shape Android's excludeRoute set,
// iOS's NEIPv6Settings.excludedRoutes and the Windows NetworkConfig use.
//
// Loopback ::1 is NOT excluded by route, and does not need to be: the kernel's
// `local` table (rule pref 0) claims every local address before the capture
// rule at pref 32763 is consulted — exactly why the v4 set can carry
// 127.0.0.0/8 inside 64.0.0.0/2 without capturing loopback.
inline const std::vector<std::string>& CaptureV6Prefixes() {
  static const std::vector<std::string> kPrefixes = {
      "::/1",     "8000::/2",  "c000::/3",  "e000::/4",
      "f000::/5", "f800::/6",  "fe00::/9",  "fec0::/10",
  };
  return kPrefixes;
}

// The three prefixes CaptureV6Prefixes deliberately omits, as one list, so the
// routes and the firewall's v6 permits are built from ONE table.
inline const std::vector<std::string>& ExcludedV6Prefixes() {
  static const std::vector<std::string> kPrefixes = {"fe80::/10", "fc00::/7", "ff00::/8"};
  return kPrefixes;
}

// Whether the capture set routes `address` into the tunnel: inside some
// capture prefix and inside no excluded one. Pure, for the tests.
constexpr bool CaptureV6Claims(std::string_view address) {
  constexpr std::string_view kExcluded[] = {"fe80::/10", "fc00::/7", "ff00::/8"};
  for (const auto excluded : kExcluded) {
    if (Ipv6PrefixContains(excluded, address)) return false;
  }
  constexpr std::string_view kCapture[] = {"::/1",     "8000::/2", "c000::/3", "e000::/4",
                                           "f000::/5", "f800::/6", "fe00::/9", "fec0::/10"};
  for (const auto prefix : kCapture) {
    if (Ipv6PrefixContains(prefix, address)) return true;
  }
  return false;
}

}  // namespace urnw
