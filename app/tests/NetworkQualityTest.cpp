// Deterministic Linux network-quality classification roots.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "NetworkQuality.hpp"

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

UR_TEST(networkQualityTrackerSeparatesPathAndQualityChanges) {
  urnw::LinuxNetworkQualityTracker tracker;
  urnw::LinuxNetworkQualitySnapshot wifi{"wlan0", 1, 4, 64};
  UR_EXPECT_EQ(urnw::LinuxNetworkChange::None, tracker.Observe(wifi));
  UR_EXPECT_EQ(urnw::LinuxNetworkChange::None, tracker.Observe(wifi));

  auto weakWifi = wifi;
  weakWifi.wireless_signal_level = 1;
  UR_EXPECT_EQ(urnw::LinuxNetworkChange::Quality, tracker.Observe(weakWifi));

  auto fasterWifi = weakWifi;
  fasterWifi.speed_bucket_mbps = 256;
  UR_EXPECT_EQ(urnw::LinuxNetworkChange::Quality, tracker.Observe(fasterWifi));

  urnw::LinuxNetworkQualitySnapshot cellular{"wwan0", 1, -1, 32};
  UR_EXPECT_EQ(urnw::LinuxNetworkChange::Path, tracker.Observe(cellular));
  cellular.carrier = 0;
  UR_EXPECT_EQ(urnw::LinuxNetworkChange::Path, tracker.Observe(cellular));
}

UR_TEST(networkQualitySpeedUsesPowerOfTwoBands) {
  UR_EXPECT_EQ(std::uint64_t{64}, urnw::LinuxNetworkSpeedBucket(70));
  UR_EXPECT_EQ(std::uint64_t{64}, urnw::LinuxNetworkSpeedBucket(120));
  UR_EXPECT_EQ(std::uint64_t{128}, urnw::LinuxNetworkSpeedBucket(128));
}
