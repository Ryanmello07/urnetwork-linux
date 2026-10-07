// Location row clicks, coalesced (SdkHost::ConnectFromRow), as Windows has
// them. Every row click is a real connect, and every connect makes the SDK tear
// the provider window down and queue a rebuild behind its dial pacing, which
// reserves 100 ms to 1 s of shared dial budget per cold dial and gives none of
// it back when the waiter is cancelled. A burst of clicks through the location
// list therefore runs that pacing minutes ahead, and every window built after
// it waits on dials that are not due.
//
// The rules:
//   * a click replaces the pending intent and its deadline (last wins), and the
//     intent is taken only after it has been still for kSettleMillis;
//   * a click on the target the session is already driving is no connect, and
//     drops a newer intent still settling, so "click away, think better of it,
//     click back" ends where it started;
//   * the immediate gestures (the Connect button, the tray, Disconnect,
//     sign-out) cancel a settling intent.
//
// Pure and time-injected, so tests/RowConnectCoalescerTest.cpp runs it without
// a main loop; SdkHost owns the timer that asks TakeDue. Not thread safe: the
// host uses it on the GTK main loop only.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace urnw {

template <class Target>
class RowConnectCoalescer {
 public:
  // Long enough to absorb a scroll-and-click hunt through the list, short
  // enough that one deliberate click still feels acted on (Windows' value).
  static constexpr int64_t kSettleMillis = 1200;

  // A row click. `driving`: the session is up and its selection is `target`.
  // True when an intent is now pending, due at DueAtMillis; false for the
  // no-op re-click.
  bool Offer(Target target, bool driving, int64_t nowMillis) {
    if (driving) {
      pending_ = false;
      return false;
    }
    target_ = std::move(target);
    pending_ = true;
    dueAtMillis_ = nowMillis + kSettleMillis;
    return true;
  }

  // The pending intent once it has settled, taken; nullopt before its deadline
  // or with nothing pending.
  std::optional<Target> TakeDue(int64_t nowMillis) {
    if (!pending_ || nowMillis < dueAtMillis_) return std::nullopt;
    pending_ = false;
    return std::move(target_);
  }

  bool Pending() const { return pending_; }
  int64_t DueAtMillis() const { return dueAtMillis_; }

  // Drops a settling intent. True when there was one.
  bool Cancel() {
    const bool had = pending_;
    pending_ = false;
    return had;
  }

 private:
  Target target_{};
  bool pending_ = false;
  int64_t dueAtMillis_ = 0;
};

}  // namespace urnw
