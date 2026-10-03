// The usage bar's referral row ("Total referrals: N" / "+N GiB/Day"), decided
// pure so tests/UsageBarReferralRowTest.cpp can pin it without GTK or the SDK.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

#include "ReferralTotalsState.hpp"

namespace urnw {

// What the row shows.
enum class UsageBarReferralKind {
  Loading,      // the referral read has not answered: no figures yet
  Unavailable,  // the read failed with no count to show: no figures
  Earned,       // the count and the GiB/day it earns
};

struct UsageBarReferralRow {
  UsageBarReferralKind kind = UsageBarReferralKind::Loading;
  int64_t totalReferrals = 0;
  int64_t bonusGibPerDay = 0;
};

// The row for the referral count read (ReferralTotalsFetch) and the server's
// terms (maxReferrals 0 = no cap). The count is 0 until the read lands and
// stays 0 when it fails, so the raw count alone read as "+0 GiB/Day" for every
// new user; the figures wait for the read. A failed background read keeps a
// count already shown (the view stays Count).
inline UsageBarReferralRow UsageBarReferralRowFor(ReferralTotalsView view, int64_t totalReferrals,
                                                  int64_t maxReferrals, int64_t bonusGibPerDay) {
  switch (view) {
    case ReferralTotalsView::Loading: return {UsageBarReferralKind::Loading};
    case ReferralTotalsView::Unavailable: return {UsageBarReferralKind::Unavailable};
    case ReferralTotalsView::Count: break;
  }
  int64_t paid = (0 < maxReferrals && maxReferrals < totalReferrals) ? maxReferrals : totalReferrals;
  if (paid < 0) paid = 0;
  return {UsageBarReferralKind::Earned, totalReferrals, paid * bonusGibPerDay};
}

}  // namespace urnw
