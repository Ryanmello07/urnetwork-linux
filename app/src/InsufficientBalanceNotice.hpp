// The out-of-balance notice: what the drawer banner says and when the desktop
// notification is posted and withdrawn.
//
// Out of balance is a billing state, not a dropped tunnel. While a connection
// is requested the tunnel keeps capturing with no provider behind it, so the
// user is told that their traffic is held and that Disconnect releases it.
// Nothing here ever disconnects: the notice only informs, and the user's own
// Disconnect (MainWindow::ToggleConnect) is the one way out.
//
// An episode starts when insufficient balance is first seen and ends when it
// clears. The notification is posted at most once per episode, at the first
// observation where the gate holds and a connection is requested (a Pro plan
// or a balance poll at entry defers it). It is withdrawn when the episode ends
// or the connection is no longer requested; a disconnect does not re-arm it
// within the episode.
//
// A new connect is a different matter: with no balance it is blocked and the
// entry point shows the upgrade path instead (BlockConnect, below). That gate
// only ever applies to starting a session, never to one that is already up.
//
// Pure and dependency-free so tests/InsufficientBalanceNoticeTest.cpp runs
// without GTK. Not thread safe: driven from the GTK main loop only.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw {
namespace balance_notice {

struct Signals {
  // ContractStatus.InsufficientBalance from the live connect reading.
  bool insufficientBalance = false;
  bool pro = false;
  // A purchase confirmation poll is in flight; the balance is about to change.
  bool polling = false;
  // A destination is selected, i.e. the tunnel is asked to carry traffic.
  bool connectRequested = false;
};

// The existing out-of-balance gate (mac ConnectActions, ConnectDrawer banner).
inline bool Gate(const Signals& s) { return s.insufficientBalance && !s.pro && !s.polling; }

// The tunnel is holding traffic for an out-of-balance account: the Connect
// page shows the held alert with Upgrade and Disconnect.
inline bool HeldAlert(const Signals& s) { return Gate(s) && s.connectRequested; }

struct BannerText {
  const char* key;
  const char* english;
};

// The drawer banner body. While a connection is requested the tunnel is
// holding traffic, so the banner says so and names the way out.
inline BannerText Banner(const Signals& s) {
  if (HeldAlert(s)) {
    return BannerText{"insufficient_balance_held_notice",
                      "Your traffic is held in the tunnel until you upgrade or disconnect."};
  }
  return BannerText{"insufficient_balance_message", "Add balance or a plan to keep connecting."};
}

// Posts and withdraws the desktop notification through a sink with Post() and
// Withdraw(). The sink is a template parameter so tests can count calls.
class Tracker {
 public:
  template <class Sink>
  void Observe(const Signals& s, Sink& sink) {
    if (!s.insufficientBalance) {
      // the episode ended: withdraw and re-arm
      Withdraw(sink);
      posted_ = false;
      return;
    }
    if (!s.connectRequested) {
      // nothing is held any more; posted_ stays set so the episode does not
      // post again after a disconnect
      Withdraw(sink);
      return;
    }
    if (!posted_ && Gate(s)) {
      posted_ = true;
      shown_ = true;
      sink.Post();
    }
  }

  bool Shown() const { return shown_; }

 private:
  template <class Sink>
  void Withdraw(Sink& sink) {
    if (!shown_) return;
    shown_ = false;
    sink.Withdraw();
  }

  bool posted_ = false;
  bool shown_ = false;
};

// ---- the start-connect gate ----------------------------------------------------
// Connect with no balance is blocked; a connection that runs out of balance
// stays up (urnetwork/android#483). Every connect entry point asks this before
// it starts anything: the tray menu, the Connect page, a location pick, connect
// on launch and the post-sign-in connect.

// What a connect entry point is asking for.
enum class ConnectAttempt {
  // No session is up: this press would start one.
  Start,
  // A session is up (health::SessionUp): a location change or a re-assertion
  // of the tunnel the user never left. Never blocked, never disconnected.
  AlreadyConnected,
};

inline ConnectAttempt ClassifyConnect(bool sessionUp) {
  return sessionUp ? ConnectAttempt::AlreadyConnected : ConnectAttempt::Start;
}

// Whether the entry point must not start the tunnel and should show the
// upgrade path instead. Only a Start is ever blocked, by the same Gate as the
// held alert.
inline bool BlockConnect(ConnectAttempt attempt, const Signals& s) {
  return attempt == ConnectAttempt::Start && Gate(s);
}

// Out of balance outlives the reading that reported it. The SDK clears the
// contract status whenever the destination changes, the user's Disconnect
// included, so after a Disconnect the live reading says nothing about the
// balance, and a Connect press would start a tunnel that can only hold
// traffic again. The latch keeps the last known out-of-balance state until
// there is evidence the balance changed:
//   - providers attached on a live session with no insufficient balance (the
//     contracts went through), or
//   - the subscription balance rose above its lowest value seen since the
//     state was latched (a refill, a redeemed code or a purchase landed).
// Pro and a purchase poll are handled by Gate. Reset on sign-in and sign-out.
class OutOfBalanceLatch {
 public:
  struct Observation {
    // ContractStatus.InsufficientBalance from the live connect reading.
    bool insufficientBalance = false;
    // A session is up and the controller reports providers attached.
    bool providersConnected = false;
    // The subscription balance has been fetched, and its available bytes.
    bool balanceKnown = false;
    long long availableBytes = 0;
  };

  void Observe(const Observation& o) {
    if (o.insufficientBalance) {
      latched_ = true;
      NoteBalance(o);
      return;
    }
    if (!latched_) return;
    if (o.providersConnected) {
      Reset();
      return;
    }
    if (o.balanceKnown && lowKnown_ && o.availableBytes > lowBytes_) {
      Reset();
      return;
    }
    NoteBalance(o);
  }

  void Reset() {
    latched_ = false;
    lowKnown_ = false;
    lowBytes_ = 0;
  }

  bool OutOfBalance() const { return latched_; }

 private:
  void NoteBalance(const Observation& o) {
    if (!o.balanceKnown) return;
    if (!lowKnown_ || o.availableBytes < lowBytes_) {
      lowKnown_ = true;
      lowBytes_ = o.availableBytes;
    }
  }

  bool latched_ = false;
  bool lowKnown_ = false;
  long long lowBytes_ = 0;
};

}  // namespace balance_notice
}  // namespace urnw
