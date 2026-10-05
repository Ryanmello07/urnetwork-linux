// Why an enabled provider is idle (ProviderStatusPresentation.hpp,
// ProviderIdleReasonFor): every row of the decision in order, the precedences
// between them (Auto disconnected and paused, Network paused, Never paused,
// an unknown mode, the two pause reasons, traffic or none), and this
// platform's rule that no reason is derived while no tunnel session runs,
// since Linux provides nothing then in any mode.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <string>

#include "ProviderStatusPresentation.hpp"

using urnw::providerstatus::kProvideModePublic;
using urnw::providerstatus::ProvideNetworkMode;
using urnw::providerstatus::ProviderIdleReason;
using urnw::providerstatus::ProviderIdleReasonFor;
using urnw::providerstatus::SessionIdleReasonFor;

namespace {

// the live provide modes (a bit set: 0 none, 1 network, 2 friends-and-family)
constexpr int64_t kNone = 0;
constexpr int64_t kNetwork = 1;
constexpr int64_t kFriendsAndFamily = 2;

ProviderIdleReason Reason(const std::string& controlMode, int64_t liveMode, bool paused = false,
                          ProvideNetworkMode network = ProvideNetworkMode::All,
                          int64_t bytes = 0) {
  return ProviderIdleReasonFor(controlMode, liveMode, paused, network, bytes);
}

bool Is(ProviderIdleReason expected, ProviderIdleReason actual) { return expected == actual; }

UR_TEST(ProviderIdleNeverAndUnknownModesGiveNoReason) {
  for (const char* mode : {"never", "manual", "", "Always", "bogus"}) {
    for (const int64_t live : {kNone, kNetwork, kProvideModePublic}) {
      UR_EXPECT_TRUE_MSG(mode, Is(ProviderIdleReason::None, Reason(mode, live)));
      UR_EXPECT_TRUE_MSG(mode, Is(ProviderIdleReason::None,
                                  Reason(mode, live, true, ProvideNetworkMode::WiFi, 0)));
      UR_EXPECT_TRUE_MSG(mode, Is(ProviderIdleReason::None,
                                  Reason(mode, live, false, ProvideNetworkMode::All, 4096)));
    }
  }
}

UR_TEST(ProviderIdleNetworkModeIsNetworkOnly) {
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NetworkOnly, Reason("network", kNetwork)));
  // whatever the live state says: carrying traffic, or even public
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NetworkOnly,
                    Reason("network", kProvideModePublic, false, ProvideNetworkMode::All, 4096)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NetworkOnly, Reason("network", kNone)));
}

UR_TEST(ProviderIdleAutoWithoutThePublicModeIsNotConnected) {
  for (const int64_t live : {kNone, kNetwork, kFriendsAndFamily}) {
    UR_EXPECT_TRUE(Is(ProviderIdleReason::AutoNotConnected, Reason("auto", live)));
    UR_EXPECT_TRUE(Is(ProviderIdleReason::AutoNotConnected,
                      Reason("auto", live, false, ProvideNetworkMode::All, 4096)));
  }
  // connected, Auto provides to everyone and reads like Always
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NoTrafficYet, Reason("auto", kProvideModePublic)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None,
                    Reason("auto", kProvideModePublic, false, ProvideNetworkMode::All, 4096)));
}

UR_TEST(ProviderIdlePausedOnWifiOnlyAndOtherwise) {
  UR_EXPECT_TRUE(Is(ProviderIdleReason::PausedWifiOnly,
                    Reason("always", kProvideModePublic, true, ProvideNetworkMode::WiFi)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::PausedNoNetwork,
                    Reason("always", kProvideModePublic, true, ProvideNetworkMode::All)));
  // paused wins over the traffic rule, either way round
  UR_EXPECT_TRUE(Is(ProviderIdleReason::PausedNoNetwork,
                    Reason("always", kProvideModePublic, true, ProvideNetworkMode::All, 4096)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::PausedWifiOnly,
                    Reason("auto", kProvideModePublic, true, ProvideNetworkMode::WiFi)));
}

UR_TEST(ProviderIdlePublicWithoutTrafficIsNoTrafficYet) {
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NoTrafficYet, Reason("always", kProvideModePublic)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None,
                    Reason("always", kProvideModePublic, false, ProvideNetworkMode::All, 1)));
  // Always that is not public (no session behind it yet) says nothing
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None, Reason("always", kNone)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None, Reason("always", kNetwork)));
}

UR_TEST(ProviderIdlePrecedences) {
  // Auto + disconnected + paused: Auto first
  UR_EXPECT_TRUE(Is(ProviderIdleReason::AutoNotConnected,
                    Reason("auto", kNetwork, true, ProvideNetworkMode::WiFi)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::AutoNotConnected,
                    Reason("auto", kNone, true, ProvideNetworkMode::All)));
  // Network + paused: Network first
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NetworkOnly,
                    Reason("network", kNetwork, true, ProvideNetworkMode::WiFi)));
  // Never + paused: nothing
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None,
                    Reason("never", kNone, true, ProvideNetworkMode::WiFi)));
  // unknown or manual: nothing
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None,
                    Reason("manual", kProvideModePublic, true, ProvideNetworkMode::WiFi)));
  // Always + paused + WiFi, Always + paused + All
  UR_EXPECT_TRUE(Is(ProviderIdleReason::PausedWifiOnly,
                    Reason("always", kProvideModePublic, true, ProvideNetworkMode::WiFi)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::PausedNoNetwork,
                    Reason("always", kProvideModePublic, true, ProvideNetworkMode::All)));
  // Always + 0 bytes, Always + bytes
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NoTrafficYet,
                    Reason("always", kProvideModePublic, false, ProvideNetworkMode::All, 0)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None,
                    Reason("always", kProvideModePublic, false, ProvideNetworkMode::All, 2048)));
}

UR_TEST(ProviderIdleLinuxDerivesNothingWithoutATunnelSession) {
  // no session: the daemon's device, the only thing that provides, is not
  // running in any mode, so neither "Choose Always" nor "own devices" is true
  for (const char* mode : {"auto", "always", "network", "never"}) {
    for (const int64_t live : {kNone, kNetwork, kProvideModePublic}) {
      UR_EXPECT_TRUE_MSG(mode, Is(ProviderIdleReason::None,
                                  SessionIdleReasonFor(false, mode, live, false, 0)));
      UR_EXPECT_TRUE_MSG(mode, Is(ProviderIdleReason::None,
                                  SessionIdleReasonFor(false, mode, live, true, 0)));
    }
  }
  // with one, the decision above, with no Wi-Fi-only setting on the desktop
  UR_EXPECT_TRUE(Is(ProviderIdleReason::AutoNotConnected,
                    SessionIdleReasonFor(true, "auto", kNetwork, false, 0)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NetworkOnly,
                    SessionIdleReasonFor(true, "network", kNetwork, false, 0)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::NoTrafficYet,
                    SessionIdleReasonFor(true, "always", kProvideModePublic, false, 0)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::None,
                    SessionIdleReasonFor(true, "always", kProvideModePublic, false, 512)));
  UR_EXPECT_TRUE(Is(ProviderIdleReason::PausedNoNetwork,
                    SessionIdleReasonFor(true, "always", kProvideModePublic, true, 0)));
}

}  // namespace
