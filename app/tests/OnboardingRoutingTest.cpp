// The onboarding flow's routing around the offer page: everyone in the
// experiment reaches it (Skip from any earlier page lands on it once, the
// referral page's button goes there), only its own link finishes the flow,
// and the holdout never sees it.
//
// SPDX-License-Identifier: MPL-2.0
#include "OnboardingRouting.hpp"
#include "TestHarness.hpp"

namespace {

using namespace urnw;

UR_TEST(offerPageShowsUnlessHoldout) {
  UR_EXPECT_TRUE(OfferPageEnabled("in_app_offer"));
  UR_EXPECT_TRUE(OfferPageEnabled(""));  // unassigned: the offer is the product
  UR_EXPECT_FALSE(OfferPageEnabled(kExperimentHoldout));
  UR_EXPECT_EQ(5, OnboardingStepCount(true));
  UR_EXPECT_EQ(4, OnboardingStepCount(false));
}

UR_TEST(skipLandsOnTheOfferPageFromEveryEarlierPage) {
  for (int step = kOnboardingStepWelcome; step <= kOnboardingStepReferral; ++step) {
    UR_EXPECT_EQ(kOnboardingStepOffer, OnboardingSkipTarget(step, true));
  }
}

UR_TEST(skipFromTheOfferPageFinishes) {
  UR_EXPECT_EQ(0, OnboardingSkipTarget(kOnboardingStepOffer, true));
}

UR_TEST(holdoutSkipFinishesImmediately) {
  for (int step = kOnboardingStepWelcome; step <= kOnboardingStepReferral; ++step) {
    UR_EXPECT_EQ(0, OnboardingSkipTarget(step, false));
  }
}

UR_TEST(referralPageLeadsToTheOfferOrFinishes) {
  UR_EXPECT_EQ(kOnboardingStepOffer, OnboardingReferralNext(true));
  UR_EXPECT_EQ(0, OnboardingReferralNext(false));
}

UR_TEST(offerPageHidesSkip) {
  UR_EXPECT_TRUE(OnboardingShowsSkip(kOnboardingStepReferral, true));
  UR_EXPECT_FALSE(OnboardingShowsSkip(kOnboardingStepOffer, true));
  UR_EXPECT_TRUE(OnboardingShowsSkip(kOnboardingStepReferral, false));
}

UR_TEST(stepNamesMatchTheEventVocabulary) {
  UR_EXPECT_TRUE(std::string(OnboardingStepName(1)) == "welcome");
  UR_EXPECT_TRUE(std::string(OnboardingStepName(2)) == "bandwidth");
  UR_EXPECT_TRUE(std::string(OnboardingStepName(3)) == "provide");
  UR_EXPECT_TRUE(std::string(OnboardingStepName(4)) == "referral");
  UR_EXPECT_TRUE(std::string(OnboardingStepName(5)) == "offer");
}

UR_TEST(deepLinksRouteByStep) {
  UR_EXPECT_TRUE(ParseOnboardingLink("urnetwork://onboarding/connect") == OnboardingLink::Connect);
  UR_EXPECT_TRUE(ParseOnboardingLink("urnetwork://onboarding/widgets") == OnboardingLink::Widgets);
  UR_EXPECT_TRUE(ParseOnboardingLink("urnetwork://onboarding/offer?t=abc") == OnboardingLink::Offer);
  UR_EXPECT_TRUE(ParseOnboardingLink("urnetwork://onboarding/feedback?token=x&r=4&why=speed") ==
                 OnboardingLink::Feedback);
  UR_EXPECT_TRUE(ParseOnboardingLink("urnetwork://onboarding/unknown") == OnboardingLink::None);
  UR_EXPECT_TRUE(ParseOnboardingLink("urnetwork://checkout?status=complete") == OnboardingLink::None);
  UR_EXPECT_TRUE(ParseOnboardingLink("https://ur.io/onboarding/offer") == OnboardingLink::None);
}

}  // namespace
