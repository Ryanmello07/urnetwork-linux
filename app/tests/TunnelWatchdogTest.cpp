// The dead-tunnel failsafe's decision (TunnelWatchdog.hpp): the verdict, its
// carrying veto, the countdown it reports, the freeze rebase and the
// destination clocks, ticked once a simulated second as the daemon's reaper
// ticks it.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <string>

#include "ControlProtocol.hpp"
#include "TunnelWatchdog.hpp"

namespace wd = urnw::watchdog;
namespace ctl = urnw::ctl;

namespace {

// The defaults every rule below is judged against, read where they are set.
const wd::DeadTunnelSettings kSettings{};

// A session as the reaper sees it: a clock, the tun's counters and the
// sampler's readings, advanced one tick at a time, under `settings`.
struct Session {
  wd::DeadTunnelWatch watch;
  wd::WatchInputs in;

  explicit Session(int64_t startMillis = 1000000,
                   const wd::DeadTunnelSettings& settings = kSettings)
      : watch(settings) {
    in.nowMillis = startMillis;
    in.tunnelUp = true;
    in.routesInstalled = true;
    watch.Start(in);
  }

  // The sampler completed a sample now, with `proven` proven exits.
  void Sample(int64_t proven) {
    in.provenCount = proven;
    in.lastSampleMillis = in.nowMillis;
    if (proven > 0) in.lastProvenMillis = in.nowMillis;
  }

  wd::WatchTick Tick(int64_t stepMillis = kSettings.evaluateIntervalMillis) {
    in.nowMillis += stepMillis;
    return watch.Tick(in);
  }

  // Ticks until a verdict or `millis` have passed; the elapsed time at the
  // verdict, or -1 for none.
  int64_t RunUntilDead(int64_t millis, bool sampling, int64_t proven, uint64_t outPerTick,
                       uint64_t inPerTick) {
    for (int64_t elapsed = kSettings.evaluateIntervalMillis; elapsed <= millis;
         elapsed += kSettings.evaluateIntervalMillis) {
      in.outboundPackets += outPerTick;
      in.inboundPackets += inPerTick;
      const wd::WatchTick tick = Tick();
      if (sampling && (elapsed % kSettings.sdkSampleIntervalMillis) == 0) Sample(proven);
      if (tick.verdict.reason != wd::DeadTunnelReason::None) return elapsed;
    }
    return -1;
  }
};

}  // namespace

// The numbers the windows were chosen from, pinned: every settings default,
// and the freeze bar's as five evaluate ticks of the default instance.
UR_TEST(TunnelWatchdog_ThresholdsArePinned) {
  UR_EXPECT_EQ(20000, kSettings.deadFastMillis);
  UR_EXPECT_EQ(8u, kSettings.deadFastOutboundPackets);
  UR_EXPECT_EQ(90000, kSettings.deadSlowMillis);
  UR_EXPECT_EQ(30000, kSettings.sdkUnresponsiveMillis);
  UR_EXPECT_EQ(10000, kSettings.sdkUnresponsiveArmMillis);
  UR_EXPECT_EQ(30000, kSettings.failsafeNoticeMillis);
  UR_EXPECT_EQ(2000, kSettings.sdkSampleIntervalMillis);
  UR_EXPECT_EQ(1000, kSettings.evaluateIntervalMillis);
  UR_EXPECT_EQ(5 * kSettings.evaluateIntervalMillis, kSettings.evaluatorFrozenMillis);
  UR_EXPECT_EQ(5000, kSettings.evaluatorFrozenMillis);
  UR_EXPECT_EQ(kSettings.evaluatorFrozenMillis, kSettings.offlineRebaseMillis);
  // An instance initialized with another evaluate interval freezes at five of
  // its own ticks, and rebases offline on the same bar.
  const wd::DeadTunnelSettings halfSecond{500};
  UR_EXPECT_EQ(2500, halfSecond.evaluatorFrozenMillis);
  UR_EXPECT_EQ(2500, halfSecond.offlineRebaseMillis);
}

UR_TEST(TunnelWatchdog_StopReasonsAreTheWireSpellings) {
  UR_EXPECT_TRUE(std::string(wd::StopReasonOf(wd::DeadTunnelReason::None)).empty());
  UR_EXPECT_TRUE(std::string(wd::StopReasonOf(wd::DeadTunnelReason::NoExit)) ==
                 "failsafe_no_exit");
  UR_EXPECT_TRUE(std::string(wd::StopReasonOf(wd::DeadTunnelReason::NoInbound)) ==
                 "failsafe_no_inbound");
  UR_EXPECT_TRUE(std::string(wd::StopReasonOf(wd::DeadTunnelReason::SdkUnresponsive)) ==
                 "failsafe_sdk_unresponsive");
  for (auto reason : {wd::DeadTunnelReason::NoExit, wd::DeadTunnelReason::NoInbound,
                      wd::DeadTunnelReason::SdkUnresponsive}) {
    UR_EXPECT_TRUE(ctl::IsFailsafeStop(wd::StopReasonOf(reason)));
  }
  for (const char* other : {"", "user", "io_loop", "tunnel_storm", "egress_unprotected"}) {
    UR_EXPECT_FALSE(ctl::IsFailsafeStop(other));
  }
}

// A tunnel that carries is never ended: ten minutes of traffic both ways with
// proven exits, and ten more with the SDK silent and no exit proven, because a
// packet back outranks every claim.
UR_TEST(TunnelWatchdog_FlowingTrafficIsNeverDead) {
  Session working;
  working.Sample(2);
  UR_EXPECT_EQ(-1, working.RunUntilDead(600000, /*sampling=*/true, /*proven=*/2, 50, 40));

  Session vetoed;
  UR_EXPECT_EQ(-1, vetoed.RunUntilDead(600000, /*sampling=*/false, /*proven=*/0, 50, 1));
  Session unproven;
  unproven.Sample(0);
  UR_EXPECT_EQ(-1, unproven.RunUntilDead(600000, /*sampling=*/true, /*proven=*/0, 50, 1));
}

// An idle machine sends nothing, and an idle blocked machine looks like an idle
// working one: NoInbound never fires, and with an exit proven nothing does.
UR_TEST(TunnelWatchdog_IdleNeverTripsTheFastRule) {
  Session idle;
  idle.Sample(1);
  UR_EXPECT_EQ(-1, idle.RunUntilDead(600000, /*sampling=*/true, /*proven=*/1, 0, 0));
  // Seven packets in is below the commitment.
  Session trickle;
  trickle.Sample(0);
  trickle.in.outboundPackets = 7;
  const int64_t at = trickle.RunUntilDead(600000, /*sampling=*/true, /*proven=*/0, 0, 0);
  // ...so only NoExit ends it, at its own window.
  UR_EXPECT_EQ(kSettings.deadSlowMillis, at);
}

UR_TEST(TunnelWatchdog_NoInboundFiresAtTwentySeconds) {
  Session dead;
  dead.Sample(0);
  int64_t at = -1;
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  for (int64_t elapsed = 1000; elapsed <= 60000; elapsed += 1000) {
    dead.in.outboundPackets += 10;
    const wd::WatchTick tick = dead.Tick();
    if (elapsed % 2000 == 0) dead.Sample(0);
    if (tick.verdict.reason != wd::DeadTunnelReason::None) {
      at = elapsed;
      reason = tick.verdict.reason;
      break;
    }
  }
  UR_EXPECT_EQ(kSettings.deadFastMillis, at);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoInbound);
}

// One packet back resets the window and the commitment at once.
UR_TEST(TunnelWatchdog_OneInboundPacketResetsTheFastWindow) {
  Session session;
  session.Sample(0);
  for (int i = 0; i < 15; ++i) {
    session.in.outboundPackets += 10;
    session.Tick();
  }
  session.in.inboundPackets += 1;
  session.Tick();
  for (int i = 0; i < 19; ++i) {
    session.in.outboundPackets += 10;
    const wd::WatchTick tick = session.Tick();
    if (i % 2 == 0) session.Sample(0);
    UR_EXPECT_TRUE(tick.verdict.reason == wd::DeadTunnelReason::None);
  }
  session.in.outboundPackets += 10;
  UR_EXPECT_TRUE(session.Tick().verdict.reason == wd::DeadTunnelReason::NoInbound);
}

// A proven exit holds NoInbound off: the SDK says something carries.
UR_TEST(TunnelWatchdog_AProvenExitHoldsTheFastRuleOff) {
  Session session;
  session.Sample(1);
  UR_EXPECT_EQ(-1, session.RunUntilDead(120000, /*sampling=*/true, /*proven=*/1, 10, 0));
}

// The SDK silent with nothing coming back: SdkUnresponsive at 30 s, ahead of
// the others even when their conditions hold too.
UR_TEST(TunnelWatchdog_AStaleSampleFiresUnresponsiveAndOutranks) {
  Session silent;
  const int64_t at = silent.RunUntilDead(120000, /*sampling=*/false, 0, 0, 0);
  UR_EXPECT_EQ(kSettings.sdkUnresponsiveMillis, at);

  // One sample at the start, then silence, with traffic in and nothing back:
  // the stale reading still claims a proven exit, which holds NoInbound off,
  // and the silence ends the session at its own window.
  Session wedged;
  wedged.Sample(1);  // proven: NoInbound held off
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  int64_t elapsed = 0;
  while (reason == wd::DeadTunnelReason::None && elapsed < 120000) {
    wedged.in.outboundPackets += 10;
    reason = wedged.Tick().verdict.reason;
    elapsed += 1000;
  }
  UR_EXPECT_EQ(kSettings.sdkUnresponsiveMillis, elapsed);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::SdkUnresponsive);

  // Every rule's condition at once: unresponsive wins.
  wd::DeadTunnelSignals all;
  all.tunnelUp = true;
  all.routesInstalled = true;
  all.upSinceMillis = 1000;
  all.lastSampleMillis = 1000;
  all.outboundSinceInbound = 100;
  const wd::DeadTunnelVerdict verdict =
      wd::Evaluate(all, 1000 + kSettings.deadSlowMillis, kSettings);
  UR_EXPECT_TRUE(verdict.reason == wd::DeadTunnelReason::SdkUnresponsive);
}

// NoExit needs a sampler that answered: never having a reading is
// SdkUnresponsive's question.
UR_TEST(TunnelWatchdog_NoExitNeedsAnAnsweringSampler) {
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  s.lastSampleMillis = 1000 + kSettings.deadSlowMillis - 1000;  // answering, nothing proven
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + kSettings.deadSlowMillis, kSettings).reason ==
                 wd::DeadTunnelReason::NoExit);
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + kSettings.deadSlowMillis - 1, kSettings).reason ==
                 wd::DeadTunnelReason::None);
  s.lastProvenMillis = 1000 + kSettings.deadSlowMillis - 5000;
  s.provenCount = 1;
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + kSettings.deadSlowMillis, kSettings).reason ==
                 wd::DeadTunnelReason::None);
  // Never answered: the SDK's silence is the reason, not the missing exit.
  s.provenCount = 0;
  s.lastProvenMillis = -1;
  s.lastSampleMillis = -1;
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + kSettings.deadSlowMillis, kSettings).reason ==
                 wd::DeadTunnelReason::SdkUnresponsive);
}

// The veto beats NoExit and SdkUnresponsive, and lapses after 20 s of silence.
UR_TEST(TunnelWatchdog_TheCarryingVetoBeatsEveryClaim) {
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  s.lastSampleMillis = -1;  // never answered
  const int64_t now = 1000 + 200000;
  s.lastInboundMillis = now - kSettings.deadFastMillis + 1;
  const wd::DeadTunnelVerdict vetoed = wd::Evaluate(s, now, kSettings);
  UR_EXPECT_TRUE(vetoed.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_FALSE(vetoed.armed);
  s.lastInboundMillis = now - kSettings.deadFastMillis;
  UR_EXPECT_TRUE(wd::Evaluate(s, now, kSettings).reason == wd::DeadTunnelReason::SdkUnresponsive);
}

// Nothing is judged unless the machine is pointed at a tunnel.
UR_TEST(TunnelWatchdog_OnlyAnUpTunnelWithRoutesIsJudged) {
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  const int64_t late = 1000 + 10 * kSettings.deadSlowMillis;
  UR_EXPECT_TRUE(wd::Evaluate(s, late, kSettings).reason != wd::DeadTunnelReason::None);
  s.routesInstalled = false;
  UR_EXPECT_TRUE(wd::Evaluate(s, late, kSettings).reason == wd::DeadTunnelReason::None);
  s.routesInstalled = true;
  s.tunnelUp = false;
  UR_EXPECT_TRUE(wd::Evaluate(s, late, kSettings).reason == wd::DeadTunnelReason::None);
  s.tunnelUp = true;
  s.upSinceMillis = 0;
  UR_EXPECT_TRUE(wd::Evaluate(s, late, kSettings).reason == wd::DeadTunnelReason::None);
}

// The countdown shows within 30 s of a teardown and not before, so the app
// warns first and never cries wolf.
UR_TEST(TunnelWatchdog_TheCountdownIsReportedInTheLastThirtySeconds) {
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  // NoExit's countdown: the sampler answers, nothing proven.
  s.lastSampleMillis = 1000 + 50000;
  wd::DeadTunnelVerdict v = wd::Evaluate(s, 1000 + 50000, kSettings);
  UR_EXPECT_FALSE(v.armed);  // 40 s to go
  s.lastSampleMillis = 1000 + 60000;
  v = wd::Evaluate(s, 1000 + 60000, kSettings);
  UR_EXPECT_TRUE(v.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(30000, v.millisToFailsafe);
  // NoInbound's, which is the sooner: traffic in since a traffic clock that
  // began 15 s ago, nothing ever back.
  s.trafficStartMillis = 1000 + 45000;
  s.outboundSinceInbound = 8;
  v = wd::Evaluate(s, 1000 + 60000, kSettings);
  UR_EXPECT_TRUE(v.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(5000, v.millisToFailsafe);
  // A proven exit: no countdown at all.
  s.provenCount = 1;
  s.lastProvenMillis = 1000 + 60000;
  v = wd::Evaluate(s, 1000 + 60000, kSettings);
  UR_EXPECT_FALSE(v.armed);
  UR_EXPECT_EQ(0, v.millisToFailsafe);
  // The unresponsive countdown shows only once a sample is 10 s overdue.
  wd::DeadTunnelSignals quiet = s;
  quiet.lastSampleMillis = 1000 + 60000 - kSettings.sdkUnresponsiveArmMillis + 1;
  UR_EXPECT_FALSE(wd::Evaluate(quiet, 1000 + 60000, kSettings).armed);
  quiet.lastSampleMillis = 1000 + 60000 - kSettings.sdkUnresponsiveArmMillis;
  v = wd::Evaluate(quiet, 1000 + 60000, kSettings);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(kSettings.sdkUnresponsiveMillis - kSettings.sdkUnresponsiveArmMillis,
               v.millisToFailsafe);
}

// A tick five or more intervals late means nothing watched: every window is
// rebased, no verdict is reached on it, and samples from before it count for
// nothing. Without the rebase the first tick after a night's suspend would end
// the session for an SDK nobody had asked anything for eight hours.
UR_TEST(TunnelWatchdog_ASuspendGapRebasesTheSession) {
  Session session;
  session.Sample(1);
  for (int i = 1; i <= 10; ++i) {
    session.in.outboundPackets += 10;
    session.in.inboundPackets += 5;
    session.Tick();
    if (i % 2 == 0) session.Sample(1);
  }
  const wd::WatchTick resumed = session.Tick(8LL * 3600 * 1000);
  UR_EXPECT_TRUE(resumed.froze);
  UR_EXPECT_TRUE(resumed.verdict.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_EQ(session.in.nowMillis, session.watch.sessionStartMillis());
  // The SDK silent after the resume: judged from the resume, so ended a full
  // window later, never on the stale sample.
  for (int i = 1; i < 30; ++i) {
    const wd::WatchTick tick = session.Tick();
    UR_EXPECT_FALSE(tick.froze);
    UR_EXPECT_TRUE(tick.verdict.reason == wd::DeadTunnelReason::None);
  }
  UR_EXPECT_TRUE(session.Tick().verdict.reason == wd::DeadTunnelReason::SdkUnresponsive);
  UR_EXPECT_TRUE(wd::EvaluatorFroze(kSettings.evaluatorFrozenMillis, kSettings));
  UR_EXPECT_FALSE(wd::EvaluatorFroze(kSettings.evaluatorFrozenMillis - 1, kSettings));
}

UR_TEST(TunnelWatchdog_StampsFromBeforeTheSessionAreNever) {
  UR_EXPECT_EQ(-1, wd::StampInSession(999, 1000));
  UR_EXPECT_EQ(1000, wd::StampInSession(1000, 1000));
  UR_EXPECT_EQ(-1, wd::StampInSession(-1, 1000));
}

// A new destination generation restarts every clock, and NoInbound waits for
// its window to form: an honest 27 s formation is not a dead tunnel.
UR_TEST(TunnelWatchdog_ANewDestinationGetsItsOwnClocks) {
  Session session;
  session.Sample(1);
  for (int i = 0; i < 10; ++i) session.Tick();
  // The user picks another location: generation 1, not yet formed, nothing
  // proven, traffic in and nothing back.
  session.in.connectionGeneration = 1;
  session.in.providerWindowMinSatisfied = false;
  session.Sample(0);
  session.in.outboundPackets += 10;
  wd::WatchTick tick = session.Tick();
  UR_EXPECT_TRUE(tick.verdictClockReset);
  UR_EXPECT_TRUE(tick.trafficClockReset);
  for (int i = 1; i < 27; ++i) {
    session.in.outboundPackets += 10;
    if (i % 2 == 0) session.Sample(0);
    tick = session.Tick();
    UR_EXPECT_TRUE(tick.verdict.reason == wd::DeadTunnelReason::None);
  }
  // Formed: the traffic clock starts now, and only now.
  session.in.providerWindowMinSatisfied = true;
  session.in.outboundPackets += 10;
  tick = session.Tick();
  UR_EXPECT_FALSE(tick.verdictClockReset);
  UR_EXPECT_TRUE(tick.trafficClockReset);
  int64_t elapsed = 0;
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  while (reason == wd::DeadTunnelReason::None && elapsed < 60000) {
    session.in.outboundPackets += 10;
    session.Sample(0);
    reason = session.Tick().verdict.reason;
    elapsed += 1000;
  }
  UR_EXPECT_EQ(kSettings.deadFastMillis, elapsed);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoInbound);
}

// The daemon starts the watch before the sampler has read the window, so the
// first sample's generation is the session's own, never a new destination: no
// clock restarts on it. NoInbound waits for it, and then judges from the up
// edge. Taking the generation as 0 until then would restart every clock at the
// first sample and hand a dead tunnel the time it took.
UR_TEST(TunnelWatchdog_TheFirstSampleIsTheBaseline) {
  wd::DeadTunnelWatch watch(kSettings);
  wd::WatchInputs in;
  in.nowMillis = 1000000;
  in.tunnelUp = true;
  in.routesInstalled = true;
  in.windowSampled = false;
  watch.Start(in);
  // Before the first sample, traffic in and nothing back is not judged.
  for (int i = 0; i < 3; ++i) {
    in.nowMillis += kSettings.evaluateIntervalMillis;
    in.outboundPackets += 10;
    const wd::WatchTick tick = watch.Tick(in);
    UR_EXPECT_TRUE(tick.verdict.reason == wd::DeadTunnelReason::None);
    UR_EXPECT_FALSE(tick.verdict.armed);
  }
  // The first sample: generation 7, formed, nothing proven.
  in.connectionGeneration = 7;
  in.providerWindowMinSatisfied = true;
  in.windowSampled = true;
  in.lastSampleMillis = in.nowMillis;
  int64_t elapsed = 3000;
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  bool reset = false;
  while (reason == wd::DeadTunnelReason::None && elapsed < 60000) {
    in.nowMillis += kSettings.evaluateIntervalMillis;
    in.outboundPackets += 10;
    in.lastSampleMillis = in.nowMillis;
    const wd::WatchTick tick = watch.Tick(in);
    reset = reset || tick.verdictClockReset || tick.trafficClockReset;
    reason = tick.verdict.reason;
    elapsed += kSettings.evaluateIntervalMillis;
  }
  UR_EXPECT_FALSE(reset);
  UR_EXPECT_EQ(kSettings.deadFastMillis, elapsed);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoInbound);
  UR_EXPECT_EQ(1000000, watch.sessionStartMillis());
}

// Readiness is one-way in a generation, and an older generation is a late
// reading of a retired window.
UR_TEST(TunnelWatchdog_TheEpochTrackerIgnoresChurnAndStragglers) {
  wd::ConnectionEpochTracker epoch;
  UR_EXPECT_FALSE(epoch.FastVerdictEligible());
  epoch.Observe(3, true);
  UR_EXPECT_TRUE(epoch.FastVerdictEligible());
  wd::ConnectionEpochUpdate update = epoch.Observe(3, false);  // churn
  UR_EXPECT_FALSE(update.verdictClockReset || update.trafficClockReset);
  UR_EXPECT_TRUE(epoch.FastVerdictEligible());
  update = epoch.Observe(2, false);  // a straggler
  UR_EXPECT_FALSE(update.verdictClockReset || update.trafficClockReset);
  UR_EXPECT_EQ(3, epoch.connectionGeneration());
  update = epoch.Observe(4, false);
  UR_EXPECT_TRUE(update.verdictClockReset && update.trafficClockReset);
  UR_EXPECT_FALSE(epoch.FastVerdictEligible());
  update = epoch.Observe(4, true);
  UR_EXPECT_TRUE(!update.verdictClockReset && update.trafficClockReset);
  UR_EXPECT_TRUE(epoch.FastVerdictEligible());
}

UR_TEST(TunnelWatchdog_TheTrafficTrackerMeasuresSinceTheLastPacketBack) {
  wd::TrafficTracker traffic;
  traffic.Reset(100, 50);
  UR_EXPECT_EQ(-1, traffic.lastInboundMillis());
  traffic.Observe(110, 50, 1000);
  UR_EXPECT_EQ(10u, traffic.outboundSinceInbound());
  traffic.Observe(120, 51, 2000);
  UR_EXPECT_EQ(2000, traffic.lastInboundMillis());
  UR_EXPECT_EQ(0u, traffic.outboundSinceInbound());
  // A counter that reads lower (a failed read reads 0) is no evidence.
  traffic.Observe(0, 0, 3000);
  UR_EXPECT_EQ(0u, traffic.outboundSinceInbound());
  UR_EXPECT_EQ(2000, traffic.lastInboundMillis());
  traffic.Observe(130, 51, 4000);
  UR_EXPECT_EQ(10u, traffic.outboundSinceInbound());
}

// ---- a machine with no network ------------------------------------------------

namespace {

// Ticks `session` once a second for `millis`, the SDK answering every two
// seconds with `proven` exits, `outPerTick` packets in and `inPerTick` back,
// offline whenever `offlineAt(elapsed)` says. The elapsed time at the first
// verdict (into *reason), or -1; *rebases counts the returns that rebased.
template <typename OfflineAt>
int64_t RunWithNetwork(Session& session, int64_t millis, int64_t proven, uint64_t outPerTick,
                       uint64_t inPerTick, OfflineAt offlineAt, wd::DeadTunnelReason* reason,
                       int* rebases = nullptr) {
  for (int64_t elapsed = kSettings.evaluateIntervalMillis; elapsed <= millis;
       elapsed += kSettings.evaluateIntervalMillis) {
    session.in.outboundPackets += outPerTick;
    session.in.inboundPackets += inPerTick;
    session.in.offline = offlineAt(elapsed);
    const wd::WatchTick tick = session.Tick();
    if (rebases != nullptr && tick.offlineStep == wd::OfflineStep::BackRebased) ++*rebases;
    if ((elapsed % kSettings.sdkSampleIntervalMillis) == 0) session.Sample(proven);
    if (tick.verdict.reason != wd::DeadTunnelReason::None) {
      if (reason != nullptr) *reason = tick.verdict.reason;
      return elapsed;
    }
  }
  return -1;
}

}  // namespace

// What each tick's network state does: hold while offline, and on the way back
// rebase after a real stretch, never after a blip.
UR_TEST(TunnelWatchdog_TheOfflineStepTable) {
  struct Row {
    bool wasOffline;
    bool offline;
    int64_t stretchMillis;
    wd::OfflineStep step;
  };
  const Row rows[] = {
      {false, false, 0, wd::OfflineStep::Online},
      {false, false, 600000, wd::OfflineStep::Online},  // a stale stretch means nothing
      {false, true, 0, wd::OfflineStep::Offline},        // the first offline tick
      {true, true, 600000, wd::OfflineStep::Offline},
      {true, false, 1000, wd::OfflineStep::BackFromBlip},
      {true, false, kSettings.offlineRebaseMillis - 1, wd::OfflineStep::BackFromBlip},
      {true, false, kSettings.offlineRebaseMillis, wd::OfflineStep::BackRebased},
      {true, false, 600000, wd::OfflineStep::BackRebased},
  };
  int index = 0;
  for (const Row& row : rows) {
    if (wd::StepOffline(row.wasOffline, row.offline, row.stretchMillis, kSettings) != row.step) {
      UR_FAIL("offline step row " + std::to_string(index) + " read wrong");
    }
    ++index;
  }
}

// Minutes with no network, apps retrying into the tunnel the whole time: no
// stop and no countdown while offline. Back on a network, every window starts
// again, so the session gets the full window to prove an exit there.
UR_TEST(TunnelWatchdog_MinutesOfflineNeverStopTheSessionAndTheReturnGetsAFreshWindow) {
  const auto online = [](int64_t) { return false; };
  for (const bool retrying : {false, true}) {
    Session session;
    session.Sample(0);
    wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
    // A minute online, idle, with nothing proven: inside NoExit's window.
    UR_EXPECT_EQ(-1, RunWithNetwork(session, 60000, 0, 0, 0, online, &reason));
    // Five minutes offline, with apps retrying into the tunnel throughout.
    for (int i = 0; i < 300; ++i) {
      session.in.outboundPackets += 10;
      session.in.offline = true;
      const wd::WatchTick tick = session.Tick();
      if (i % 2 == 1) session.Sample(0);
      UR_EXPECT_TRUE(tick.verdict.reason == wd::DeadTunnelReason::None);
      UR_EXPECT_FALSE(tick.verdict.armed);
      UR_EXPECT_TRUE(tick.offlineStep == wd::OfflineStep::Offline);
      UR_EXPECT_EQ(i == 0, tick.wentOffline);
    }
    // Back: the return rebases, and each rule fires its own full window later.
    int rebases = 0;
    const int64_t at = RunWithNetwork(session, 200000, 0, retrying ? 10 : 0, 0, online, &reason,
                                      &rebases);
    UR_EXPECT_EQ(1, rebases);
    if (retrying) {
      UR_EXPECT_EQ(kSettings.evaluateIntervalMillis + kSettings.deadFastMillis, at);
      UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoInbound);
    } else {
      UR_EXPECT_EQ(kSettings.evaluateIntervalMillis + kSettings.deadSlowMillis, at);
      UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoExit);
    }
  }
}

// A blip shorter than the rebase stretch rebases nothing: only its seconds
// are left out, so NoExit fires after exactly ninety seconds of ONLINE time.
UR_TEST(TunnelWatchdog_ABlipIsLeftOutNotRebased) {
  Session session;
  session.Sample(0);
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  int rebases = 0;
  // Offline readings at 61, 62 and 63 s: the three seconds up to 64 s are left out.
  const auto blip = [](int64_t elapsed) { return elapsed > 60000 && elapsed <= 63000; };
  const int64_t at = RunWithNetwork(session, 200000, 0, 0, 0, blip, &reason, &rebases);
  UR_EXPECT_EQ(0, rebases);
  UR_EXPECT_EQ(kSettings.deadSlowMillis + 3000, at);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoExit);
}

// Being offline never hides a dead tunnel while online: a link that drops for
// two seconds out of every twenty never rebases, and the tunnel is stopped
// once it has spent ninety seconds online without a proven exit.
UR_TEST(TunnelWatchdog_AFlappingLinkCannotKeepADeadTunnelAlive) {
  Session session;
  session.Sample(0);
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  int rebases = 0;
  const auto flapping = [](int64_t elapsed) {
    return elapsed % 20000 == 1000 || elapsed % 20000 == 2000;
  };
  const int64_t at = RunWithNetwork(session, 300000, 0, 0, 0, flapping, &reason, &rebases);
  UR_EXPECT_EQ(0, rebases);
  // Five blips of two seconds each by then: 100 s elapsed, 90 s of it online.
  UR_EXPECT_EQ(100000, at);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoExit);
}

// SdkUnresponsive is not about the network: an SDK that stops answering while
// the machine is offline is still a wedged SDK, at its own window.
UR_TEST(TunnelWatchdog_OfflineStillWatchesTheSdk) {
  Session session;
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  int64_t elapsed = 0;
  while (reason == wd::DeadTunnelReason::None && elapsed < 120000) {
    session.in.offline = true;
    reason = session.Tick().verdict.reason;
    elapsed += kSettings.evaluateIntervalMillis;
  }
  UR_EXPECT_EQ(kSettings.sdkUnresponsiveMillis, elapsed);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::SdkUnresponsive);
}

// The verdict's half: offline holds the no-exit rules and their countdowns,
// their windows leave the offline time out, and the veto reads the raw age, so
// offline time never stretches it.
UR_TEST(TunnelWatchdog_TheVerdictHoldsTheNoExitRulesOffline) {
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  const int64_t now = 1000 + 200000;
  s.lastSampleMillis = now - 1000;  // answering
  s.outboundSinceInbound = 100;
  UR_EXPECT_TRUE(wd::Evaluate(s, now, kSettings).reason == wd::DeadTunnelReason::NoInbound);
  s.offline = true;
  wd::DeadTunnelVerdict v = wd::Evaluate(s, now, kSettings);
  UR_EXPECT_TRUE(v.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_FALSE(v.armed);
  // Online again with all but five seconds of both windows spent offline: the
  // fast countdown has fifteen seconds to run.
  s.offline = false;
  s.provenOfflineMillis = 195000;
  s.inboundOfflineMillis = 195000;
  v = wd::Evaluate(s, now, kSettings);
  UR_EXPECT_TRUE(v.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(kSettings.deadFastMillis - 5000, v.millisToFailsafe);
  // NoExit reads its window the same way.
  s.outboundSinceInbound = 0;
  s.provenOfflineMillis = 200000 - kSettings.deadSlowMillis;
  UR_EXPECT_TRUE(wd::Evaluate(s, now, kSettings).reason == wd::DeadTunnelReason::NoExit);
  s.provenOfflineMillis += 1;
  UR_EXPECT_TRUE(wd::Evaluate(s, now, kSettings).reason == wd::DeadTunnelReason::None);
  // A packet back 25 s ago, 20 s of it offline: the veto has lapsed, and a
  // silent SDK is judged.
  wd::DeadTunnelSignals quiet;
  quiet.tunnelUp = true;
  quiet.routesInstalled = true;
  quiet.upSinceMillis = 1000;
  quiet.lastInboundMillis = now - 25000;
  quiet.inboundOfflineMillis = 20000;
  quiet.offline = true;
  UR_EXPECT_TRUE(wd::Evaluate(quiet, now, kSettings).reason ==
                 wd::DeadTunnelReason::SdkUnresponsive);
}

// Every rule timing comes from the settings the watch was built with: shorter
// windows fire sooner, the veto's window is NoInbound's, a shorter rebase
// stretch rebases after a blip and a longer one turns a long stretch into a
// blip whose time is only left out.
UR_TEST(TunnelWatchdog_NonDefaultSettingsAreHonoured) {
  wd::DeadTunnelSettings quick;
  quick.deadFastMillis = 5000;
  quick.deadFastOutboundPackets = 2;
  quick.deadSlowMillis = 30000;
  Session idle(1000000, quick);
  idle.Sample(0);
  UR_EXPECT_EQ(quick.deadSlowMillis, idle.RunUntilDead(120000, /*sampling=*/true, 0, 0, 0));
  Session sending(1000000, quick);
  sending.Sample(0);
  UR_EXPECT_EQ(quick.deadFastMillis, sending.RunUntilDead(120000, /*sampling=*/true, 0, 2, 0));

  // A packet back 6 s ago: still carrying by the defaults, no longer by these.
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  const int64_t now = 1000 + 100000;
  s.lastInboundMillis = now - 6000;
  UR_EXPECT_TRUE(wd::Evaluate(s, now, kSettings).reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(wd::Evaluate(s, now, quick).reason == wd::DeadTunnelReason::SdkUnresponsive);

  wd::DeadTunnelSettings eager;
  eager.offlineRebaseMillis = 1000;
  UR_EXPECT_TRUE(wd::StepOffline(true, false, 2000, eager) == wd::OfflineStep::BackRebased);
  UR_EXPECT_TRUE(wd::StepOffline(true, false, 2000, kSettings) == wd::OfflineStep::BackFromBlip);

  wd::DeadTunnelSettings patient;
  patient.offlineRebaseMillis = 600000;
  Session session(1000000, patient);
  session.Sample(0);
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  int rebases = 0;
  const auto halfMinute = [](int64_t elapsed) { return elapsed > 10000 && elapsed <= 40000; };
  const int64_t at = RunWithNetwork(session, 300000, 0, 0, 0, halfMinute, &reason, &rebases);
  UR_EXPECT_EQ(0, rebases);
  UR_EXPECT_EQ(patient.deadSlowMillis + 30000, at);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoExit);
}

// The SDK rule's window, its countdown's arming bar, the notice, the freeze bar
// and the sampler's interval come from the settings too.
UR_TEST(TunnelWatchdog_TheSdkAndCadenceTimingsAreSettings) {
  wd::DeadTunnelSettings quick;
  quick.sdkUnresponsiveMillis = 8000;
  quick.sdkUnresponsiveArmMillis = 2000;
  quick.failsafeNoticeMillis = 5000;
  Session silent(1000000, quick);
  UR_EXPECT_EQ(quick.sdkUnresponsiveMillis,
               silent.RunUntilDead(120000, /*sampling=*/false, 0, 0, 0));

  // The countdown shows once it is within the notice, not before.
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  const int64_t now = 1000 + 10000;
  s.lastSampleMillis = now - 2999;  // 5001 to go: overdue, but not yet within 5 s
  UR_EXPECT_FALSE(wd::Evaluate(s, now, quick).armed);
  s.lastSampleMillis = now - 3000;
  const wd::DeadTunnelVerdict v = wd::Evaluate(s, now, quick);
  UR_EXPECT_TRUE(v.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(5000, v.millisToFailsafe);
  // By the defaults the same sample is not even overdue.
  UR_EXPECT_FALSE(wd::Evaluate(s, now, kSettings).armed);

  wd::DeadTunnelSettings jumpy;
  jumpy.evaluatorFrozenMillis = 3000;
  UR_EXPECT_TRUE(wd::EvaluatorFroze(3000, jumpy));
  UR_EXPECT_FALSE(wd::EvaluatorFroze(2999, jumpy));
  UR_EXPECT_FALSE(wd::EvaluatorFroze(3000, kSettings));
  Session session(1000000, jumpy);
  UR_EXPECT_FALSE(session.Tick(2999).froze);
  UR_EXPECT_TRUE(session.Tick(3000).froze);

  wd::DeadTunnelSettings sampler;
  sampler.sdkSampleIntervalMillis = 500;
  UR_EXPECT_EQ(500, wd::SampleIntervalMillis(sampler));
  UR_EXPECT_EQ(kSettings.sdkSampleIntervalMillis, wd::SampleIntervalMillis(kSettings));
}

// What 0 means for each timing, as DeadTunnelSettings documents it: a rule
// window of 0 turns its rule off, a notice of 0 never warns, an arming bar of
// 0 counts down from every sample, a commitment of 0 judges an idle machine, a
// freeze bar of 0 turns the failsafe off, a rebase stretch of 0 rebases on
// every return, and a sample interval of 0 is read as the default.
UR_TEST(TunnelWatchdog_WhatZeroMeansForEachTiming) {
  // SdkUnresponsive off: a sampler that never answers ends nothing, and no
  // countdown runs for it.
  wd::DeadTunnelSettings noWedge;
  noWedge.sdkUnresponsiveMillis = 0;
  Session silent(1000000, noWedge);
  UR_EXPECT_EQ(-1, silent.RunUntilDead(300000, /*sampling=*/false, 0, 0, 0));
  UR_EXPECT_FALSE(silent.Tick().verdict.armed);

  // NoInbound off, and its veto: sending with nothing back is left to NoExit,
  // and a packet back a moment ago does not hold off a silent SDK.
  wd::DeadTunnelSettings noFast;
  noFast.deadFastMillis = 0;
  Session sending(1000000, noFast);
  sending.Sample(0);
  int64_t at = sending.RunUntilDead(300000, /*sampling=*/true, 0, 10, 0);
  UR_EXPECT_EQ(noFast.deadSlowMillis, at);
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  const int64_t now = 1000 + 100000;
  s.lastInboundMillis = now - 1;
  UR_EXPECT_TRUE(wd::Evaluate(s, now, kSettings).reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(wd::Evaluate(s, now, noFast).reason ==
                 wd::DeadTunnelReason::SdkUnresponsive);

  // NoExit off: an answering SDK with nothing proven ends nothing.
  wd::DeadTunnelSettings noSlow;
  noSlow.deadSlowMillis = 0;
  Session idle(1000000, noSlow);
  idle.Sample(0);
  UR_EXPECT_EQ(-1, idle.RunUntilDead(300000, /*sampling=*/true, 0, 0, 0));
  UR_EXPECT_FALSE(idle.Tick().verdict.armed);

  // No notice: NoExit's countdown is never shown, and the rule still fires.
  wd::DeadTunnelSettings noNotice;
  noNotice.failsafeNoticeMillis = 0;
  wd::DeadTunnelSignals counting;
  counting.tunnelUp = true;
  counting.routesInstalled = true;
  counting.upSinceMillis = 1000;
  counting.lastSampleMillis = 1000 + 60000;
  UR_EXPECT_TRUE(wd::Evaluate(counting, 1000 + 60000, kSettings).armed);
  UR_EXPECT_FALSE(wd::Evaluate(counting, 1000 + 60000, noNotice).armed);
  counting.lastSampleMillis = 1000 + noNotice.deadSlowMillis;
  UR_EXPECT_TRUE(wd::Evaluate(counting, 1000 + noNotice.deadSlowMillis, noNotice).reason ==
                 wd::DeadTunnelReason::NoExit);

  // No arming bar: the SDK countdown runs from a sample just taken, and with
  // the default notice it is shown at once.
  wd::DeadTunnelSettings eagerArm;
  eagerArm.sdkUnresponsiveArmMillis = 0;
  wd::DeadTunnelSignals fresh;
  fresh.tunnelUp = true;
  fresh.routesInstalled = true;
  fresh.upSinceMillis = 1000;
  fresh.provenCount = 1;
  fresh.lastProvenMillis = 1000 + 5000;
  fresh.lastSampleMillis = 1000 + 5000;
  UR_EXPECT_FALSE(wd::Evaluate(fresh, 1000 + 5000, kSettings).armed);
  const wd::DeadTunnelVerdict armed = wd::Evaluate(fresh, 1000 + 5000, eagerArm);
  UR_EXPECT_TRUE(armed.armed);
  UR_EXPECT_EQ(eagerArm.sdkUnresponsiveMillis, armed.millisToFailsafe);

  // No commitment: an idle machine with nothing proven and nothing back is
  // judged by NoInbound, where by the defaults it waits for NoExit.
  wd::DeadTunnelSettings noCommitment;
  noCommitment.deadFastOutboundPackets = 0;
  Session idleJudged(1000000, noCommitment);
  idleJudged.Sample(0);
  wd::DeadTunnelReason reason = wd::DeadTunnelReason::None;
  int64_t elapsed = 0;
  while (reason == wd::DeadTunnelReason::None && elapsed < 300000) {
    elapsed += kSettings.evaluateIntervalMillis;
    reason = idleJudged.Tick().verdict.reason;
    if (elapsed % kSettings.sdkSampleIntervalMillis == 0) idleJudged.Sample(0);
  }
  UR_EXPECT_EQ(noCommitment.deadFastMillis, elapsed);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoInbound);

  // No freeze bar: every tick is a freeze, so nothing is ever judged.
  wd::DeadTunnelSettings alwaysFrozen;
  alwaysFrozen.evaluatorFrozenMillis = 0;
  Session frozen(1000000, alwaysFrozen);
  UR_EXPECT_TRUE(frozen.Tick().froze);
  UR_EXPECT_EQ(-1, frozen.RunUntilDead(300000, /*sampling=*/false, 0, 10, 0));
  // An instance made for an evaluate interval of 0 freezes at 0, and rebases
  // on the same bar.
  const wd::DeadTunnelSettings noInterval{0};
  UR_EXPECT_EQ(0, noInterval.evaluatorFrozenMillis);
  UR_EXPECT_EQ(0, noInterval.offlineRebaseMillis);

  // No rebase stretch: one offline tick is enough to rebase on the return.
  wd::DeadTunnelSettings alwaysRebase;
  alwaysRebase.offlineRebaseMillis = 0;
  UR_EXPECT_TRUE(wd::StepOffline(true, false, kSettings.evaluateIntervalMillis, alwaysRebase) ==
                 wd::OfflineStep::BackRebased);
  UR_EXPECT_TRUE(wd::StepOffline(true, false, kSettings.evaluateIntervalMillis, kSettings) ==
                 wd::OfflineStep::BackFromBlip);
  Session blip(1000000, alwaysRebase);
  blip.in.offline = true;
  blip.Tick();
  blip.in.offline = false;
  const wd::WatchTick back = blip.Tick();
  UR_EXPECT_TRUE(back.offlineStep == wd::OfflineStep::BackRebased);
  UR_EXPECT_EQ(blip.in.nowMillis, blip.watch.sessionStartMillis());

  // A sample interval of 0, or less, is the default: the sampler never spins.
  wd::DeadTunnelSettings spin;
  spin.sdkSampleIntervalMillis = 0;
  UR_EXPECT_EQ(kSettings.sdkSampleIntervalMillis, wd::SampleIntervalMillis(spin));
  spin.sdkSampleIntervalMillis = -5;
  UR_EXPECT_EQ(kSettings.sdkSampleIntervalMillis, wd::SampleIntervalMillis(spin));
}
