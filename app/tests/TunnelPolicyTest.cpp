// The dual-stack tunnel policy, and THE FACT THAT SOMETHING CALLS IT.
//
// The predicate below was once restored from upstream with no caller: the file
// was present and the fix was not. That is the same shape as NetFilter::Verify,
// Tunnel::VerifyDnsStillApplied, Health.hpp and the egress resolver whose gate
// froze at handout -- four shipped defects where the logic existed and nothing
// ran it. So the last test here does not test the predicate at all; it reads
// Tunnel.cpp and fails if Tunnel::Open stops asking.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "TunnelPolicy.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

urnw::TunnelConfig DualStack() {
  urnw::TunnelConfig config;
  config.local_addr_v4 = "169.254.2.1";
  config.prefix_v4 = 24;
  config.dns_servers_v4 = {"65.49.70.65", "9.9.9.9"};
  config.local_addr_v6 = "fd00:7572:6e65:1a2b:3c4d:5e6f:7081:92a3";
  config.prefix_v6 = 64;
  config.dns_servers_v6 = {"2001:db8::65:49:70:65"};
  return config;
}

// Keep the genuinely compile-time half of TunnelPolicy on the C++17 boundary.
// The TunnelConfig validators below consume owning strings and vectors at
// runtime; compiling this file with the Ubuntu 22.04 GCC 11 toolchain is the
// regression for accidentally marking that runtime boundary constexpr.
static_assert(urnw::IsIpv4Literal("192.0.2.1"));
static_assert(!urnw::IsIpv4Literal("192.0.2.999"));
static_assert(urnw::IsIpv6Literal("2001:db8::1"));
static_assert(!urnw::IsIpv6Literal("2001:db8::1::2"));
static_assert(urnw::Ipv6PrefixContains("fc00::/7", "fd00::1"));
static_assert(urnw::IsIpv6UniqueLocal("fd00::1"));
static_assert(urnw::CaptureV6Claims("2001:db8::1"));
static_assert(!urnw::CaptureV6Claims("fe80::1"));

}  // namespace

// ---- v6 literals -----------------------------------------------------------

UR_TEST(ipv6LiteralAcceptsTheStandardForms) {
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("::1"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("::"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("fd00:7572:6e65:ffff::2"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("2001:db8::65:49:70:65"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("2001:0db8:0000:0000:0000:0000:0000:0001"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("2001:DB8::1"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("fe80::1"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("1::"));
  // an embedded dotted quad, as a mapped address is written
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("::ffff:192.0.2.1"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("64:ff9b::192.0.2.33"));
}

UR_TEST(ipv6LiteralRejectsWhatIsNotAnAddress) {
  UR_EXPECT_FALSE(urnw::IsIpv6Literal(""));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("169.254.2.1"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("resolver.example"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal(":1"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("1:"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal(":::"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("1::2::3"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("12345::1"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("g::1"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("1:2:3:4:5:6:7"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("1:2:3:4:5:6:7:8:9"));
  UR_EXPECT_TRUE(urnw::IsIpv6Literal("1:2:3:4:5:6:7::"));   // "::" is the one zero group left
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("1:2:3:4:5:6:7:8::"));  // nothing left for "::" to stand for
  // an embedded quad anywhere but last, or malformed
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("::192.0.2.1:1"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("::192.0.2"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("::192.0.2.256"));
  // no brackets, zone ids or prefix lengths: those are not addresses
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("[::1]"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("fe80::1%eth0"));
  UR_EXPECT_FALSE(urnw::IsIpv6Literal("fd00::/64"));
}

UR_TEST(ipv6LiteralParsesToTheRightBytes) {
  urnw::Ipv6Bytes bytes;
  UR_EXPECT_TRUE(urnw::ParseIpv6Literal("2001:db8::1", &bytes));
  UR_EXPECT_EQ(0x20, bytes.b[0]);
  UR_EXPECT_EQ(0x01, bytes.b[1]);
  UR_EXPECT_EQ(0x0d, bytes.b[2]);
  UR_EXPECT_EQ(0xb8, bytes.b[3]);
  for (int i = 4; i < 15; ++i) UR_EXPECT_EQ(0, bytes.b[i]);
  UR_EXPECT_EQ(1, bytes.b[15]);

  UR_EXPECT_TRUE(urnw::ParseIpv6Literal("::ffff:192.0.2.1", &bytes));
  UR_EXPECT_EQ(0xff, bytes.b[10]);
  UR_EXPECT_EQ(0xff, bytes.b[11]);
  UR_EXPECT_EQ(192, bytes.b[12]);
  UR_EXPECT_EQ(0, bytes.b[13]);
  UR_EXPECT_EQ(2, bytes.b[14]);
  UR_EXPECT_EQ(1, bytes.b[15]);

  UR_EXPECT_TRUE(urnw::ParseIpv6Literal("1::", &bytes));
  UR_EXPECT_EQ(0, bytes.b[0]);
  UR_EXPECT_EQ(1, bytes.b[1]);
  UR_EXPECT_EQ(0, bytes.b[15]);
}

UR_TEST(ipv6PrefixContainsHonoursThePrefixLength) {
  UR_EXPECT_TRUE(urnw::Ipv6PrefixContains("fc00::/7", "fd00:7572:6e65::1"));
  UR_EXPECT_TRUE(urnw::Ipv6PrefixContains("fc00::/7", "fc12::1"));
  UR_EXPECT_FALSE(urnw::Ipv6PrefixContains("fc00::/7", "fe80::1"));
  UR_EXPECT_TRUE(urnw::Ipv6PrefixContains("fe80::/10", "febf::1"));
  UR_EXPECT_FALSE(urnw::Ipv6PrefixContains("fe80::/10", "fec0::1"));
  UR_EXPECT_TRUE(urnw::Ipv6PrefixContains("::/0", "2001:db8::1"));
  UR_EXPECT_TRUE(urnw::Ipv6PrefixContains("::1/128", "::1"));
  UR_EXPECT_FALSE(urnw::Ipv6PrefixContains("::1/128", "::2"));
  // malformed is false, never a match
  UR_EXPECT_FALSE(urnw::Ipv6PrefixContains("fc00::/129", "fd00::1"));
  UR_EXPECT_FALSE(urnw::Ipv6PrefixContains("fc00::", "fd00::1"));
  UR_EXPECT_FALSE(urnw::Ipv6PrefixContains("fc00::/7", "not-an-address"));
}

// ---- the dual-stack floor --------------------------------------------------

UR_TEST(tunnelPolicyRuntimeValidatorsAcceptOwningStrings) {
  urnw::TunnelConfig config = DualStack();
  UR_EXPECT_TRUE(urnw::IsIpv4HalfValid(config));
  UR_EXPECT_TRUE(urnw::IsIpv6HalfValid(config));

  config.dns_servers_v4.push_back("not-an-address");
  UR_EXPECT_FALSE(urnw::IsIpv4HalfValid(config));
  UR_EXPECT_TRUE(urnw::IsIpv6HalfValid(config));

  config = DualStack();
  config.dns_servers_v6.push_back("not-an-address");
  UR_EXPECT_TRUE(urnw::IsIpv4HalfValid(config));
  UR_EXPECT_FALSE(urnw::IsIpv6HalfValid(config));
}

UR_TEST(tunnelPolicyAcceptsADualStackConfiguration) {
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(DualStack()));
}

UR_TEST(tunnelPolicyAcceptsTheConfigurationTheDaemonActuallyBuilds) {
  // TunnelHost's fallbacks verbatim: the addresses it substitutes when the
  // device reports unusable ones, and a resolver list of one per family. A
  // default that the guard refuses would refuse every connect.
  urnw::TunnelConfig config;
  UR_EXPECT_TRUE(config.local_addr_v4 == "169.254.2.1");
  UR_EXPECT_TRUE(config.local_addr_v6 == "fd00:7572:6e65:ffff::2");
  UR_EXPECT_EQ(config.prefix_v4, 24);
  UR_EXPECT_EQ(config.prefix_v6, 64);
  UR_EXPECT_EQ(config.mtu, urnw::kTunnelMtu);
  UR_EXPECT_EQ(config.mtu, 1280);
  UR_EXPECT_TRUE(config.dns_servers_v4.empty());
  UR_EXPECT_TRUE(config.dns_servers_v6.empty());
  config.dns_servers_v4 = {"65.49.70.65"};
  config.dns_servers_v6 = {"2001:db8::65:49:70:65"};
  config.require_egress_protection = true;
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));

  // No resolver of a family is a tunnel whose DNS the other family carries,
  // not a refusal: the nft DNS floor and status.dns_detail describe that state.
  config.dns_servers_v6.clear();
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));
  config.dns_servers_v4.clear();
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));

  // The fork's own field is not part of the address-family decision.
  config.require_egress_protection = false;
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));
}

UR_TEST(tunnelPolicyRefusesAMissingOrWrongFamilyHalf) {
  // the v4 half must be v4
  urnw::TunnelConfig config = DualStack();
  config.local_addr_v4 = "fd00::1";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config = DualStack();
  config.dns_servers_v4 = {"2001:4860:4860::8888"};
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));

  // the v6 half must be v6
  config = DualStack();
  config.local_addr_v6 = "169.254.2.1";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config = DualStack();
  config.local_addr_v6 = "";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config = DualStack();
  config.dns_servers_v6 = {"9.9.9.9"};
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  // One bad resolver poisons the whole list -- Open refuses rather than
  // quietly bringing a tunnel up with the survivors.
  config = DualStack();
  config.dns_servers_v6 = {"2001:db8::1", "resolver.example"};
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
}

UR_TEST(tunnelPolicyRequiresAUniqueLocalTunnelAddress) {
  // the SDK's fixed /48 and the daemon's fallback are both ULA
  UR_EXPECT_TRUE(urnw::IsIpv6UniqueLocal("fd00:7572:6e65:1a2b::1"));
  UR_EXPECT_TRUE(urnw::IsIpv6UniqueLocal("fd00:7572:6e65:ffff::2"));
  // a global address would be an address the tun claims on the internet, the
  // unspecified address is no address, and multicast cannot be an interface
  // address at all
  urnw::TunnelConfig config = DualStack();
  config.local_addr_v6 = "2001:db8::1";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.local_addr_v6 = "::";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.local_addr_v6 = "ff02::1";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.local_addr_v6 = "fe80::1";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
}

UR_TEST(tunnelPolicyRejectsHostnamesAndInvalidIpv4Values) {
  urnw::TunnelConfig config = DualStack();
  config.local_addr_v4 = "resolver.example";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));

  config.local_addr_v4 = "192.0.2.999";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));

  config.local_addr_v4 = "192.0.2.1";
  config.prefix_v4 = 33;
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
}

// ---- what the call site must refuse ----------------------------------------

UR_TEST(tunnelPolicyRefusesAPrefixThatWouldSwallowTheDefaultRoute) {
  urnw::TunnelConfig config = DualStack();
  // /0 on the tun means a CONNECTED route covering the whole space, which
  // captures traffic without any of the policy routing that is supposed to
  // decide it. Tunnel::Open's own prefix check used to accept 0.
  config.prefix_v4 = 0;
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.prefix_v4 = 1;
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));
  config.prefix_v4 = 32;
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));
  config.prefix_v4 = -1;
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));

  config = DualStack();
  config.prefix_v6 = 0;
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.prefix_v6 = 1;
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));
  config.prefix_v6 = 128;
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));
  config.prefix_v6 = 129;
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
}

UR_TEST(tunnelPolicyRefusesAnEmptyOrTruncatedAddress) {
  urnw::TunnelConfig config = DualStack();
  config.local_addr_v4 = "";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.local_addr_v4 = "192.0.2";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.local_addr_v4 = "192.0.2.";
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
  config.local_addr_v4 = "192.0.2.1";
  UR_EXPECT_TRUE(urnw::IsDualStackTunnelConfig(config));
  // One bad resolver poisons the whole list -- Open refuses rather than
  // quietly bringing a tunnel up with the survivors.
  config.dns_servers_v4 = {"9.9.9.9", "2001:4860:4860::8888"};
  UR_EXPECT_FALSE(urnw::IsDualStackTunnelConfig(config));
}

UR_TEST(tunnelMtuClearsTheIpv6LinkMinimum) {
  // The daemon hands the same MTU to both halves. Linux disables IPv6 on an
  // interface whose MTU is below 1280, so the constant must never fall below
  // it -- the v6 half of the tunnel would silently cease to exist.
  UR_EXPECT_TRUE(urnw::kIpv6MinimumMtu <= urnw::kTunnelMtu);
  UR_EXPECT_EQ(1280, urnw::kIpv6MinimumMtu);
}

// ---- the v6 capture set ----------------------------------------------------

UR_TEST(captureV6SetIsTheComplementOfTheThreeExclusions) {
  // captured: global unicast, the documentation prefix the in-tunnel resolver
  // and the egress witness use, 6to4, NAT64, the whole lower half
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("2001:db8::65:49:70:65"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("2001:db8::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("2606:4700:4700::1111"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("2002:c000:0204::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("3fff::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("64:ff9b::192.0.2.33"));
  // the edges of every capture prefix
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("7fff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("8000::"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("bfff::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("c000::"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("dfff::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("e000::"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("efff::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("f000::"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("f7ff::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("f800::"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("fbff:ffff::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("fe00::"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("fe7f:ffff::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("fec0::"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("feff:ffff::1"));

  // excluded: ULA (the LAN analogue, and the tun's own family), link-local,
  // multicast -- including the solicited-node block DAD depends on
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("fc00::1"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("fd00:7572:6e65:ffff::2"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("fdff:ffff::1"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("fe80::1"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("febf:ffff::1"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("ff02::1"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("ff02::1:ff00:1"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("ff05::1:3"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("ffff::1"));
}

UR_TEST(captureV6PrefixesAreEightAndDisjointFromTheExclusions) {
  const auto& capture = urnw::CaptureV6Prefixes();
  UR_EXPECT_EQ(8u, capture.size());
  // every capture prefix is a well-formed prefix that excludes all three
  for (const auto& prefix : capture) {
    const size_t slash = prefix.find('/');
    UR_EXPECT_TRUE(slash != std::string::npos);
    UR_EXPECT_TRUE(urnw::IsIpv6Literal(prefix.substr(0, slash)));
    for (const auto& excluded : urnw::ExcludedV6Prefixes()) {
      const size_t excludedSlash = excluded.find('/');
      const std::string excludedNetwork = excluded.substr(0, excludedSlash);
      UR_EXPECT_TRUE_MSG(prefix + " must not contain " + excluded,
                         !urnw::Ipv6PrefixContains(prefix, excludedNetwork));
    }
  }
  // and the three exclusions are exactly the ones the design names
  UR_EXPECT_EQ(3u, urnw::ExcludedV6Prefixes().size());
  UR_EXPECT_TRUE(urnw::ExcludedV6Prefixes()[0] == "fe80::/10");
  UR_EXPECT_TRUE(urnw::ExcludedV6Prefixes()[1] == "fc00::/7");
  UR_EXPECT_TRUE(urnw::ExcludedV6Prefixes()[2] == "ff00::/8");
}

// Loopback is claimed by the kernel's `local` table (rule pref 0) before the
// capture rule is ever consulted, exactly as 127.0.0.0/8 inside 64.0.0.0/2 is
// for v4 -- so ::1 sits inside ::/1 and needs no route of its own.
UR_TEST(captureV6SetLeavesLoopbackToTheLocalTable) {
  UR_EXPECT_TRUE(urnw::Ipv6PrefixContains("::/1", "::1"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("::1"));
}

UR_TEST(nftCgroupPolicyKeepsTheCgroupBeltWhenTheKernelSupportsIt) {
  UR_EXPECT_TRUE(urnw::SelectNftCgroupMode(/*socketMarkerProven=*/false,
                                           /*cgroupSocketMatchSupported=*/true,
                                           /*blockFloor=*/true,
                                           /*helperDnsRequired=*/true) ==
                 urnw::NftCgroupMode::CgroupAndMark);
}

UR_TEST(nftCgroupPolicyUsesAProvenMarkOnFloorlessUnsupportedKernels) {
  UR_EXPECT_TRUE(urnw::SelectNftCgroupMode(/*socketMarkerProven=*/true,
                                           /*cgroupSocketMatchSupported=*/false,
                                           /*blockFloor=*/false,
                                           /*helperDnsRequired=*/false) ==
                 urnw::NftCgroupMode::MarkOnly);
}

UR_TEST(nftCgroupPolicyRefusesAnUnprovenMarkOnUnsupportedKernels) {
  UR_EXPECT_TRUE(urnw::SelectNftCgroupMode(/*socketMarkerProven=*/false,
                                           /*cgroupSocketMatchSupported=*/false,
                                           /*blockFloor=*/false,
                                           /*helperDnsRequired=*/false) ==
                 urnw::NftCgroupMode::Refuse);
}

UR_TEST(nftCgroupPolicyDoesNotWeakenFloorOrHelperDns) {
  UR_EXPECT_TRUE(urnw::SelectNftCgroupMode(/*socketMarkerProven=*/true,
                                           /*cgroupSocketMatchSupported=*/false,
                                           /*blockFloor=*/true,
                                           /*helperDnsRequired=*/false) ==
                 urnw::NftCgroupMode::Refuse);
  UR_EXPECT_TRUE(urnw::SelectNftCgroupMode(/*socketMarkerProven=*/true,
                                           /*cgroupSocketMatchSupported=*/false,
                                           /*blockFloor=*/false,
                                           /*helperDnsRequired=*/true) ==
                 urnw::NftCgroupMode::Refuse);
}

// ---- the call site ---------------------------------------------------------

namespace {

std::string ReadTunnelSource() {
  const std::string candidates[] = {
      std::string(UR_SRC_DIR) + "/Tunnel.cpp",
      "app/src/Tunnel.cpp",
      "../app/src/Tunnel.cpp",
      "linux/app/src/Tunnel.cpp",
  };
  for (const auto& path : candidates) {
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    std::ostringstream out;
    out << in.rdbuf();
    if (!out.str().empty()) return out.str();
  }
  return std::string();
}

}  // namespace

UR_TEST(tunnelOpenRefusesANonDualStackConfigurationBeforeItTouchesTheDevice) {
  const std::string source = ReadTunnelSource();
  // NOT a skip. A predicate nobody calls is the defect this test exists for, so
  // "I could not check" has to read as failure, not as silence.
  // UR_FAIL records and continues, so every fatal step returns: one clear line
  // beats a cascade of consequences of the same missing thing.
  if (source.empty()) {
    UR_FAIL("could not read Tunnel.cpp to check the guard is wired");
    return;
  }

  const size_t open = source.find("std::unique_ptr<Tunnel> Tunnel::Open(");
  if (open == std::string::npos) {
    UR_FAIL("Tunnel::Open was not found in Tunnel.cpp");
    return;
  }

  const size_t guard = source.find("IsDualStackTunnelConfig(cfg)", open);
  if (guard == std::string::npos) {
    UR_FAIL("Tunnel::Open does not call IsDualStackTunnelConfig -- the dual-stack "
            "policy exists and nothing enforces it");
    return;
  }

  if (source.find("[tun] refusing a tunnel configuration that is not dual-stack", open) ==
      std::string::npos) {
    UR_FAIL("the [tun] refusal diagnostic is missing from Tunnel::Open");
  }

  // BEFORE the device: the whole point is that no tun is created, no address is
  // assigned and no route is installed for a configuration we will not honour.
  const size_t device = source.find("\"/dev/net/tun\"", open);
  if (device == std::string::npos) {
    UR_FAIL("Tunnel::Open no longer opens /dev/net/tun");
    return;
  }
  if (guard > device) UR_FAIL("the dual-stack guard runs AFTER the tun device is opened");

  // And ahead of every other field check.
  const size_t firstOtherCheck = source.find("ValidInterfaceName(cfg.name)", open);
  if (firstOtherCheck != std::string::npos && guard > firstOtherCheck) {
    UR_FAIL("the dual-stack guard is no longer the first check in Tunnel::Open");
  }

  // The v6 half is installed from the ONE capture table, so the routes and the
  // firewall's v6 permits cannot disagree about what is captured.
  if (source.find("CaptureV6Prefixes()", open) == std::string::npos) {
    UR_FAIL("Tunnel.cpp installs no v6 capture routes: the v6 half of the tunnel is missing");
  }
}
