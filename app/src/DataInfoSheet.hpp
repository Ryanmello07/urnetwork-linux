// "About your data": what Used, Pending and Available mean, the daily balance
// the server reports and when the free data refreshes (DataInfo.hpp), from the
// window's balance store. Opened from the info button on the Account page's
// data-usage group and from Why? in the Connect page's out-of-balance alert.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <gtkmm.h>

#include "InsufficientBalanceNotice.hpp"
#include "SubscriptionBalance.hpp"

namespace urnw {

// The time left until the next 00:00 UTC as a compact duration ("5h 12m"),
// with the provider_connected_duration strings.
std::string FreeRefreshCountdownText();

// The line under the refresh line that says whether the missing data is
// reserved by open connections, with the reserved amount, or used up
// (balance_notice::OutOfBalanceKindFor); empty for neither. The Connect page's
// alert and the upgrade sheet a blocked connect opens both show it.
std::string OutOfBalanceKindText(balance_notice::OutOfBalanceKind kind, int64_t reservedByteCount);

// Calls `apply` now and again each time the displayed countdown changes, on
// the GTK loop, until Stop. Keeps a "free data refreshes in {time}" line
// current: the Connect page's alert, this sheet and the upgrade sheet.
class FreeRefreshTicker {
 public:
  FreeRefreshTicker() = default;
  FreeRefreshTicker(const FreeRefreshTicker&) = delete;
  FreeRefreshTicker& operator=(const FreeRefreshTicker&) = delete;
  ~FreeRefreshTicker() { Stop(); }

  void Start(std::function<void()> apply);
  void Stop();
  bool Running() const { return static_cast<bool>(apply_); }

 private:
  void Arm();

  std::function<void()> apply_;
  sigc::connection timeout_;
};

class DataInfoSheet : public Gtk::Window {
 public:
  DataInfoSheet(Gtk::Window& parent, SubscriptionBalanceStore& balance);

  // Fills the amounts from the store's last balance and presents the sheet.
  void Open();

 private:
  SubscriptionBalanceStore& balance_;
  Gtk::Label* usedValue_ = nullptr;
  Gtk::Label* pendingValue_ = nullptr;
  Gtk::Label* availableValue_ = nullptr;
  Gtk::Label* dailyValue_ = nullptr;
  Gtk::Label* refreshLabel_ = nullptr;
  FreeRefreshTicker refreshTicker_;
};

}  // namespace urnw
