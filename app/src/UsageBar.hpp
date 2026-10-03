// Usage bar (port of the apple Shared/Views/UsageBar.swift, with the android
// port's legend): the stacked used / pending / available balance bar, the
// series legend, the daily-data-balance row, and the referral row
// ("Total referrals: N" / "+N GiB/Day": the GiB per day the network's paid
// referrals earn, capped and priced by the server's referral terms). Non-zero
// segments are widened to a 1.5% floor so a sliver of pending data still reads
// (mac minNonZeroValue), and the bar's 12px corner radius matches the card
// system.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>

#include <gtkmm.h>

#include "UsageBarData.hpp"

namespace urnw {

class UsageBar : public Gtk::Box {
 public:
  UsageBar();

  // UsageBarDataFrom(store): the figures with the server's referral terms; the
  // referral row follows the count read, see UsageBarReferralRow.hpp
  void SetData(const UsageBarData& data);
  // the referral row; off where referrals have their own page
  void SetShowReferrals(bool show);

  // The referral row is a flat button: tapping "Total referrals" opens the one
  // Referrals page (Account > Referrals), the same as the android/apple drawer
  // row. Unset, the row is inert.
  std::function<void()> on_referrals;

 private:
  void ApplyReferralRow();
  void UpdateReferralAccessibleName();
  void DrawBar(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

  Gtk::DrawingArea bar_;
  Gtk::Label* dailyBalanceValue_ = nullptr;
  Gtk::Label* referralCount_ = nullptr;
  Gtk::Label* referralBonus_ = nullptr;
  Gtk::Separator* referralSeparator_ = nullptr;
  Gtk::Box* referralRow_ = nullptr;
  Gtk::Button* referralButton_ = nullptr;  // wraps referralRow_
  UsageBarData data_;
};

}  // namespace urnw
