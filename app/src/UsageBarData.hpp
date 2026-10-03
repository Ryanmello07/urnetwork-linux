// What the usage bar shows, read from the balance store in one place so every
// bar gets the server's referral terms with its figures. The connect drawer
// filled the bar's figures but never its terms, so its referral bonus always
// used the bar's own defaults (20 referrals, 3 GiB/day) instead of the terms
// GET /account/referral-code returned. The bar now has no terms of its own.
// Pure (the store is a template parameter) so tests/UsageBarDataTest.cpp can
// pin it without GTK or the SDK.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

#include "ReferralTotalsState.hpp"
#include "UsageBarReferralRow.hpp"

namespace urnw {

struct UsageBarData {
  int64_t usedByteCount = 0;
  int64_t pendingByteCount = 0;
  int64_t availableByteCount = 0;
  int64_t dailyBalanceByteCount = 0;
  // the referral count read (the store's TotalsView()) and its count
  ReferralTotalsView referralView = ReferralTotalsView::Loading;
  int64_t totalReferrals = 0;
  // the program's cap (0 = no cap) and per-referral bonus: the server's terms
  int64_t maxReferrals = 0;
  int64_t bonusGibPerDay = 0;
};

// Balance: SubscriptionBalanceStore (or a test fake with the same getters).
template <class Balance>
UsageBarData UsageBarDataFrom(const Balance& balance) {
  UsageBarData data;
  data.usedByteCount = balance.UsedByteCount();
  data.pendingByteCount = balance.PendingByteCount();
  data.availableByteCount = balance.AvailableByteCount();
  data.dailyBalanceByteCount = balance.StartBalanceByteCount();
  data.referralView = balance.TotalsView();
  data.totalReferrals = balance.TotalReferrals();
  data.maxReferrals = balance.MaxReferrals();
  data.bonusGibPerDay = balance.BonusGibPerDay();
  return data;
}

inline UsageBarReferralRow UsageBarReferralRowFor(const UsageBarData& data) {
  return UsageBarReferralRowFor(data.referralView, data.totalReferrals, data.maxReferrals,
                                data.bonusGibPerDay);
}

}  // namespace urnw
