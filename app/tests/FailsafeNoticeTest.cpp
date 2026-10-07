// What the app says about the daemon's dead-tunnel failsafe (FailsafeNotice.hpp):
// the countdown warning only on a live tunnel, and after a failsafe stop the
// blocked or the restored line by the kill switch the daemon reports, never for
// another stop and never for a kill switch that could not be armed. And the
// tray's recovery items, each only while it is the answer to something and the
// window holds no session.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "ControlProtocol.hpp"
#include "FailsafeNotice.hpp"

namespace ctl = urnw::ctl;
namespace notice = urnw::failsafe_notice;

UR_TEST(FailsafeNotice_TheWarningShowsOnlyOnALiveTunnel) {
  UR_EXPECT_TRUE(notice::ShowsArmedWarning(true, ctl::TunnelState::Up));
  UR_EXPECT_FALSE(notice::ShowsArmedWarning(false, ctl::TunnelState::Up));
  for (auto state : {ctl::TunnelState::Stopped, ctl::TunnelState::Starting,
                     ctl::TunnelState::Stopping, ctl::TunnelState::Error}) {
    UR_EXPECT_FALSE(notice::ShowsArmedWarning(true, state));
  }
  UR_EXPECT_TRUE(std::string(notice::ArmedCopy().key) == "conn_failsafe_armed");
}

// Blocked while the armed floor holds, restored once no table does: the one
// thing the two lines disagree on is whether this machine reaches anything.
UR_TEST(FailsafeNotice_AFailsafeStopReadsByTheKillSwitch) {
  for (const char* reason : {ctl::kStopReasonFailsafeNoExit, ctl::kStopReasonFailsafeNoInbound,
                             ctl::kStopReasonFailsafeSdkUnresponsive}) {
    const auto blocked = notice::StoppedCopy(reason, ctl::KillSwitchState::Armed);
    UR_EXPECT_TRUE(blocked.has_value());
    if (blocked) UR_EXPECT_TRUE(std::string(blocked->key) == "conn_failsafe_blocked");
    const auto restored = notice::StoppedCopy(reason, ctl::KillSwitchState::Off);
    UR_EXPECT_TRUE(restored.has_value());
    if (restored) UR_EXPECT_TRUE(std::string(restored->key) == "conn_failsafe_restored");
    // Neither line is true of a floor that could not go in; the daemon's
    // sentence says what is left.
    UR_EXPECT_FALSE(notice::StoppedCopy(reason, ctl::KillSwitchState::Failed).has_value());
    UR_EXPECT_FALSE(notice::StoppedCopy(reason, ctl::KillSwitchState::Connected).has_value());
  }
}

UR_TEST(FailsafeNotice_OtherStopsKeepTheDaemonsSentence) {
  for (const char* reason :
       {"", "user", "io_loop", "start_failed", "tunnel_storm", "egress_unprotected", "orphaned"}) {
    for (auto killSwitch : {ctl::KillSwitchState::Off, ctl::KillSwitchState::Armed,
                            ctl::KillSwitchState::Failed}) {
      UR_EXPECT_FALSE(notice::StoppedCopy(reason, killSwitch).has_value());
    }
  }
}

// The English is the store's, byte for byte (CatalogLookupTest checks the
// catalog too): the restored line keeps its emphasis on NOT, and the blocked
// one its dash.
UR_TEST(FailsafeNotice_TheEnglishIsTheStores) {
  const auto blocked =
      notice::StoppedCopy(ctl::kStopReasonFailsafeNoExit, ctl::KillSwitchState::Armed);
  const auto restored =
      notice::StoppedCopy(ctl::kStopReasonFailsafeNoExit, ctl::KillSwitchState::Off);
  UR_EXPECT_TRUE(blocked && std::string(blocked->english).find("off \xe2\x80\x94 nothing") !=
                                std::string::npos);
  UR_EXPECT_TRUE(restored &&
                 std::string(restored->english).find("is NOT protected") != std::string::npos);
  UR_EXPECT_TRUE(std::string(notice::ArmedCopy().english) ==
                 "If nothing gets through shortly, URnetwork will turn the tunnel off "
                 "automatically so you keep your internet.");
}

namespace {

ctl::StatusReply StatusOf(ctl::TunnelState state, bool routesInstalled,
                          ctl::KillSwitchState killSwitch) {
  ctl::StatusReply status;
  status.tunnel_state = state;
  status.routes_installed = routesInstalled;
  status.kill_switch = killSwitch;
  return status;
}

}  // namespace

// Force off for a tunnel the daemon runs, or capture routes still in, that the
// window does not hold; with the window's own session it is the window's
// Disconnect.
UR_TEST(FailsafeNotice_TheTrayOffersForceOffForATunnelTheWindowDoesNotHold) {
  const auto up = StatusOf(ctl::TunnelState::Up, true, ctl::KillSwitchState::Connected);
  UR_EXPECT_TRUE(notice::TrayRecoveryFor(false, up).forceTunnelOff);
  UR_EXPECT_FALSE(notice::TrayRecoveryFor(false, up).liftKillSwitch);
  UR_EXPECT_TRUE(notice::TrayRecoveryFor(true, up) == notice::TrayRecovery{});
  // Routes left in under a session in error still capture the machine.
  const auto stuck = StatusOf(ctl::TunnelState::Error, true, ctl::KillSwitchState::Off);
  UR_EXPECT_TRUE(notice::TrayRecoveryFor(false, stuck).forceTunnelOff);
  for (auto state : {ctl::TunnelState::Stopped, ctl::TunnelState::Starting,
                     ctl::TunnelState::Stopping, ctl::TunnelState::Error}) {
    UR_EXPECT_FALSE(
        notice::TrayRecoveryFor(false, StatusOf(state, false, ctl::KillSwitchState::Off))
            .forceTunnelOff);
  }
}

// Lift for a floor that blocks the machine with no tunnel up: armed, or left by
// an arm that failed. Never over a live tunnel, where the floor is the leak
// floor the tunnel keeps.
UR_TEST(FailsafeNotice_TheTrayOffersTheLiftForAFloorWithNoTunnel) {
  for (auto state : {ctl::TunnelState::Stopped, ctl::TunnelState::Error}) {
    for (auto killSwitch : {ctl::KillSwitchState::Armed, ctl::KillSwitchState::Failed}) {
      const auto recovery = notice::TrayRecoveryFor(false, StatusOf(state, false, killSwitch));
      UR_EXPECT_TRUE(recovery.liftKillSwitch);
      UR_EXPECT_FALSE(recovery.forceTunnelOff);
      UR_EXPECT_FALSE(notice::TrayRecoveryFor(true, StatusOf(state, false, killSwitch))
                          .liftKillSwitch);
    }
    UR_EXPECT_FALSE(
        notice::TrayRecoveryFor(false, StatusOf(state, false, ctl::KillSwitchState::Off))
            .liftKillSwitch);
  }
  UR_EXPECT_FALSE(notice::TrayRecoveryFor(false, StatusOf(ctl::TunnelState::Up, true,
                                                          ctl::KillSwitchState::Failed))
                      .liftKillSwitch);
}

// Another user's session is theirs: nothing is offered over a redacted status.
UR_TEST(FailsafeNotice_TheTrayOffersNothingOverAnotherUsersSession) {
  ctl::StatusReply other = StatusOf(ctl::TunnelState::Up, false, ctl::KillSwitchState::Armed);
  other.redacted = true;
  UR_EXPECT_TRUE(notice::TrayRecoveryFor(false, other) == notice::TrayRecovery{});
  other.tunnel_state = ctl::TunnelState::Stopped;
  UR_EXPECT_TRUE(notice::TrayRecoveryFor(false, other) == notice::TrayRecovery{});
}

UR_TEST(FailsafeNotice_TheTrayItemsAreTheStoresKeys) {
  UR_EXPECT_TRUE(std::string(notice::TrayRecovery::ForceTunnelOffCopy().key) ==
                 "conn_tray_force_tunnel_off");
  UR_EXPECT_TRUE(std::string(notice::TrayRecovery::ForceTunnelOffCopy().english) ==
                 "Force the tunnel off (recovery)");
  UR_EXPECT_TRUE(std::string(notice::TrayRecovery::LiftKillSwitchCopy().key) ==
                 "conn_tray_lift_kill_switch");
  UR_EXPECT_TRUE(std::string(notice::TrayRecovery::LiftKillSwitchCopy().english) ==
                 "Turn off the kill switch (unblock this machine)");
}
