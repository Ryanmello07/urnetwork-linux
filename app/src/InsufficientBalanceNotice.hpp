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

}  // namespace balance_notice
}  // namespace urnw
