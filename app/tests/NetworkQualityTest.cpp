// Deterministic Linux network-quality classification roots.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "NetworkQuality.hpp"

#include <cstdint>
#include <string>

UR_TEST(networkQualityRouteParserChoosesLowestMetricPhysicalDefault) {
  const std::string routes =
      "Iface Destination Gateway Flags RefCnt Use Metric Mask\n"
      "urnet0 00000000 00000000 0001 0 0 1 00000000\n"
      "wlan0 00000000 01020304 0003 0 0 600 00000000\n"
      "wwan0 00000000 01020304 0003 0 0 100 00000000\n";
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteInterface(routes, "urnet0") == "wwan0");
}

UR_TEST(networkQualityWirelessJitterStaysInsideOneBar) {
  const std::string first = "Inter-| sta\n face | status\n wlan0: 0000 55. -60. -256\n";
  const std::string second = "Inter-| sta\n face | status\n wlan0: 0000 54. -61. -256\n";
  UR_EXPECT_EQ(3, urnw::LinuxWirelessSignalLevel(first, "wlan0"));
  UR_EXPECT_EQ(3, urnw::LinuxWirelessSignalLevel(second, "wlan0"));
}

UR_TEST(networkQualityWirelessParserIgnoresAnEmptyInterface) {
  const std::string wireless = "Inter-| sta\n     : 0000 70. -40. -256\n";

  UR_EXPECT_EQ(-1, urnw::LinuxWirelessSignalLevel(wireless, "wlan0"));
}

UR_TEST(networkQualitySpeedUsesPowerOfTwoBands) {
  UR_EXPECT_EQ(std::uint64_t{64}, urnw::LinuxNetworkSpeedBucket(70));
  UR_EXPECT_EQ(std::uint64_t{64}, urnw::LinuxNetworkSpeedBucket(120));
  UR_EXPECT_EQ(std::uint64_t{128}, urnw::LinuxNetworkSpeedBucket(128));
}

// ---- the network path and when it kicks --------------------------------------

namespace {

using urnw::LinuxNetworkChange;

constexpr LinuxNetworkChange kNone = LinuxNetworkChange::None;
constexpr LinuxNetworkChange kQuality = LinuxNetworkChange::Quality;
constexpr LinuxNetworkChange kPath = LinuxNetworkChange::Path;
// One reaper tick.
constexpr std::int64_t kTick = 1000;
// The defaults the trackers and the detector below are built with, read where
// they are set (the scenarios count ticks against the default holdoff, which
// networkPathSettingsDefaultsArePinned pins).
const urnw::LinuxNetworkPathSettings kPathSettings{};
const urnw::LinuxSuspendSettings kSuspendSettings{};

urnw::LinuxFamilyPath Family(const char* iface, const char* gateway, const char* source,
                             std::uint64_t carrierUps = 1) {
  urnw::LinuxFamilyPath path;
  path.interface_name = iface;
  path.gateway = gateway;
  path.source = source;
  path.carrier_ups = carrierUps;
  return path;
}

urnw::LinuxNetworkQualitySnapshot Snapshot(const urnw::LinuxFamilyPath& v4,
                                           const urnw::LinuxFamilyPath& v6 = {},
                                           int carrier = 1, int signal = -1,
                                           std::uint64_t speed = 0) {
  urnw::LinuxNetworkQualitySnapshot snapshot;
  snapshot.v4 = v4;
  snapshot.v6 = v6;
  snapshot.interface_name = v4.present() ? v4.interface_name : v6.interface_name;
  snapshot.carrier = snapshot.interface_name.empty() ? -1 : carrier;
  snapshot.wireless_signal_level = signal;
  snapshot.speed_bucket_mbps = speed;
  return snapshot;
}

const urnw::LinuxNetworkQualitySnapshot kOffline{};

const urnw::LinuxFamilyPath kWifiA = Family("wlan0", "0101A8C0", "192.168.1.20", 5);
const urnw::LinuxFamilyPath kEthernet = Family("eth0", "010014AC", "172.20.0.9", 2);
const urnw::LinuxFamilyPath kWifiA6 =
    Family("wlan0", "fe800000000000000000000000000001", "2001:db8:1:2::/64", 5);
const urnw::LinuxFamilyPath kEthernet6 =
    Family("eth0", "fe800000000000000000000000000009", "2001:db8:9:9::/64", 2);

bool SameIdentity(const urnw::LinuxFamilyPath& a, const urnw::LinuxFamilyPath& b) {
  return urnw::SameLinuxPathIdentity(a, b);
}

}  // namespace

// The defaults the scenarios below are counted against.
UR_TEST(networkPathSettingsDefaultsArePinned) {
  UR_EXPECT_EQ(2 * kTick, kPathSettings.holdoff_millis);
  UR_EXPECT_EQ(1000, kSuspendSettings.threshold_millis);
  UR_EXPECT_EQ(kPathSettings.holdoff_millis,
               urnw::LinuxNetworkQualityTracker().settings().holdoff_millis);
}

// A holdoff that is not the default is the one the tracker waits out: none at
// all kicks both halves of a dual-stack move a tick apart, a long one holds the
// second until it ends.
UR_TEST(networkPathANonDefaultHoldoffIsHonoured) {
  for (const std::int64_t holdoff : {std::int64_t{0}, 5 * kTick}) {
    urnw::LinuxNetworkPathSettings settings;
    settings.holdoff_millis = holdoff;
    urnw::LinuxNetworkQualityTracker tracker(settings);
    std::int64_t now = 0;
    tracker.Rebase(Snapshot(kWifiA, kWifiA6));
    UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kEthernet, kWifiA6), now += kTick));
    const std::int64_t firstKick = now;
    int ticks = 0;
    LinuxNetworkChange change = kNone;
    while (change != kPath && ticks < 20) {
      change = tracker.Observe(Snapshot(kEthernet, kEthernet6), now += kTick);
      ++ticks;
    }
    // The second kick lands on the first tick at or past the holdoff's end.
    const std::int64_t expected = holdoff < kTick ? kTick : holdoff;
    UR_EXPECT_EQ(expected, now - firstKick);
  }
}

UR_TEST(aNonDefaultSuspendThresholdIsHonoured) {
  urnw::LinuxSuspendSettings settings;
  settings.threshold_millis = 5000;
  urnw::LinuxSuspendDetector detector(settings);
  UR_EXPECT_FALSE(detector.Observe(0));
  UR_EXPECT_FALSE(detector.Observe(2000));  // a resume by the default, not by these
  UR_EXPECT_TRUE(detector.Observe(7000));
  urnw::LinuxSuspendDetector byDefault(kSuspendSettings);
  UR_EXPECT_FALSE(byDefault.Observe(0));
  UR_EXPECT_TRUE(byDefault.Observe(2000));
}

// A threshold of 0 turns the detector off: a zero bar would read every tick,
// the clocks unmoved included, as a resume and kick on each one.
UR_TEST(aZeroSuspendThresholdTurnsTheDetectorOff) {
  urnw::LinuxSuspendSettings settings;
  settings.threshold_millis = 0;
  urnw::LinuxSuspendDetector detector(settings);
  UR_EXPECT_FALSE(detector.Observe(0));
  UR_EXPECT_FALSE(detector.Observe(0));
  UR_EXPECT_FALSE(detector.Observe(8LL * 3600 * 1000));
}

UR_TEST(networkQualityTrackerSeparatesPathAndQualityChanges) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  const auto wifi = Snapshot(kWifiA, {}, 1, 4, 64);
  tracker.Rebase(wifi);
  UR_EXPECT_EQ(kNone, tracker.Observe(wifi, now += kTick));

  auto weakWifi = wifi;
  weakWifi.wireless_signal_level = 1;
  UR_EXPECT_EQ(kQuality, tracker.Observe(weakWifi, now += kTick));

  auto fasterWifi = weakWifi;
  fasterWifi.speed_bucket_mbps = 256;
  UR_EXPECT_EQ(kQuality, tracker.Observe(fasterWifi, now += kTick));

  auto cellular = Snapshot(Family("wwan0", "", "10.64.12.7", 1), {}, 1, -1, 32);
  UR_EXPECT_EQ(kPath, tracker.Observe(cellular, now += kTick));
  // INVERTED: a carrier change alone moves no path. It used to kick, both edges
  // of every flap, each one a dial out of a link that could not carry it.
  cellular.carrier = 0;
  UR_EXPECT_EQ(kNone, tracker.Observe(cellular, now += 5 * kTick));
}

// Pull the cable with wifi up: NetworkManager keeps the cable's route for a few
// seconds and the kernel keeps using it, so the probe still binds the cable's
// address and nothing kicks until the route moves -- then exactly once.
UR_TEST(networkPathACarrierDropUnderARouteTheKernelStillUsesNeverKicks) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  tracker.Rebase(Snapshot(kEthernet));
  for (int i = 0; i < 5; ++i) {
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kEthernet, {}, /*carrier=*/0), now += kTick));
  }
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA), now += kTick));
  for (int i = 0; i < 5; ++i) {
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA), now += kTick));
  }
}

// Wifi A to wifi B: the old network's routes go first, then the new ones come.
// Nothing kicks into the gap; the new network does.
UR_TEST(networkPathLossNeverKicksAndTheNextNetworkDoes) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  tracker.Rebase(Snapshot(kWifiA));
  for (int i = 0; i < 4; ++i) UR_EXPECT_EQ(kNone, tracker.Observe(kOffline, now += kTick));
  const auto wifiB = Snapshot(Family("wlan0", "0100000A", "10.0.0.34", 6));
  UR_EXPECT_EQ(kPath, tracker.Observe(wifiB, now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(wifiB, now += kTick));
  // A single family lost beside a surviving one is not a loss of the machine,
  // but the transports may have been on it: that is a move.
  tracker.Rebase(Snapshot(kWifiA, kWifiA6));
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA), now += kTick));
}

// Two networks handing out the same addressing (two home routers on
// 192.168.1.0/24) differ only in that the carrier came up again across a gap.
UR_TEST(networkPathASameIdentityRejoinAcrossAGapKicks) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  tracker.Rebase(Snapshot(kWifiA));
  UR_EXPECT_EQ(kNone, tracker.Observe(kOffline, now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(kOffline, now += kTick));
  auto rejoined = kWifiA;
  rejoined.carrier_ups = kWifiA.carrier_ups + 1;
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(rejoined), now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(rejoined), now += kTick));
  // The same identity back across a gap WITHOUT the carrier coming up again (a
  // lease renewed in place, a route flushed and restored) is the same network.
  UR_EXPECT_EQ(kNone, tracker.Observe(kOffline, now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(rejoined), now += kTick));
}

// A roam between access points of one network: the carrier comes up again
// with no gap and no new address. Not a kick, and the counter is taken in, so
// a later gap is measured from the roam rather than misread as a rejoin.
UR_TEST(networkPathARoamInsideOneNetworkDoesNotKick) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  tracker.Rebase(Snapshot(kWifiA));
  auto roamed = kWifiA;
  roamed.carrier_ups = kWifiA.carrier_ups + 1;
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(roamed), now += kTick));
  UR_EXPECT_TRUE(tracker.kicked().v4.carrier_ups == roamed.carrier_ups);
  UR_EXPECT_EQ(kNone, tracker.Observe(kOffline, now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(roamed), now += kTick));
}

// Joining a dual-stack network from nothing: IPv4 lands first and kicks; IPv6
// arriving beside it a second later moves nothing the transports use.
UR_TEST(networkPathAFamilyThatOnlyAppearsRidesAlong) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  tracker.Rebase(kOffline);
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA), now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA, kWifiA6), now += kTick));
  UR_EXPECT_TRUE(SameIdentity(tracker.kicked().v6, kWifiA6));
  // ...and once taken in, that family's own move kicks.
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA, kEthernet6), now += 3 * kTick));
}

// Wifi to ethernet on a dual-stack host: IPv4 moves first (DHCP), IPv6 a
// second later (the router advertisement). One kick at once, one more when the
// holdoff ends -- never one per family per tick.
UR_TEST(networkPathADualStackMoveKicksOnceThenOnceMoreWhenTheHoldoffEnds) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  tracker.Rebase(Snapshot(kWifiA, kWifiA6));
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kEthernet, kWifiA6), now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kEthernet, kEthernet6), now += kTick));
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kEthernet, kEthernet6), now += kTick));
  UR_EXPECT_TRUE(SameIdentity(tracker.kicked().v6, kEthernet6));
  for (int i = 0; i < 4; ++i) {
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kEthernet, kEthernet6), now += kTick));
  }
  // A change that settled back to the kicked path inside the holdoff costs
  // nothing at all.
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA, kEthernet6), now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kEthernet, kEthernet6), now += kTick / 2));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA, kEthernet6), now += kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA, kEthernet6), now += kTick));
}

// A link flapping between two paths every tick costs at most one kick per
// holdoff, and the tracker still ends on the path the link settles on.
UR_TEST(networkPathAFlappingLinkCostsAtMostOneKickPerHoldoff) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  tracker.Rebase(Snapshot(kWifiA));
  int kicks = 0;
  for (int i = 0; i < 20; ++i) {
    const auto next = (i % 2 == 0) ? Snapshot(kEthernet) : Snapshot(kWifiA);
    if (tracker.Observe(next, now += kTick) == kPath) ++kicks;
  }
  const int ceiling = static_cast<int>((20 * kTick) / kPathSettings.holdoff_millis) + 1;
  if (kicks < 1 || kicks > ceiling) {
    UR_FAIL("a 20-tick flap kicked " + std::to_string(kicks) + " times; at most " +
            std::to_string(ceiling) + " are allowed");
  }
  // The flap ends on the path it was NOT last kicked onto: the trailing
  // re-evaluation still lands on it once the holdoff is over.
  for (int i = 0; i < 3; ++i) tracker.Observe(Snapshot(kWifiA), now += kTick);
  UR_EXPECT_TRUE(SameIdentity(tracker.kicked().v4, kWifiA));
}

UR_TEST(networkPathAnIpv6PrivacyAddressIsComparedAtItsSlash64) {
  UR_EXPECT_TRUE(urnw::LinuxIpv6Prefix64("2001:db8:1:2::abcd") == "2001:db8:1:2::/64");
  UR_EXPECT_TRUE(urnw::LinuxIpv6Prefix64("2001:0db8:0001:0002:1111:2222:3333:4444") ==
                 "2001:db8:1:2::/64");
  UR_EXPECT_TRUE(urnw::LinuxIpv6Prefix64("2001:db8:1:3::abcd") == "2001:db8:1:3::/64");
  UR_EXPECT_TRUE(urnw::LinuxIpv6Prefix64("::1") == "0:0:0:0::/64");
  UR_EXPECT_TRUE(urnw::LinuxIpv6Prefix64("192.168.1.20").empty());
  UR_EXPECT_TRUE(urnw::LinuxIpv6Prefix64("fe80::1%2").empty());
}

// An IPv6-only network (a carrier's v6-only APN) is a network like any other:
// its paths change, and joining it from nothing kicks.
UR_TEST(networkPathAnIpv6OnlyNetworkKicksLikeAnyOther) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  const auto modem = Family("wwan0", "", "2001:db8:aa:1::/64", 1);
  tracker.Rebase(kOffline);
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot({}, modem), now += kTick));
  UR_EXPECT_TRUE(tracker.kicked().interface_name == "wwan0");
  const auto renumbered = Family("wwan0", "", "2001:db8:aa:7::/64", 1);
  UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot({}, renumbered), now += 3 * kTick));
  UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot({}, renumbered), now += kTick));
}

UR_TEST(networkPathTheDeviceBaselineAndAForcedKick) {
  std::int64_t now = 0;
  {
    // The network a device started on is its baseline: unchanged, nothing.
    urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
    tracker.Rebase(Snapshot(kWifiA));
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA), now += kTick));
    // Moved since the device was created (a switch during a bring-up): kicked
    // on the first tick.
    tracker.Rebase(Snapshot(kWifiA));
    UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kEthernet), now += kTick));
    // A new device's baseline carries no holdoff from a kick before it.
    tracker.Rebase(Snapshot(kEthernet));
    UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA), now += 1));
  }
  {
    // A device created offline is kicked by its first network.
    urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
    tracker.Rebase(kOffline);
    UR_EXPECT_EQ(kNone, tracker.Observe(kOffline, now += kTick));
    UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA), now += kTick));
  }
  {
    // A resume: the next tick with a path kicks, unchanged or not, and once.
    urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
    tracker.Rebase(Snapshot(kWifiA));
    tracker.ForceKickOnNextPath();
    UR_EXPECT_EQ(kNone, tracker.Observe(kOffline, now += kTick));
    UR_EXPECT_EQ(kPath, tracker.Observe(Snapshot(kWifiA), now += kTick));
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA), now += 5 * kTick));
    // A Rebase (a new device) owes nothing for a resume before it.
    tracker.ForceKickOnNextPath();
    tracker.Rebase(Snapshot(kWifiA));
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA), now += kTick));
  }
  {
    // Without a baseline the first observation is the baseline, as after Reset.
    urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kWifiA), now += kTick));
    tracker.Reset();
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kEthernet), now += kTick));
    UR_EXPECT_EQ(kNone, tracker.Observe(Snapshot(kEthernet), now += kTick));
  }
}

UR_TEST(networkPathGatewayParsersReadTheInterfacesOwnDefault) {
  const std::string routes =
      "Iface Destination Gateway Flags RefCnt Use Metric Mask\n"
      "wlan0 00000000 0101A8C0 0003 0 0 600 00000000\n"
      "wlan0 0001A8C0 00000000 0001 0 0 600 00FFFFFF\n"
      "eth0 00000000 010014AC 0003 0 0 100 00000000\n"
      "eth0 00000000 020014AC 0003 0 0 100 00000000\n"
      "wwan0 00000000 00000000 0001 0 0 700 00000000\n";
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGateway(routes, "wlan0") == "0101A8C0");
  // Equal metrics: the smallest next hop, whatever order the table prints.
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGateway(routes, "eth0") == "010014AC");
  // An on-link default (a point-to-point modem) has no next hop.
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGateway(routes, "wwan0").empty());
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGateway(routes, "urnet0").empty());

  const std::string zero(32, '0');
  const std::string routes6 =
      zero + " 00 " + zero + " 00 fe800000000000000000000000000001 00000400 00000001 00000000 "
             "00450003 wlan0\n" +
      zero + " 00 " + zero + " 00 fe800000000000000000000000000002 00000200 00000001 00000000 "
             "00450003 wlan0\n" +
      zero + " 00 " + zero + " 00 " + zero +
      " ffffffff 00000001 00000000 00200200 lo\n"
      "00000000000000000000000000000000 01 " + zero + " 00 " + zero +
      " 00000000 00000001 00000000 00000001 urnet0\n" +
      zero + " 00 " + zero + " 00 fe800000000000000000000000000009 00000400 00000001 00000000 "
             "00000201 eth0\n";
  // The lower metric of the two routers.
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGatewayV6(routes6, "wlan0") ==
                 "fe800000000000000000000000000002");
  // A reject route is no way off the machine.
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGatewayV6(routes6, "eth0").empty());
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGatewayV6(routes6, "urnet0").empty());
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteGatewayV6("", "wlan0").empty());
}

UR_TEST(networkPathLinkLocalAndLoopbackSourcesAreNotAWayOff) {
  UR_EXPECT_TRUE(urnw::LinuxSourceLeavesTheLink("192.168.1.20"));
  UR_EXPECT_TRUE(urnw::LinuxSourceLeavesTheLink("2001:db8:1:2::abcd"));
  UR_EXPECT_TRUE(urnw::LinuxSourceLeavesTheLink("fd00:1::5"));
  UR_EXPECT_FALSE(urnw::LinuxSourceLeavesTheLink("169.254.7.7"));
  UR_EXPECT_FALSE(urnw::LinuxSourceLeavesTheLink("127.0.0.1"));
  UR_EXPECT_FALSE(urnw::LinuxSourceLeavesTheLink("fe80::1"));
  UR_EXPECT_FALSE(urnw::LinuxSourceLeavesTheLink("febf::1"));
  UR_EXPECT_FALSE(urnw::LinuxSourceLeavesTheLink("::1"));
  UR_EXPECT_FALSE(urnw::LinuxSourceLeavesTheLink(""));
  UR_EXPECT_FALSE(urnw::LinuxSourceLeavesTheLink("not-an-address"));
}

// What the dead-tunnel failsafe holds its no-exit rules on: the last reading
// had no path in any family. Never before a reading, never after a Reset.
UR_TEST(networkPathTheTrackerSaysWhenTheMachineIsOffline) {
  urnw::LinuxNetworkQualityTracker tracker(kPathSettings);
  std::int64_t now = 0;
  UR_EXPECT_FALSE(tracker.offline());
  tracker.Rebase(kOffline);
  UR_EXPECT_TRUE(tracker.offline());
  tracker.Observe(Snapshot({}, kWifiA6), now += kTick);
  UR_EXPECT_FALSE(tracker.offline());
  tracker.Observe(kOffline, now += kTick);
  UR_EXPECT_TRUE(tracker.offline());
  tracker.Observe(Snapshot(kWifiA), now += kTick);
  UR_EXPECT_FALSE(tracker.offline());
  tracker.Observe(kOffline, now += kTick);
  tracker.Reset();
  UR_EXPECT_FALSE(tracker.offline());
}

UR_TEST(networkPathChangesAreLoggedWithoutAddresses) {
  const std::string line = urnw::DescribeLinuxNetworkPath(Snapshot(kEthernet, kWifiA6));
  UR_EXPECT_TRUE(line == "v4 eth0, v6 wlan0");
  UR_EXPECT_TRUE(line.find("172.20") == std::string::npos);
  UR_EXPECT_TRUE(urnw::DescribeLinuxNetworkPath(kOffline) == "v4 none, v6 none");
}

// The suspend clock: CLOCK_BOOTTIME minus CLOCK_MONOTONIC grows only while the
// machine sleeps, so a stalled main loop (both clocks move) is never a resume.
UR_TEST(aSuspendIsTheSuspendClockGrowingNotATickGap) {
  struct Row {
    std::int64_t suspendedMillis;
    bool resumed;
  };
  const Row rows[] = {
      {0, false},       // the first reading is the baseline
      {0, false},       // a tick, or a ten-second stall of the loop: unchanged
      {3, false},       // clock read jitter
      {1002, false},    // 999 ms asleep: below the bar
      {2002, true},     // a one-second sleep
      {2002, false},    // consumed
      {3602002, true},  // an hour's sleep
      {3602001, false}, // never backwards into a resume
  };
  urnw::LinuxSuspendDetector detector(kSuspendSettings);
  int index = 0;
  for (const Row& row : rows) {
    if (detector.Observe(row.suspendedMillis) != row.resumed) {
      UR_FAIL("row " + std::to_string(index) + " (" + std::to_string(row.suspendedMillis) +
              " ms) read wrong");
    }
    ++index;
  }
}
