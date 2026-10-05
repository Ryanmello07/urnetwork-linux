// The provider lifecycle (ProvideLifecycle.hpp): connected or disconnected x
// provide mode -> does this device provide, at which tier, and what the GUI
// asks the daemon while it has no tunnel session (support inbox 1521, P008).
// Before the fix Linux provided only while connected, in every mode.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "ProvideLifecycle.hpp"

namespace provide = urnw::provide;

namespace {

struct LifecycleRow {
  const char* mode;
  bool connected;
  bool runs;
  provide::Tier tier;
};

// The sdk's DeviceLocal.applyProvideControlModeWithLock, row by row.
const LifecycleRow kLifecycle[] = {
    {"never", true, false, provide::Tier::None},
    {"never", false, false, provide::Tier::None},
    {"always", true, true, provide::Tier::Public},
    {"always", false, true, provide::Tier::Public},
    {"network", true, true, provide::Tier::Network},
    {"network", false, true, provide::Tier::Network},
    {"auto", true, true, provide::Tier::Public},
    {"auto", false, true, provide::Tier::Network},
    // never offered by this app; its explicit provide mode lives in a device
    // that does not exist while disconnected
    {"manual", true, false, provide::Tier::None},
    {"manual", false, false, provide::Tier::None},
    {"", false, false, provide::Tier::None},
    {"ALWAYS", false, false, provide::Tier::None},
};

provide::DaemonProviderFacts IdleDaemon() {
  provide::DaemonProviderFacts facts;
  facts.answered = true;
  return facts;
}

provide::DaemonProviderFacts RunningProvider(const char* mode) {
  provide::DaemonProviderFacts facts = IdleDaemon();
  facts.providerRunning = true;
  facts.ownerConnected = true;
  facts.providerControlMode = mode;
  return facts;
}

}  // namespace

UR_TEST(provideLifecycleConnectedOrNotByMode) {
  for (const LifecycleRow& row : kLifecycle) {
    const provide::ControlMode mode = provide::ControlModeFrom(row.mode);
    const std::string label = std::string(row.mode) + (row.connected ? " connected" : " disconnected");
    UR_EXPECT_TRUE_MSG(label, provide::ProviderRuns(mode, row.connected) == row.runs);
    UR_EXPECT_TRUE_MSG(label, provide::SdkTierFor(mode, row.connected) == row.tier);
  }
}

// THE DEFECT, as one assertion each: disconnecting must not end providing for
// the modes that provide while disconnected.
UR_TEST(provideLifecycleDisconnectingKeepsAlwaysNetworkAndAuto) {
  UR_EXPECT_TRUE(provide::ProviderRuns(provide::ControlMode::Always, /*connected=*/false));
  UR_EXPECT_TRUE(provide::ProviderRuns(provide::ControlMode::Network, /*connected=*/false));
  UR_EXPECT_TRUE(provide::ProviderRuns(provide::ControlMode::Auto, /*connected=*/false));
  UR_EXPECT_FALSE(provide::ProviderRuns(provide::ControlMode::Never, /*connected=*/false));
}

// Auto shares publicly only while connected and with the user's own devices
// otherwise; Always stays public.
UR_TEST(provideLifecycleTiersMatchTheSdk) {
  UR_EXPECT_EQ(3, static_cast<int64_t>(provide::Tier::Public));
  UR_EXPECT_EQ(1, static_cast<int64_t>(provide::Tier::Network));
  UR_EXPECT_EQ(0, static_cast<int64_t>(provide::Tier::None));
  UR_EXPECT_TRUE(provide::SdkTierFor(provide::ControlMode::Auto, false) ==
                 provide::Tier::Network);
  UR_EXPECT_TRUE(provide::SdkTierFor(provide::ControlMode::Auto, true) == provide::Tier::Public);
  UR_EXPECT_TRUE(provide::SdkTierFor(provide::ControlMode::Always, false) ==
                 provide::Tier::Public);
}

UR_TEST(provideLifecycleModeNamesAreTheWireStrings) {
  for (const char* mode : {"never", "always", "network", "auto"}) {
    UR_EXPECT_TRUE_MSG(mode, std::string(provide::ToString(provide::ControlModeFrom(mode))) == mode);
  }
  UR_EXPECT_TRUE(provide::ControlModeFrom("manual") == provide::ControlMode::Unknown);
  UR_EXPECT_TRUE(std::string(provide::ToString(provide::ControlMode::Unknown)) == "unknown");
}

UR_TEST(provideStepStartsTheProviderAfterDisconnectOrLaunch) {
  for (const char* mode : {"always", "network", "auto"}) {
    UR_EXPECT_TRUE_MSG(mode, provide::DisconnectedProviderStep(mode, IdleDaemon()) ==
                                 provide::DisconnectedStep::Start);
  }
  for (const char* mode : {"never", "manual", ""}) {
    UR_EXPECT_TRUE_MSG(mode, provide::DisconnectedProviderStep(mode, IdleDaemon()) ==
                                 provide::DisconnectedStep::None);
  }
}

UR_TEST(provideStepNeverStopsARunningProvider) {
  UR_EXPECT_TRUE(provide::DisconnectedProviderStep("never", RunningProvider("always")) ==
                 provide::DisconnectedStep::Stop);
  UR_EXPECT_TRUE(provide::DisconnectedProviderStep("manual", RunningProvider("auto")) ==
                 provide::DisconnectedStep::Stop);
}

UR_TEST(provideStepAppliesAChangedModeAndKeepsAMatchingOne) {
  UR_EXPECT_TRUE(provide::DisconnectedProviderStep("auto", RunningProvider("always")) ==
                 provide::DisconnectedStep::Apply);
  UR_EXPECT_TRUE(provide::DisconnectedProviderStep("always", RunningProvider("always")) ==
                 provide::DisconnectedStep::None);
  // an edited provider policy needs a new device
  UR_EXPECT_TRUE(provide::DisconnectedProviderStep("always", RunningProvider("always"),
                                                   /*settingsChanged=*/true) ==
                 provide::DisconnectedStep::Start);
}

// A relaunched GUI finds the provider its previous run left with no owner and
// adopts it with an idempotent start, so the session is owned again.
UR_TEST(provideStepAdoptsAnOwnerlessProvider) {
  provide::DaemonProviderFacts facts = RunningProvider("always");
  facts.ownerConnected = false;
  UR_EXPECT_TRUE(provide::DisconnectedProviderStep("always", facts) ==
                 provide::DisconnectedStep::Start);
}

// Never beside a tunnel session (its device provides), never on another user's
// session, never on no answer, and not while the kill switch holds the machine.
UR_TEST(provideStepLeavesTunnelsOtherUsersAndTheArmedFloorAlone) {
  provide::DaemonProviderFacts tunnel = IdleDaemon();
  tunnel.tunnelSession = true;
  provide::DaemonProviderFacts foreign = IdleDaemon();
  foreign.redacted = true;
  provide::DaemonProviderFacts unknown;
  provide::DaemonProviderFacts armed = IdleDaemon();
  armed.killSwitchArmed = true;
  for (const char* mode : {"always", "network", "auto", "never"}) {
    UR_EXPECT_TRUE_MSG(mode, provide::DisconnectedProviderStep(mode, tunnel) ==
                                 provide::DisconnectedStep::None);
    UR_EXPECT_TRUE_MSG(mode, provide::DisconnectedProviderStep(mode, foreign) ==
                                 provide::DisconnectedStep::None);
    UR_EXPECT_TRUE_MSG(mode, provide::DisconnectedProviderStep(mode, unknown) ==
                                 provide::DisconnectedStep::None);
    UR_EXPECT_TRUE_MSG(mode, provide::DisconnectedProviderStep(mode, armed) ==
                                 provide::DisconnectedStep::None);
  }
}

UR_TEST(provideStepBackoffDoublesToFiveMinutesAndResets) {
  provide::ProviderStepBackoff backoff;
  UR_EXPECT_TRUE(backoff.Allows(0));
  backoff.NoteFailure(1000);
  UR_EXPECT_EQ(5000, backoff.DelayMillis());
  UR_EXPECT_FALSE(backoff.Allows(5999));
  UR_EXPECT_TRUE(backoff.Allows(6000));
  backoff.NoteFailure(6000);
  UR_EXPECT_EQ(10000, backoff.DelayMillis());
  UR_EXPECT_FALSE(backoff.Allows(15999));
  for (int i = 0; i < 20; ++i) backoff.NoteFailure(20000);
  UR_EXPECT_EQ(provide::ProviderStepBackoff::kMaxDelayMillis, backoff.DelayMillis());
  backoff.NoteSuccess();
  UR_EXPECT_EQ(0, backoff.DelayMillis());
  UR_EXPECT_TRUE(backoff.Allows(20000));
}
