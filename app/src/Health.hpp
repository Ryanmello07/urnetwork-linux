// THE AGGREGATE CONNECTION HEALTH — the one reading pane A renders from.
//
// docs/parity/connect-page.md §2.1: "One writer: ConnectPage::ApplyConnectStatus().
// It renders the AGGREGATE health (`urnw::health::State`: NoService, Disconnected,
// Connecting, Evaluating, Connected, Degraded, Failed), not the raw SDK status."
// docs/parity/connect-canvas.md §5: "State sources (ConnectPage::ApplyConnectStatus
// — one writer; the canvas may never lag the status line above it, same inputs,
// same instant, one function)."
//
// WHY THIS FILE EXISTS. Before it, ApplyConnectStatus branched on the raw SDK
// status string in five places and the Connect button label was computed from a
// SIXTH, independent predicate. The owner screenshotted the result: the headline
// "Connecting to providers" with a yellow dot and the connecting hero, beside a
// button reading "Disconnect" — the app describing two different sessions in one
// row of pixels, at the moment he pressed Disconnect. Four channels, four
// readings. Everything below exists to make that unrepresentable: Render() takes
// ONE reading and returns text, dot, hero pose and button label together, in one
// value, and the page renders that value and nothing else.
//
// Pure C++17 on purpose — no GTK, no SDK, no glib. It is a decision table, it is
// the part of the page that can be reasoned about (and exercised) without a
// display, and the presentation tokens (the dot hex, ConnectCanvas::State) stay
// on the page side so this header cannot drift from them.
//
// ---------------------------------------------------------------------------
// THE STATE TABLE (connect-page.md §2.1, connect-canvas.md §5). Every render
// state, its headline, its dot, its hero pose and its button label:
//
//  render state          | StatusText (T_ key / English)                | dot        | hero         | button
//  ----------------------|----------------------------------------------|------------|--------------|-----------
//  Connected             | connected / "Connected"                      | Green      | Connected    | Disconnect
//  Evaluating            | conn_finding_providers / "Finding providers…" | Connecting | Connecting   | Disconnect
//  Degraded              | conn_degraded / "Connection degraded —        | Coral      | Connecting   | Disconnect
//                        |   reconnecting"                              |            |              |
//  Connecting            | connecting_status_indicator /                 | Connecting | Connecting   | Disconnect
//                        |   "Connecting to providers"                   |            |              |
//  Failed                | conn_failed / "Couldn't connect"             | Coral      | Error        | Retry
//  NoService/Disconnected| conn_disconnecting / "Disconnecting — your    | Connecting | Connecting   | Disconnect
//    routes still installed |  traffic is still going through the tunnel"|            |              |
//  NoService/Disconnected| conn_blocked_kill_switch /                    | Coral      | Disconnected | Connect
//    kill switch armed   |   "Blocked — kill switch on"                  |            |              |
//  NoService/Disconnected| disconnected / "Disconnected"                | Idle       | Disconnected | Connect
//    plain               |   (+ showNotProtected)                        |            |              |
//
//  balance overrides (they replace the canvas, never the button — an
//  out-of-balance session still has to be disconnectable):
//  balanceConfirming     | (headline unchanged)                          | unchanged  | Processing   | unchanged
//  balanceBlocked        | insufficient_balance_add_balance_or_plan /     | Coral      | Error        | unchanged
//                        |   "Insufficient balance — add balance or a plan"
//
// The dot tokens: Green #87FB67, Connecting #E6EA23, Coral #FF6C58, Idle #2A60FF.
// The hero poses map one-for-one onto ConnectCanvas::State.
//
// TWO ROWS ARE LOAD-BEARING FOR THE HERO'S PROVIDER DOTS. Evaluating and
// Degraded map to the Connecting hero DELIBERATELY (connect-canvas.md §5:
// "on purpose — the state in which SetGrid keeps taking updates, so the dots
// keep telling the evaluation story instead of freezing mid-way under a green
// headline"). ConnectCanvas::SetGrid drops every push unless the canvas is
// Connecting, and that freeze rule is correct iOS/Windows parity; the missing
// piece was always this mapping above it.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cctype>
#include <string>

namespace urnw {
namespace health {

// The aggregate. NoService and Disconnected render and act identically (both
// are "the SDK is not active"); they are kept apart because the reason differs
// and the daemon notice under the headline says which.
enum class State {
  NoService,     // no service/daemon behind the app: nothing can be carried
  Disconnected,  // settled idle
  Connecting,    // asked for, not yet carrying
  Evaluating,    // providers in the window, none proven
  Connected,     // carrying
  Degraded,      // was carrying; every provider in the window has stopped
  Failed,        // settled failure
};

// The SDK connect controller's own status (connect-page.md §2.1: parsed
// case-insensitively from getConnectionStatus(); anything else ⇒ Disconnected).
enum class SdkStatus { Disconnected, Connecting, DestinationSet, Connected, Failed };

// The hero pose. Mirrors ConnectCanvas::State one-for-one; a separate enum so
// this header stays free of GTK and the canvas cannot be written to from here.
enum class Hero { Disconnected, Connecting, Connected, Error, Processing };

// The status dot. A token, not a hex: the page owns the palette (§8.1).
enum class Dot { Idle, Connecting, Green, Coral };

// The one action the hero, the button and the tray share.
enum class Action { Connect, Disconnect, Retry };

// What the SERVICE says about this machine. On Windows these are the window's
// status* getters; on Linux they come off urnetworkd's status reply, carried
// on KillSwitchStatus (SdkHost) — tunnel_state and the installed floor.
//
// Every field is a FACT or false. `known` is what separates "the daemon told us
// the floor is off" from "the daemon has never answered": a machine whose
// nftables floor survived a dead daemon is still captured, and claiming
// otherwise is a fabrication in the dangerous direction.
struct ServiceFacts {
  bool known = false;             // a status reply landed; the rest is meaningful
  bool pipeUp = true;             // the control channel is healthy (permissive
                                  // until proven otherwise: a page that has
                                  // never probed must not claim "no service")
  bool routesInstalled = false;   // tun open / still tearing down: traffic is
                                  // STILL going through the tunnel
  bool killSwitchArmed = false;   // floor installed with no tunnel behind it
  bool killSwitchInForce = false; // floor installed (armed or alongside a tunnel)
};

// The provider window's own reading. Today it is derived from the grid points
// the canvas already rides; the SDK's WindowStatus carries exactly these
// counters plus StallReason/Failed/MinSatisfied and is the intended source
// (see the TODO in ConnectPage::ApplyConnectStatus).
//
// `known == false` is UNKNOWN, not empty: an idle or rpc-only session reports
// no grid at all, and reading that as "no provider is carrying" would turn a
// working connection into "Finding providers…".
struct Window {
  bool known = false;
  int inEvaluation = 0;
  int evaluationFailed = 0;
  int notAdded = 0;
  int added = 0;
  int removed = 0;
  bool failed = false;      // WindowStatus.Failed (settled window failure)
  std::string stallReason;  // WindowStatus.StallReason ("" = none)
};

// ---- the shared predicates (connect-page.md §2.4) ---------------------------
// The machine is captured when this app has taken its traffic: routes are
// installed, or a floor is in force. Both mean pressing Connect is not what the
// user wants next.
inline bool MachineIsCaptured(const ServiceFacts& f) {
  return f.routesInstalled || f.killSwitchInForce;
}
// An ARMED floor with no tunnel deliberately offers Connect, not Disconnect:
// there is nothing to disconnect from, and the way out is to connect (or to
// turn the switch off in options).
inline bool BlockedByKillSwitch(const ServiceFacts& f) {
  return !f.routesInstalled && f.killSwitchArmed;
}
inline bool SdkActive(State s) {
  return s != State::Disconnected && s != State::NoService;
}
// The SAME predicate the tray item uses. The button label is never computed
// from anything else.
inline bool ActionIsDisconnect(const ServiceFacts& f, State s) {
  return SdkActive(s) || (MachineIsCaptured(f) && !BlockedByKillSwitch(f));
}

// ---- parsing ---------------------------------------------------------------
inline SdkStatus ParseSdkStatus(const std::string& raw) {
  std::string s;
  s.reserve(raw.size());
  for (const char c : raw) s += static_cast<char>(::toupper(static_cast<unsigned char>(c)));
  if (s == "CONNECTED") return SdkStatus::Connected;
  if (s == "CONNECTING") return SdkStatus::Connecting;
  if (s == "DESTINATION_SET") return SdkStatus::DestinationSet;
  if (s == "CONNECT_FAILED") return SdkStatus::Failed;
  return SdkStatus::Disconnected;  // anything else ⇒ Disconnected
}

// ---- the aggregate ---------------------------------------------------------
struct Signals {
  SdkStatus sdk = SdkStatus::Disconnected;
  // The HONEST tunnel: SdkHost::Connected() (the SDK's own connected flag AND a
  // DeviceRemote bound over the current control session) AND LiveStats.connected.
  bool tunnelUp = false;
  Window window;
  ServiceFacts facts;
  // The user pressed Disconnect. An explicit local intent outranks a stale SDK
  // status — the press is the newest fact on the machine (the mirror image of
  // the optimistic Connecting write on the connect press, connect-page.md
  // §2.4 step 4).
  bool disconnectRequested = false;
};

inline State Aggregate(const Signals& s) {
  if (s.disconnectRequested) return State::Disconnected;
  if (!s.facts.pipeUp) return State::NoService;
  if (s.sdk == SdkStatus::Failed) return State::Failed;
  if (s.window.known && s.window.failed) return State::Failed;
  if (s.tunnelUp) {
    if (s.window.known) {
      // The window's own verdict, in the order the states settle: one provider
      // carrying is a connection; none carrying but some still being evaluated
      // is Evaluating; none carrying and none left to evaluate is Degraded.
      if (s.window.added > 0) return State::Connected;
      if (s.window.inEvaluation > 0) return State::Evaluating;
      if (s.window.evaluationFailed + s.window.notAdded + s.window.removed > 0) {
        return State::Degraded;
      }
    }
    // No window reading at all: the carried tunnel IS the evidence. Never
    // downgrade a working connection on the strength of a silent grid.
    return State::Connected;
  }
  if (s.sdk == SdkStatus::Connecting || s.sdk == SdkStatus::DestinationSet) {
    return State::Connecting;
  }
  return State::Disconnected;
}

// ---- the render --------------------------------------------------------
struct Inputs {
  State health = State::Disconnected;
  SdkStatus sdk = SdkStatus::Disconnected;
  ServiceFacts facts;
  // Carried so ONE gather feeds both halves: Aggregate() reads it to produce
  // `health`, Render() does not look at it again. (The page builds its Signals
  // from these same fields — there is no second gather anywhere.)
  Window window;
  bool tunnelUp = false;
  bool balanceBlocked = false;     // out of balance
  bool balanceConfirming = false;  // post-checkout poll running (wins)
  bool disconnectRequested = false;
  std::string stallReason;
};

// ONE value. Text, dot, hero and button label leave this function together or
// not at all — there is no path on which they can be taken from different
// readings.
struct Reading {
  State state = State::Disconnected;
  const char* textKey = "disconnected";
  const char* textEnglish = "Disconnected";
  Dot dot = Dot::Idle;
  Hero hero = Hero::Disconnected;
  Action action = Action::Connect;
  bool showNotProtected = false;
  // the soft-kill-switch honesty line (§2.1 TrafficHeldText); null = collapsed
  const char* heldKey = nullptr;
  const char* heldEnglish = nullptr;
  // the stall diagnosis (§2.1 StatusReasonText); null = collapsed
  const char* reasonKey = nullptr;
  const char* reasonEnglish = nullptr;
};

inline Reading Render(const Inputs& in) {
  // 1. RECONCILIATION (§2.1 RenderHealth). The SDK status and the aggregate can
  // describe different instants; this settles which one the whole row obeys.
  State render = in.health;
  // The intent is authoritative HERE too, not only in Aggregate(): the whole
  // point of the invariant is that no caller — and no future one — can arrange
  // a reading in which the user has asked to stop and the row says the app is
  // connecting. Belt and braces on the one defect this file exists to kill.
  if (in.disconnectRequested) render = State::Disconnected;
  if (!in.disconnectRequested) {
    // A connect in flight outranks a stale idle/connected aggregate. It must
    // NOT override Evaluating/Degraded/Failed — those are newer, more specific
    // readings of the same session. And it is skipped outright once the user
    // has pressed Disconnect: promoting a stale CONNECTING status over an
    // explicit disconnect is precisely the reported defect.
    if ((in.sdk == SdkStatus::Connecting || in.sdk == SdkStatus::DestinationSet) &&
        (render == State::Disconnected || render == State::NoService ||
         render == State::Connected)) {
      render = State::Connecting;
    }
  }
  // A settled idle status outranks stale ACTIVE health.
  if (in.sdk == SdkStatus::Disconnected && render != State::NoService &&
      render != State::Disconnected) {
    render = State::Disconnected;
  }

  Reading r;
  r.state = render;
  switch (render) {
    case State::Connected:
      r.textKey = "connected";
      r.textEnglish = "Connected";
      r.dot = Dot::Green;
      r.hero = Hero::Connected;
      break;
    case State::Evaluating:
      r.textKey = "conn_finding_providers";
      r.textEnglish = "Finding providers…";
      r.dot = Dot::Connecting;
      r.hero = Hero::Connecting;
      break;
    case State::Degraded:
      r.textKey = "conn_degraded";
      r.textEnglish = "Connection degraded — reconnecting";
      r.dot = Dot::Coral;
      r.hero = Hero::Connecting;
      break;
    case State::Connecting:
      r.textKey = "connecting_status_indicator";
      r.textEnglish = "Connecting to providers";
      r.dot = Dot::Connecting;
      r.hero = Hero::Connecting;
      break;
    case State::Failed:
      r.textKey = "conn_failed";
      r.textEnglish = "Couldn't connect";
      r.dot = Dot::Coral;
      r.hero = Hero::Error;
      break;
    case State::NoService:
    case State::Disconnected:
      if (in.facts.routesInstalled) {
        // The machine is still captured. This is the honest reading of the
        // moment AFTER the Disconnect press: the tunnel is coming down and the
        // traffic on it has not moved yet. It is not "Connecting".
        r.textKey = "conn_disconnecting";
        r.textEnglish = "Disconnecting — your traffic is still going through the tunnel";
        r.dot = Dot::Connecting;
        r.hero = Hero::Connecting;
      } else if (BlockedByKillSwitch(in.facts)) {
        r.textKey = "conn_blocked_kill_switch";
        r.textEnglish = "Blocked — kill switch on";
        r.dot = Dot::Coral;
        r.hero = Hero::Disconnected;
      } else {
        r.textKey = "disconnected";
        r.textEnglish = "Disconnected";
        r.dot = Dot::Idle;
        r.hero = Hero::Disconnected;
        // never claimed on a captured or kill-switched reading: those say
        // something more precise
        r.showNotProtected = true;
      }
      break;
  }

  // 2. THE ACTION. One predicate, applied to the state that was just rendered —
  // so "Connecting to providers" beside a "Disconnect" button is a consequence
  // of one reading, and "Disconnecting…" beside "Connect" is unreachable.
  if (render == State::Failed) {
    r.action = Action::Retry;  // Retry stays FILLED (§2.4)
  } else {
    r.action = ActionIsDisconnect(in.facts, render) ? Action::Disconnect : Action::Connect;
  }

  // 3. THE SUPPORTING LINES.
  // TrafficHeldText — the soft-kill-switch honesty line. A tunnel that is up
  // with nothing carrying is the one state where silence is a lie: the user's
  // traffic is either being held or leaking, and which one it is depends on the
  // floor.
  if (in.tunnelUp && (render == State::Evaluating || render == State::Degraded ||
                      render == State::Failed)) {
    if (in.facts.killSwitchInForce) {
      r.heldKey = "conn_traffic_blocked";
      r.heldEnglish =
          "No working provider right now — your traffic is blocked, not exposed. "
          "Disconnect to go back to your normal connection.";
    } else {
      r.heldKey = "conn_traffic_blocked_unprotected";
      r.heldEnglish =
          "No working provider right now — traffic sent into the tunnel is going "
          "nowhere, and leak protection is off, so some traffic may bypass it. "
          "Disconnect to go back to your normal connection.";
    }
  }
  // StatusReasonText — the stall diagnosis, only while something is in flight.
  if (render == State::Connecting || render == State::Evaluating ||
      render == State::Degraded || render == State::Failed) {
    const std::string& why = in.stallReason;
    if (why == "platform-unreachable") {
      r.reasonKey = "conn_reason_platform";
      r.reasonEnglish = "Contacting the platform…";
    } else if (why == "providers-unresponsive") {
      r.reasonKey = "conn_reason_providers";
      r.reasonEnglish = "Providers not responding — retrying…";
    } else if (why == "rate-limited") {
      r.reasonKey = "conn_reason_rate_limited";
      r.reasonEnglish = "Rate limited — waiting…";
    } else if (why == "auth-failing") {
      r.reasonKey = "conn_reason_auth";
      r.reasonEnglish = "Signing in to the platform is failing…";
    } else if (render == State::Failed) {
      // "evaluating" and "" render nothing: the headline already says it.
      r.reasonKey = "conn_failed_detail";
      r.reasonEnglish = "No providers could be reached. Retry rebuilds the connection from scratch.";
    }
  }

  // 4. THE BALANCE OVERRIDE (canvas §5: processing wins over out-of-balance).
  // It replaces the CANVAS, never the button: an out-of-balance session that is
  // still captured must stay disconnectable.
  if (in.balanceConfirming) {
    r.hero = Hero::Processing;
  } else if (in.balanceBlocked) {
    r.textKey = "insufficient_balance_add_balance_or_plan";
    r.textEnglish = "Insufficient balance — add balance or a plan";
    r.dot = Dot::Coral;
    r.hero = Hero::Error;
    r.showNotProtected = false;
  }
  return r;
}

}  // namespace health
}  // namespace urnw
