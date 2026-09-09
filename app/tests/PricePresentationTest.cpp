// The plan cards' price rules: the billed amount is the number, the
// per-month equivalent is a sub-line only on the standard tier, the regional
// tier prints none, and the welcome offer's first year comes off the tier's
// yearly price.
//
// SPDX-License-Identifier: MPL-2.0
#include "PricePresentation.hpp"
#include "TestHarness.hpp"

namespace {

using namespace urnw;

UR_TEST(moneyPrintsWholeAmountsWithoutDecimals) {
  UR_EXPECT_TRUE(FormatMoney(40.0, "USD") == "$40");
  UR_EXPECT_TRUE(FormatMoney(4.0, "USD") == "$4");
  UR_EXPECT_TRUE(FormatMoney(30.0, "") == "$30");
}

UR_TEST(moneyPrintsCentsWhenThereAreAny) {
  UR_EXPECT_TRUE(FormatMoney(3.34, "USD") == "$3.34");
  UR_EXPECT_TRUE(FormatMoney(0.5, "USD") == "$0.50");
  UR_EXPECT_TRUE(FormatMoney(39.99, "USD") == "$39.99");
  // half a cent rounds to the cent, never truncates
  UR_EXPECT_TRUE(FormatMoney(3.335, "USD") == "$3.34");
}

UR_TEST(moneyPrintsOtherCurrenciesAfterTheAmount) {
  UR_EXPECT_TRUE(FormatMoney(4.0, "EUR") == "4 EUR");
}

UR_TEST(standardTierShowsTheEquivalentOnlyWhenTheSdkSaysSo) {
  PriceTierView standard;  // $40 / $5
  PriceEquivalentView eq;
  eq.monthlyEquivalent = 3.34;
  eq.showEquivalent = true;
  eq.savingPercent = 33;
  UR_EXPECT_TRUE(ShowMonthlyEquivalent(standard, eq));
  UR_EXPECT_TRUE(ShowSaving(eq));
  eq.showEquivalent = false;
  UR_EXPECT_FALSE(ShowMonthlyEquivalent(standard, eq));
}

UR_TEST(regionalTierNeverShowsTheEquivalent) {
  PriceTierView regional;
  regional.name = kPriceTierRegional;
  regional.yearly = 4.0;
  regional.monthly = 0.5;
  PriceEquivalentView eq;
  eq.monthlyEquivalent = 0.34;
  eq.showEquivalent = true;  // even if the SDK would show one
  eq.savingPercent = 33;
  UR_EXPECT_FALSE(ShowMonthlyEquivalent(regional, eq));
  UR_EXPECT_TRUE(ShowSaving(eq));
  UR_EXPECT_TRUE(FormatMoney(regional.yearly, regional.currency) == "$4");
  UR_EXPECT_TRUE(FormatMoney(regional.monthly, regional.currency) == "$0.50");
}

UR_TEST(noSavingBadgeWithoutASaving) {
  PriceEquivalentView eq;
  eq.savingPercent = 0;
  UR_EXPECT_FALSE(ShowSaving(eq));
}

UR_TEST(offerFirstYearIsPercentOffTheYearlyPrice) {
  UR_EXPECT_NEAR(30.0, OfferFirstYear(40.0, 25), 1e-9);
  UR_EXPECT_NEAR(3.0, OfferFirstYear(4.0, 25), 1e-9);
  UR_EXPECT_NEAR(29.99, OfferFirstYear(39.99, 25), 0.005);
}

UR_TEST(planVocabularyFollowsTheSelection) {
  UR_EXPECT_TRUE(std::string(PlanName(true)) == "yearly");
  UR_EXPECT_TRUE(std::string(PlanName(false)) == "monthly");
  UR_EXPECT_TRUE(std::string(PlanProduct(true)) == "pro_yearly");
  UR_EXPECT_TRUE(std::string(PlanProduct(false)) == "pro_monthly");
}

}  // namespace
