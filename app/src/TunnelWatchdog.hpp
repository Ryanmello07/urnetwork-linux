// The dead-tunnel failsafe's decision (docs/linux_agent_help.md 6.4): when a
// live tunnel that carries nothing is ended so the machine is not left pointed
// at it.
//
// With the kill switch off the capture routes send everything into the tun
// whether or not the session behind it can carry it, so a session whose
// transports died used to leave the machine without internet until somebody
// acted. The daemon now ends such a session the way its other protective
// teardowns do: with the kill switch off the machine gets its internet back,
// unprotected and told so; with it on the machine stays blocked behind the
// armed floor. It never reconnects, so it cannot thrash.
//
// Three rules, any one of which ends the session, behind one veto:
//   the carrying veto  a packet back out of the tunnel in the last 20 s means
//                      it carries, and nothing fires (a measurement outranks
//                      every claim below it)
//   SdkUnresponsive    no completed sample of the SDK for 30 s
//   NoInbound          8 or more packets into the tunnel since anything came
//                      back, nothing back and no proven exit, for 20 s
//   NoExit             no proven exit for 90 s, with the SDK answering
// A false teardown of a working tunnel is the failure this must never have, so
// an idle machine never trips NoInbound (it sends nothing), a suspend or a
// stalled daemon rebases every window rather than judging time nobody watched,
// and a new destination window gets its own clock while it forms.
//
// Pure, so every rule is a table test: TunnelHost reads the tun's packet
// counters and the exit sampler's last readings once a second on its reaper,
// hands them to DeadTunnelWatch with one clock, and acts on the verdict. The
// clock is CLOCK_BOOTTIME, which runs through a suspend, so a resume shows up
// as the tick gap it is. Ported from urnetwork/windows Service/TunnelWatchdog.h.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

#include "ControlProtocol.hpp"

namespace urnw::watchdog {

// ---- the thresholds ---------------------------------------------------------

// NoInbound's window. Above the longest honest machine-wide inbound gap (a DNS
// client's schedule across a server list runs about 13 s; a black-holed TCP
// connect stalls one flow, not all of them), and below the 30 s after which a
// person has decided the internet is broken.
inline constexpr int64_t kDeadFastMillis = 20000;
// Packets into the tunnel it takes to arm NoInbound: a machine that is trying
// retransmits this many in well under a second, and an idle one never sends
// them. An idle blocked machine looks like an idle working one, so it is never
// judged.
inline constexpr uint64_t kDeadFastOutboundPackets = 8;
// NoExit's window: about 2.5 times one honest cold attempt after a roam (DNS,
// TCP and the handshake, about 35 s), so a roam that recovers on its second
// attempt is never ended.
inline constexpr int64_t kDeadSlowMillis = 90000;
// SdkUnresponsive's window: 15 missed samples. A call into the SDK that has not
// returned in 30 s is wedged, not slow, and this is the one rule that needs
// nothing from the SDK to fire.
inline constexpr int64_t kSdkUnresponsiveMillis = 30000;
// How overdue a sample must be before its countdown is worth showing; below it
// is the normal cadence.
inline constexpr int64_t kSdkUnresponsiveArmMillis = 10000;
// How close a verdict must be before the app is told a countdown runs
// (StatusReply::failsafe_armed), so a teardown is never a surprise and a warning
// is never noise.
inline constexpr int64_t kFailsafeNoticeMillis = 30000;
// How often the exit sampler asks the SDK, and how often the verdict is
// reached. The verdict runs on the reaper, which makes no call on a watched
// session's device: the sampler makes them all, the network-change kicks
// included, so a wedged device stops the sampler and cannot keep the verdict
// from being reached.
inline constexpr int64_t kSdkSampleIntervalMillis = 2000;
inline constexpr int64_t kEvaluateIntervalMillis = 1000;
// A tick this late means nothing watched the session in between: a suspend, a
// stopped process, a main loop held by a long stop. The session is owed a fresh
// grace period then, not a verdict. Five intervals is far beyond scheduling
// jitter and far below the shortest window above.
inline constexpr int64_t kEvaluatorFrozenMillis = 5 * kEvaluateIntervalMillis;

static_assert(kSdkUnresponsiveMillis > kSdkUnresponsiveArmMillis);
static_assert(kSdkUnresponsiveMillis >= 10 * kSdkSampleIntervalMillis);
static_assert(kEvaluatorFrozenMillis < kDeadFastMillis);

inline constexpr bool EvaluatorFroze(int64_t tickGapMillis) {
  return tickGapMillis >= kEvaluatorFrozenMillis;
}

// A stamp from before `sessionStartMillis` is no evidence about this session;
// -1 is how the verdict reads "never in this session".
inline constexpr int64_t StampInSession(int64_t stampMillis, int64_t sessionStartMillis) {
  return stampMillis >= sessionStartMillis ? stampMillis : -1;
}

// ---- the verdict ------------------------------------------------------------

enum class DeadTunnelReason {
  None,
  // No proven exit for kDeadSlowMillis: nothing to carry traffic to.
  NoExit,
  // Committed traffic in and nothing back for kDeadFastMillis: the hole,
  // measured rather than inferred.
  NoInbound,
  // The SDK has not answered for kSdkUnresponsiveMillis. The other two read
  // the SDK, so this outranks them.
  SdkUnresponsive,
};

// The stop_reason a teardown for `reason` publishes ("" for None).
inline const char* StopReasonOf(DeadTunnelReason reason) {
  switch (reason) {
    case DeadTunnelReason::None:
      return "";
    case DeadTunnelReason::NoExit:
      return ctl::kStopReasonFailsafeNoExit;
    case DeadTunnelReason::NoInbound:
      return ctl::kStopReasonFailsafeNoInbound;
    case DeadTunnelReason::SdkUnresponsive:
      return ctl::kStopReasonFailsafeSdkUnresponsive;
  }
  return "";
}

// Everything the verdict reads. Timestamps are milliseconds on the watchdog's
// clock, -1 for "never in this session".
struct DeadTunnelSignals {
  // The tunnel is up and its capture routes are in: the machine is pointed at
  // it, which is the only thing the verdict judges.
  bool tunnelUp = false;
  bool routesInstalled = false;
  // When the session being judged began; 0 for no session.
  int64_t upSinceMillis = 0;
  // When NoInbound's traffic clock began: a new destination window starts it
  // only once it first forms. 0 means upSinceMillis.
  int64_t trafficStartMillis = 0;
  // False only while a new destination generation first forms.
  bool providerWindowReady = true;

  // From the exit sampler: the proven exits in its last completed sample, when
  // a sample last had one, and when a sample last completed at all (the wedge
  // detector).
  int64_t provenCount = 0;
  int64_t lastProvenMillis = -1;
  int64_t lastSampleMillis = -1;

  // From the tun's counters: when a packet last came back out of the tunnel,
  // and how many went in since (or since the traffic clock began).
  int64_t lastInboundMillis = -1;
  uint64_t outboundSinceInbound = 0;
};

struct DeadTunnelVerdict {
  DeadTunnelReason reason = DeadTunnelReason::None;
  // A countdown runs and is within kFailsafeNoticeMillis of firing
  // (StatusReply::failsafe_armed).
  bool armed = false;
  // Until the earliest deadline, while armed; 0 otherwise.
  int64_t millisToFailsafe = 0;
};

namespace detail {
// How long since `stampMillis`, counted from `startMillis` when it is -1.
inline constexpr int64_t AgeSince(int64_t stampMillis, int64_t startMillis, int64_t nowMillis) {
  return nowMillis - (stampMillis < 0 ? startMillis : stampMillis);
}
}  // namespace detail

// The verdict. Recovery is one-sided and immediate: one inbound packet or one
// proven exit clears every countdown that depends on it.
inline DeadTunnelVerdict Evaluate(const DeadTunnelSignals& s, int64_t nowMillis) {
  DeadTunnelVerdict v;
  if (!s.tunnelUp || !s.routesInstalled || s.upSinceMillis <= 0) return v;

  const int64_t sampleAge = detail::AgeSince(s.lastSampleMillis, s.upSinceMillis, nowMillis);
  const int64_t provenAge = detail::AgeSince(s.lastProvenMillis, s.upSinceMillis, nowMillis);
  const int64_t trafficStartMillis =
      s.trafficStartMillis > 0 ? s.trafficStartMillis : s.upSinceMillis;
  const int64_t inboundAge =
      detail::AgeSince(s.lastInboundMillis, trafficStartMillis, nowMillis);

  // The carrying veto, first. A packet back out of the tun is this process's
  // only first-hand evidence that the tunnel carries; the proven count is the
  // SDK's claim about its transports and a completed sample is a claim about
  // the SDK answering, and neither may overrule it. It cannot mask NoInbound,
  // which needs the same window of silence, nor a wedge that stops the SDK's
  // packet path too, which delivers nothing and lets the veto lapse within
  // kDeadFastMillis.
  //
  // Its known hole (docs/linux_agent_help.md 6.4, open task #44): the tun's
  // counters cannot tell a provider's packet from one the SDK writes itself.
  // The SDK's DNS mux claims every UDP/53 query on the tun and writes the
  // answer itself, a failure included, and resets DoT with a local RST, so a
  // machine whose apps keep resolving can hold the veto with every provider
  // dead, and then none of the three rules below fires. Closing it needs an
  // SDK count of the packets it wrote that no provider sent.
  if (s.lastInboundMillis >= 0 && inboundAge < kDeadFastMillis) return v;

  if (sampleAge >= kSdkUnresponsiveMillis) {
    v.reason = DeadTunnelReason::SdkUnresponsive;
    return v;
  }

  const bool fastApplies = s.providerWindowReady && s.provenCount == 0 &&
                           s.outboundSinceInbound >= kDeadFastOutboundPackets;
  if (fastApplies && inboundAge >= kDeadFastMillis && provenAge >= kDeadFastMillis) {
    v.reason = DeadTunnelReason::NoInbound;
    return v;
  }

  // Only once the sampler has answered: never having a reading is
  // SdkUnresponsive's question, not this rule's.
  if (s.provenCount == 0 && s.lastSampleMillis >= 0 && provenAge >= kDeadSlowMillis) {
    v.reason = DeadTunnelReason::NoExit;
    return v;
  }

  // Nothing fired: is a countdown accumulating, and how close is it? The
  // unresponsive one counts only once overdue, the fast one only while its
  // traffic holds.
  int64_t soonest = -1;
  auto consider = [&soonest](int64_t remaining) {
    if (remaining < 0) remaining = 0;
    if (soonest < 0 || remaining < soonest) soonest = remaining;
  };
  if (sampleAge >= kSdkUnresponsiveArmMillis) consider(kSdkUnresponsiveMillis - sampleAge);
  if (fastApplies) consider(kDeadFastMillis - (inboundAge < provenAge ? inboundAge : provenAge));
  if (s.provenCount == 0 && s.lastSampleMillis >= 0) consider(kDeadSlowMillis - provenAge);
  if (soonest >= 0 && soonest <= kFailsafeNoticeMillis) {
    v.armed = true;
    v.millisToFailsafe = soonest;
  }
  return v;
}

// ---- the trackers -----------------------------------------------------------

// Turns the tun's two monotonic packet counters into the edges the verdict
// reads: when something last came back, and how much went in since.
class TrafficTracker {
 public:
  // A new session or traffic clock: everything counted before belongs to
  // something else.
  void Reset(uint64_t outbound, uint64_t inbound) {
    lastOutbound_ = outbound;
    lastInbound_ = inbound;
    outboundAtLastInbound_ = outbound;
    lastInboundMillis_ = -1;
  }

  void Observe(uint64_t outbound, uint64_t inbound, int64_t nowMillis) {
    if (inbound > lastInbound_) {
      lastInbound_ = inbound;
      lastInboundMillis_ = nowMillis;
      // A packet back resets the commitment as well as the clock.
      outboundAtLastInbound_ = outbound;
    }
    if (outbound > lastOutbound_) lastOutbound_ = outbound;
  }

  int64_t lastInboundMillis() const { return lastInboundMillis_; }
  uint64_t outboundSinceInbound() const {
    return lastOutbound_ > outboundAtLastInbound_ ? lastOutbound_ - outboundAtLastInbound_ : 0;
  }

 private:
  uint64_t lastOutbound_ = 0;
  uint64_t lastInbound_ = 0;
  uint64_t outboundAtLastInbound_ = 0;
  int64_t lastInboundMillis_ = -1;
};

// What one reading of the SDK's window status changes. A new destination
// generation (another location, or a reconnect to the same one) restarts every
// clock; its window first being satisfied restarts only NoInbound's traffic
// clock, so a replacement window may honestly take longer than kDeadFastMillis
// to form without inheriting the old one's silence, and without a second
// kDeadSlowMillis of grace once it has formed.
struct ConnectionEpochUpdate {
  bool verdictClockReset = false;
  bool trafficClockReset = false;
};

// Follows WindowStatus::ConnectionGeneration and MinSatisfied. An older
// generation is ignored, and readiness is one-way within a generation, so the
// window's ordinary churn cannot keep resetting the failsafe.
class ConnectionEpochTracker {
 public:
  ConnectionEpochUpdate Observe(int64_t connectionGeneration, bool minSatisfied) {
    ConnectionEpochUpdate update;
    if (!initialized_) {
      initialized_ = true;
      connectionGeneration_ = connectionGeneration;
      forming_ = !minSatisfied;
      return update;
    }
    if (connectionGeneration < connectionGeneration_) return update;
    if (connectionGeneration > connectionGeneration_) {
      connectionGeneration_ = connectionGeneration;
      forming_ = !minSatisfied;
      update.verdictClockReset = true;
      update.trafficClockReset = true;
      return update;
    }
    if (forming_ && minSatisfied) {
      forming_ = false;
      update.trafficClockReset = true;
    }
    return update;
  }

  bool FastVerdictEligible() const { return initialized_ && !forming_; }
  int64_t connectionGeneration() const { return connectionGeneration_; }

 private:
  bool initialized_ = false;
  bool forming_ = false;
  int64_t connectionGeneration_ = 0;
};

// ---- one session's watch ----------------------------------------------------

// What one tick reads: the clock, the tunnel's state, the tun's counters and
// the exit sampler's last readings.
struct WatchInputs {
  int64_t nowMillis = 0;
  bool tunnelUp = false;
  bool routesInstalled = false;
  // tx_packets (into the tunnel) and rx_packets (back out of it) of the tun.
  uint64_t outboundPackets = 0;
  uint64_t inboundPackets = 0;
  int64_t provenCount = 0;
  int64_t lastProvenMillis = -1;
  int64_t lastSampleMillis = -1;
  // The destination generation and whether its window is satisfied, as the
  // sampler last read them. Without a reading, 0 and true: one generation,
  // formed.
  int64_t connectionGeneration = 0;
  bool providerWindowMinSatisfied = true;
  // The two values above come from a completed sample. Until one has, the
  // destination clocks have no baseline and NoInbound waits: the sampler's
  // first sample, taken at the up edge, sets the level, so the session's own
  // generation is never read as a new one.
  bool windowSampled = true;
};

struct WatchTick {
  DeadTunnelVerdict verdict;
  // The tick came kEvaluatorFrozenMillis or more after the one before it, so
  // every window was rebased and no verdict was reached.
  bool froze = false;
  int64_t tickGapMillis = 0;
  bool verdictClockReset = false;
  bool trafficClockReset = false;
  // The verdict's inputs, for the log line of a teardown.
  DeadTunnelSignals signals;
};

// The evaluator of one Up session: the freeze rebase, the destination clocks
// and the traffic edges around Evaluate, ticked once a second.
class DeadTunnelWatch {
 public:
  // The session's up edge.
  void Start(const WatchInputs& in) {
    sessionStartMillis_ = in.nowMillis;
    trafficStartMillis_ = in.nowMillis;
    lastTickMillis_ = in.nowMillis;
    traffic_.Reset(in.outboundPackets, in.inboundPackets);
    epoch_ = ConnectionEpochTracker();
    if (in.windowSampled) epoch_.Observe(in.connectionGeneration, in.providerWindowMinSatisfied);
  }

  WatchTick Tick(const WatchInputs& in) {
    WatchTick tick;
    const int64_t now = in.nowMillis;
    tick.tickGapMillis = now - lastTickMillis_;
    lastTickMillis_ = now;
    if (EvaluatorFroze(tick.tickGapMillis)) {
      // Nothing watched the session in that gap: every age would be the length
      // of the gap, not of a failure. Rebase, and take the current readings as
      // the new baseline.
      tick.froze = true;
      sessionStartMillis_ = now;
      trafficStartMillis_ = now;
      traffic_.Reset(in.outboundPackets, in.inboundPackets);
      if (in.windowSampled) epoch_.Observe(in.connectionGeneration, in.providerWindowMinSatisfied);
      return tick;
    }
    // The first sampled reading is the baseline, never a change.
    const ConnectionEpochUpdate update =
        in.windowSampled ? epoch_.Observe(in.connectionGeneration, in.providerWindowMinSatisfied)
                         : ConnectionEpochUpdate{};
    tick.verdictClockReset = update.verdictClockReset;
    tick.trafficClockReset = update.trafficClockReset;
    if (update.verdictClockReset) sessionStartMillis_ = now;
    if (update.trafficClockReset) {
      trafficStartMillis_ = now;
      traffic_.Reset(in.outboundPackets, in.inboundPackets);
    }
    traffic_.Observe(in.outboundPackets, in.inboundPackets, now);

    DeadTunnelSignals& s = tick.signals;
    s.tunnelUp = in.tunnelUp;
    s.routesInstalled = in.routesInstalled;
    s.upSinceMillis = sessionStartMillis_;
    s.trafficStartMillis = trafficStartMillis_;
    s.providerWindowReady = epoch_.FastVerdictEligible();
    s.provenCount = in.provenCount;
    // A sample taken before a rebase says nothing about the session now.
    s.lastProvenMillis = StampInSession(in.lastProvenMillis, sessionStartMillis_);
    s.lastSampleMillis = StampInSession(in.lastSampleMillis, sessionStartMillis_);
    s.lastInboundMillis = traffic_.lastInboundMillis();
    s.outboundSinceInbound = traffic_.outboundSinceInbound();
    tick.verdict = Evaluate(s, now);
    return tick;
  }

  int64_t sessionStartMillis() const { return sessionStartMillis_; }
  int64_t trafficStartMillis() const { return trafficStartMillis_; }

 private:
  int64_t sessionStartMillis_ = 0;
  int64_t trafficStartMillis_ = 0;
  int64_t lastTickMillis_ = 0;
  TrafficTracker traffic_;
  ConnectionEpochTracker epoch_;
};

}  // namespace urnw::watchdog
