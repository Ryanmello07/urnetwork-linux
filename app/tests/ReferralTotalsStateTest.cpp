// The referral count behind "Total referrals": a failed read with no count is
// an error with Try again, not "0"; a failed background read keeps a count
// already shown; Try again goes back to Loading until the answer lands.
//
// SPDX-License-Identifier: MPL-2.0
#include "ReferralTotalsState.hpp"
#include "TestHarness.hpp"

namespace {

using urnw::ReferralTotalsFetch;
using urnw::ReferralTotalsView;

UR_TEST(referralTotalsIsLoadingBeforeAnyAnswer) {
  ReferralTotalsFetch fetch;
  UR_EXPECT_TRUE(fetch.View() == ReferralTotalsView::Loading);
}

UR_TEST(referralTotalsFailedFirstReadIsUnavailableNotZero) {
  ReferralTotalsFetch fetch;
  fetch.Fail();
  UR_EXPECT_TRUE(fetch.View() == ReferralTotalsView::Unavailable);
}

UR_TEST(referralTotalsShowsTheCountIncludingZero) {
  ReferralTotalsFetch fetch;
  fetch.Succeed(0);
  UR_EXPECT_TRUE(fetch.View() == ReferralTotalsView::Count);
  UR_EXPECT_EQ(int64_t{0}, fetch.Total());
  fetch.Succeed(4);
  UR_EXPECT_EQ(int64_t{4}, fetch.Total());
}

UR_TEST(referralTotalsFailedPollKeepsTheCount) {
  ReferralTotalsFetch fetch;
  fetch.Succeed(3);
  fetch.Fail();
  UR_EXPECT_TRUE(fetch.View() == ReferralTotalsView::Count);
  UR_EXPECT_EQ(int64_t{3}, fetch.Total());
}

UR_TEST(referralTotalsTryAgainGoesBackToLoading) {
  ReferralTotalsFetch fetch;
  fetch.Fail();
  fetch.Retry();
  UR_EXPECT_TRUE(fetch.View() == ReferralTotalsView::Loading);
  fetch.Succeed(2);
  UR_EXPECT_TRUE(fetch.View() == ReferralTotalsView::Count);
  UR_EXPECT_EQ(int64_t{2}, fetch.Total());
}

UR_TEST(referralTotalsLogoutDropsTheCount) {
  ReferralTotalsFetch fetch;
  fetch.Succeed(5);
  fetch.Reset();
  UR_EXPECT_TRUE(fetch.View() == ReferralTotalsView::Loading);
  UR_EXPECT_EQ(int64_t{0}, fetch.Total());
}

}  // namespace
