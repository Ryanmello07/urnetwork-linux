// Support inbox 1698: invitees missed the referral code entry, a flat "Add
// referral code" toggle with an Apply button. Sign-up now shows the optional
// field above Continue (the Windows sign-up's bonus code box): what the typed
// text means, what the create call carries, and that an answer for changed
// text is dropped are pinned here, as is the Referrals page's "Add referral
// code" action.
//
// SPDX-License-Identifier: MPL-2.0
#include "ReferralCodeField.hpp"
#include "TestHarness.hpp"

namespace {

using urnw::NormalizeReferralCode;
using urnw::ReferralCodeEntry;
using urnw::ReferralCodeForCreate;
using urnw::ReferralCodeVerdict;
using urnw::ReferralCodeVerdictOf;
using urnw::ReferralNetworkOffersAddCode;

UR_TEST(referralCodeIsTrimmedAndUpperCased) {
  UR_EXPECT_TRUE(NormalizeReferralCode("  ab12cd \n") == "AB12CD");
  UR_EXPECT_TRUE(NormalizeReferralCode("9f1c-22ab") == "9F1C-22AB");
  UR_EXPECT_TRUE(NormalizeReferralCode("   ").empty());
}

UR_TEST(referralCodeVerdictIsTheServersAnswer) {
  UR_EXPECT_TRUE(ReferralCodeVerdictOf(true, true, false) == ReferralCodeVerdict::Valid);
  UR_EXPECT_TRUE(ReferralCodeVerdictOf(true, false, false) == ReferralCodeVerdict::Invalid);
  UR_EXPECT_TRUE(ReferralCodeVerdictOf(true, true, true) == ReferralCodeVerdict::Capped);
  UR_EXPECT_TRUE(ReferralCodeVerdictOf(false, false, false) == ReferralCodeVerdict::CheckFailed);
}

UR_TEST(referralCodeSignUpCarriesATypedCode) {
  // valid, or not judged yet (Continue right after typing, a check that did
  // not answer): the server checks the code again on create
  for (auto verdict : {ReferralCodeVerdict::Valid, ReferralCodeVerdict::Unchecked,
                       ReferralCodeVerdict::Checking, ReferralCodeVerdict::CheckFailed}) {
    UR_EXPECT_TRUE(ReferralCodeForCreate(" ab12cd ", verdict) == "AB12CD");
  }
  UR_EXPECT_TRUE(ReferralCodeForCreate("AB12CD", ReferralCodeVerdict::Invalid).empty());
  UR_EXPECT_TRUE(ReferralCodeForCreate("AB12CD", ReferralCodeVerdict::Capped).empty());
  UR_EXPECT_TRUE(ReferralCodeForCreate("  ", ReferralCodeVerdict::Valid).empty());
}

UR_TEST(referralCodeTypingSchedulesACheckOfTheCode) {
  ReferralCodeEntry entry;
  UR_EXPECT_TRUE(entry.Edit(" ab12cd "));
  UR_EXPECT_TRUE(entry.Code() == "AB12CD");
  // the same code retyped is not a new check
  UR_EXPECT_TRUE(!entry.Edit("AB12CD "));
  // an empty field is no code at all
  UR_EXPECT_TRUE(!entry.Edit("   "));
  UR_EXPECT_TRUE(!entry.BeginCheck().has_value());
  UR_EXPECT_TRUE(entry.CreateCode().empty());
}

UR_TEST(referralCodeContinueRightAfterTypingCarriesTheCode) {
  ReferralCodeEntry entry;
  entry.Edit("AB12CD");
  UR_EXPECT_TRUE(entry.Verdict() == ReferralCodeVerdict::Unchecked);
  UR_EXPECT_TRUE(entry.CreateCode() == "AB12CD");
  const auto edit = entry.BeginCheck();
  UR_EXPECT_TRUE(edit.has_value());
  // a second check for the same text is not sent while the first is out
  UR_EXPECT_TRUE(!entry.BeginCheck().has_value());
  UR_EXPECT_TRUE(entry.CreateCode() == "AB12CD");
}

UR_TEST(referralCodeAnswersSetTheVerdict) {
  ReferralCodeEntry entry;
  entry.Edit("AB12CD");
  auto edit = entry.BeginCheck();
  UR_EXPECT_TRUE(entry.Answer(*edit, true, true, false));
  UR_EXPECT_TRUE(entry.Verdict() == ReferralCodeVerdict::Valid);
  UR_EXPECT_TRUE(entry.CreateCode() == "AB12CD");

  entry.Edit("ZZ99ZZ");
  edit = entry.BeginCheck();
  UR_EXPECT_TRUE(entry.Answer(*edit, true, false, false));
  UR_EXPECT_TRUE(entry.Verdict() == ReferralCodeVerdict::Invalid);
  UR_EXPECT_TRUE(entry.CreateCode().empty());

  entry.Edit("QQ11QQ");
  edit = entry.BeginCheck();
  UR_EXPECT_TRUE(entry.Answer(*edit, false, false, false));
  UR_EXPECT_TRUE(entry.Verdict() == ReferralCodeVerdict::CheckFailed);
  UR_EXPECT_TRUE(entry.CreateCode() == "QQ11QQ");
}

UR_TEST(referralCodeAnswerForChangedTextIsDropped) {
  ReferralCodeEntry entry;
  entry.Edit("AB12CD");
  const auto first = entry.BeginCheck();
  // the user types another code while the check is out
  entry.Edit("ZZ99ZZ");
  UR_EXPECT_TRUE(!entry.Answer(*first, true, true, false));
  UR_EXPECT_TRUE(entry.Verdict() == ReferralCodeVerdict::Unchecked);
  UR_EXPECT_TRUE(entry.CreateCode() == "ZZ99ZZ");
}

UR_TEST(referralCodeResetDropsTheCodeAndAnswersStillOut) {
  ReferralCodeEntry entry;
  entry.Edit("AB12CD");
  const auto edit = entry.BeginCheck();
  entry.Reset();
  UR_EXPECT_TRUE(!entry.Answer(*edit, true, true, false));
  UR_EXPECT_TRUE(entry.CreateCode().empty());
  // the same code typed again after a reset is checked again
  UR_EXPECT_TRUE(entry.Edit("AB12CD"));
}

UR_TEST(referralNetworkWithNoneLinkedOffersTheCodeEntry) {
  UR_EXPECT_TRUE(ReferralNetworkOffersAddCode(true, ""));
  UR_EXPECT_TRUE(!ReferralNetworkOffersAddCode(true, "parent_network"));
  // still loading or failed: the row stays (it opens the sheet too)
  UR_EXPECT_TRUE(!ReferralNetworkOffersAddCode(false, ""));
}

}  // namespace
