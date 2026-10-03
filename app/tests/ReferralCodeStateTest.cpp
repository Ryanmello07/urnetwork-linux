// The referral code read behind the referral panel: a failed read with no code
// shows the error and Try again instead of an empty space under "Your referral
// code", a failed read repaints the panel, a failed background poll keeps a
// code already shown, and Try again goes back to Loading until the answer
// lands.
//
// SPDX-License-Identifier: MPL-2.0
#include "ReferralCodeState.hpp"
#include "TestHarness.hpp"

namespace {

using urnw::ReferralCodeFetch;
using urnw::ReferralCodeView;

constexpr const char* kTestCode = "TESTCODE1";

UR_TEST(referralCodeIsLoadingBeforeAnyAnswer) {
  ReferralCodeFetch fetch;
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Loading);
}

UR_TEST(referralCodeFailedFirstReadIsUnavailable) {
  ReferralCodeFetch fetch;
  const bool repaint = fetch.Fail();
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Unavailable);
  // the store emits only when told to: a silent failure leaves the panel as it was
  UR_EXPECT_TRUE(repaint);
}

UR_TEST(referralCodeAnsweredWithoutACodeIsUnavailable) {
  ReferralCodeFetch fetch;
  fetch.Succeed(std::string());
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Unavailable);
}

UR_TEST(referralCodeShowsTheCode) {
  ReferralCodeFetch fetch;
  fetch.Succeed(kTestCode);
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Code);
  UR_EXPECT_TRUE(fetch.Code() == kTestCode);
}

UR_TEST(referralCodeFailedPollKeepsTheCodeShown) {
  ReferralCodeFetch fetch;
  fetch.Succeed(kTestCode);
  fetch.Fail();
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Code);
  UR_EXPECT_TRUE(fetch.Code() == kTestCode);
}

UR_TEST(referralCodeRetryLoadsUntilTheAnswerLands) {
  ReferralCodeFetch fetch;
  fetch.Fail();
  fetch.Retry();
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Loading);
  fetch.Fail();
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Unavailable);
  fetch.Retry();
  fetch.Succeed(kTestCode);
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Code);
}

UR_TEST(referralCodeRetryKeepsTheCodeShown) {
  ReferralCodeFetch fetch;
  fetch.Succeed(kTestCode);
  fetch.Retry();
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Code);
}

UR_TEST(referralCodeResetForgetsTheCodeAndTheAnswer) {
  ReferralCodeFetch fetch;
  fetch.Succeed(kTestCode);
  fetch.Reset();
  UR_EXPECT_TRUE(fetch.View() == ReferralCodeView::Loading);
  UR_EXPECT_TRUE(fetch.Code().empty());
}

}  // namespace
