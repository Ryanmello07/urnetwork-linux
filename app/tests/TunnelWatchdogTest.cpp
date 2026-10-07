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

// A session as the reaper sees it: a clock, the tun's counters and the
// sampler's readings, advanced one tick at a time.
struct Session {
  wd::DeadTunnelWatch watch;
  wd::WatchInputs in;

  explicit Session(int64_t startMillis = 1000000) {
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

  wd::WatchTick Tick(int64_t stepMillis = wd::kEvaluateIntervalMillis) {
    in.nowMillis += stepMillis;
    return watch.Tick(in);
  }

  // Ticks until a verdict or `millis` have passed; the elapsed time at the
  // verdict, or -1 for none.
  int64_t RunUntilDead(int64_t millis, bool sampling, int64_t proven, uint64_t outPerTick,
                       uint64_t inPerTick) {
    for (int64_t elapsed = wd::kEvaluateIntervalMillis; elapsed <= millis;
         elapsed += wd::kEvaluateIntervalMillis) {
      in.outboundPackets += outPerTick;
      in.inboundPackets += inPerTick;
      const wd::WatchTick tick = Tick();
      if (sampling && (elapsed % wd::kSdkSampleIntervalMillis) == 0) Sample(proven);
      if (tick.verdict.reason != wd::DeadTunnelReason::None) return elapsed;
    }
    return -1;
  }
};

}  // namespace

// The numbers the windows were chosen from, pinned.
UR_TEST(TunnelWatchdog_ThresholdsArePinned) {
  UR_EXPECT_EQ(20000, wd::kDeadFastMillis);
  UR_EXPECT_EQ(8u, wd::kDeadFastOutboundPackets);
  UR_EXPECT_EQ(90000, wd::kDeadSlowMillis);
  UR_EXPECT_EQ(30000, wd::kSdkUnresponsiveMillis);
  UR_EXPECT_EQ(10000, wd::kSdkUnresponsiveArmMillis);
  UR_EXPECT_EQ(30000, wd::kFailsafeNoticeMillis);
  UR_EXPECT_EQ(2000, wd::kSdkSampleIntervalMillis);
  UR_EXPECT_EQ(1000, wd::kEvaluateIntervalMillis);
  UR_EXPECT_EQ(5000, wd::kEvaluatorFrozenMillis);
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
  UR_EXPECT_EQ(wd::kDeadSlowMillis, at);
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
  UR_EXPECT_EQ(wd::kDeadFastMillis, at);
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
  UR_EXPECT_EQ(wd::kSdkUnresponsiveMillis, at);

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
  UR_EXPECT_EQ(wd::kSdkUnresponsiveMillis, elapsed);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::SdkUnresponsive);

  // Every rule's condition at once: unresponsive wins.
  wd::DeadTunnelSignals all;
  all.tunnelUp = true;
  all.routesInstalled = true;
  all.upSinceMillis = 1000;
  all.lastSampleMillis = 1000;
  all.outboundSinceInbound = 100;
  const wd::DeadTunnelVerdict verdict = wd::Evaluate(all, 1000 + wd::kDeadSlowMillis);
  UR_EXPECT_TRUE(verdict.reason == wd::DeadTunnelReason::SdkUnresponsive);
}

// NoExit needs a sampler that answered: never having a reading is
// SdkUnresponsive's question.
UR_TEST(TunnelWatchdog_NoExitNeedsAnAnsweringSampler) {
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  s.lastSampleMillis = 1000 + wd::kDeadSlowMillis - 1000;  // answering, nothing proven
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + wd::kDeadSlowMillis).reason ==
                 wd::DeadTunnelReason::NoExit);
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + wd::kDeadSlowMillis - 1).reason ==
                 wd::DeadTunnelReason::None);
  s.lastProvenMillis = 1000 + wd::kDeadSlowMillis - 5000;
  s.provenCount = 1;
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + wd::kDeadSlowMillis).reason ==
                 wd::DeadTunnelReason::None);
  // Never answered: the SDK's silence is the reason, not the missing exit.
  s.provenCount = 0;
  s.lastProvenMillis = -1;
  s.lastSampleMillis = -1;
  UR_EXPECT_TRUE(wd::Evaluate(s, 1000 + wd::kDeadSlowMillis).reason ==
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
  s.lastInboundMillis = now - wd::kDeadFastMillis + 1;
  const wd::DeadTunnelVerdict vetoed = wd::Evaluate(s, now);
  UR_EXPECT_TRUE(vetoed.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_FALSE(vetoed.armed);
  s.lastInboundMillis = now - wd::kDeadFastMillis;
  UR_EXPECT_TRUE(wd::Evaluate(s, now).reason == wd::DeadTunnelReason::SdkUnresponsive);
}

// Nothing is judged unless the machine is pointed at a tunnel.
UR_TEST(TunnelWatchdog_OnlyAnUpTunnelWithRoutesIsJudged) {
  wd::DeadTunnelSignals s;
  s.tunnelUp = true;
  s.routesInstalled = true;
  s.upSinceMillis = 1000;
  const int64_t late = 1000 + 10 * wd::kDeadSlowMillis;
  UR_EXPECT_TRUE(wd::Evaluate(s, late).reason != wd::DeadTunnelReason::None);
  s.routesInstalled = false;
  UR_EXPECT_TRUE(wd::Evaluate(s, late).reason == wd::DeadTunnelReason::None);
  s.routesInstalled = true;
  s.tunnelUp = false;
  UR_EXPECT_TRUE(wd::Evaluate(s, late).reason == wd::DeadTunnelReason::None);
  s.tunnelUp = true;
  s.upSinceMillis = 0;
  UR_EXPECT_TRUE(wd::Evaluate(s, late).reason == wd::DeadTunnelReason::None);
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
  wd::DeadTunnelVerdict v = wd::Evaluate(s, 1000 + 50000);
  UR_EXPECT_FALSE(v.armed);  // 40 s to go
  s.lastSampleMillis = 1000 + 60000;
  v = wd::Evaluate(s, 1000 + 60000);
  UR_EXPECT_TRUE(v.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(30000, v.millisToFailsafe);
  // NoInbound's, which is the sooner: traffic in since a traffic clock that
  // began 15 s ago, nothing ever back.
  s.trafficStartMillis = 1000 + 45000;
  s.outboundSinceInbound = 8;
  v = wd::Evaluate(s, 1000 + 60000);
  UR_EXPECT_TRUE(v.reason == wd::DeadTunnelReason::None);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(5000, v.millisToFailsafe);
  // A proven exit: no countdown at all.
  s.provenCount = 1;
  s.lastProvenMillis = 1000 + 60000;
  v = wd::Evaluate(s, 1000 + 60000);
  UR_EXPECT_FALSE(v.armed);
  UR_EXPECT_EQ(0, v.millisToFailsafe);
  // The unresponsive countdown shows only once a sample is 10 s overdue.
  wd::DeadTunnelSignals quiet = s;
  quiet.lastSampleMillis = 1000 + 60000 - wd::kSdkUnresponsiveArmMillis + 1;
  UR_EXPECT_FALSE(wd::Evaluate(quiet, 1000 + 60000).armed);
  quiet.lastSampleMillis = 1000 + 60000 - wd::kSdkUnresponsiveArmMillis;
  v = wd::Evaluate(quiet, 1000 + 60000);
  UR_EXPECT_TRUE(v.armed);
  UR_EXPECT_EQ(wd::kSdkUnresponsiveMillis - wd::kSdkUnresponsiveArmMillis, v.millisToFailsafe);
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
  UR_EXPECT_TRUE(wd::EvaluatorFroze(wd::kEvaluatorFrozenMillis));
  UR_EXPECT_FALSE(wd::EvaluatorFroze(wd::kEvaluatorFrozenMillis - 1));
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
  UR_EXPECT_EQ(wd::kDeadFastMillis, elapsed);
  UR_EXPECT_TRUE(reason == wd::DeadTunnelReason::NoInbound);
}

// The daemon starts the watch before the sampler has read the window, so the
// first sample's generation is the session's own, never a new destination: no
// clock restarts on it. NoInbound waits for it, and then judges from the up
// edge. Taking the generation as 0 until then would restart every clock at the
// first sample and hand a dead tunnel the time it took.
UR_TEST(TunnelWatchdog_TheFirstSampleIsTheBaseline) {
  wd::DeadTunnelWatch watch;
  wd::WatchInputs in;
  in.nowMillis = 1000000;
  in.tunnelUp = true;
  in.routesInstalled = true;
  in.windowSampled = false;
  watch.Start(in);
  // Before the first sample, traffic in and nothing back is not judged.
  for (int i = 0; i < 3; ++i) {
    in.nowMillis += wd::kEvaluateIntervalMillis;
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
    in.nowMillis += wd::kEvaluateIntervalMillis;
    in.outboundPackets += 10;
    in.lastSampleMillis = in.nowMillis;
    const wd::WatchTick tick = watch.Tick(in);
    reset = reset || tick.verdictClockReset || tick.trafficClockReset;
    reason = tick.verdict.reason;
    elapsed += wd::kEvaluateIntervalMillis;
  }
  UR_EXPECT_FALSE(reset);
  UR_EXPECT_EQ(wd::kDeadFastMillis, elapsed);
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
