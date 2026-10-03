// The usage bar's referral row: the raw count is 0 until the referral read
// lands and stays 0 when it fails, so the row read "Total referrals: 0" and
// "+0 GiB/Day" for a new user; the figures now wait for the read.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"
#include "UsageBarReferralRow.hpp"

namespace {

using urnw::ReferralTotalsView;
using urnw::UsageBarReferralKind;
using urnw::UsageBarReferralRowFor;

UR_TEST(usageBarReferralRowWaitsForTheRead) {
  const auto row = UsageBarReferralRowFor(ReferralTotalsView::Loading, 0, 20, 3);
  UR_EXPECT_TRUE(row.kind == UsageBarReferralKind::Loading);
}

UR_TEST(usageBarReferralRowFailedReadIsNotAZeroBonus) {
  const auto row = UsageBarReferralRowFor(ReferralTotalsView::Unavailable, 0, 20, 3);
  UR_EXPECT_TRUE(row.kind == UsageBarReferralKind::Unavailable);
}

UR_TEST(usageBarReferralRowShowsTheLandedCount) {
  auto row = UsageBarReferralRowFor(ReferralTotalsView::Count, 2, 20, 3);
  UR_EXPECT_TRUE(row.kind == UsageBarReferralKind::Earned);
  UR_EXPECT_EQ(int64_t{2}, row.totalReferrals);
  UR_EXPECT_EQ(int64_t{6}, row.bonusGibPerDay);
  // no referrals really earns nothing once the read says so
  row = UsageBarReferralRowFor(ReferralTotalsView::Count, 0, 20, 3);
  UR_EXPECT_TRUE(row.kind == UsageBarReferralKind::Earned);
  UR_EXPECT_EQ(int64_t{0}, row.bonusGibPerDay);
}

UR_TEST(usageBarReferralRowCapsTheBonus) {
  auto row = UsageBarReferralRowFor(ReferralTotalsView::Count, 25, 20, 3);
  UR_EXPECT_EQ(int64_t{60}, row.bonusGibPerDay);
  row = UsageBarReferralRowFor(ReferralTotalsView::Count, 25, 0, 3);  // 0 = no cap
  UR_EXPECT_EQ(int64_t{75}, row.bonusGibPerDay);
}

}  // namespace
