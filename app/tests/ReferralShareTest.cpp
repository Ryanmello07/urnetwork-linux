// Support inbox 1698: the copied invitation had been the code alone since
// 2026-07, so a friend on Android had no link that opens the app (or Play,
// with the install referrer) with the code applied. The invitation now
// carries the code's ur.io/c link after the message, which still names the
// code.
//
// SPDX-License-Identifier: MPL-2.0
#include "NetworkSpaceBootstrap.hpp"
#include "ReferralShare.hpp"
#include "TestHarness.hpp"

namespace {

using urnw::kUrLinkHostName;
using urnw::ReferralLinkUrl;
using urnw::ReferralShareText;

constexpr const char* kMessage =
    "Join me on URnetwork! Get the app and enter referral code AB12CD when you sign up.";

UR_TEST(referralShareLinkIsTheCodesUrIoConnectLink) {
  UR_EXPECT_TRUE(ReferralLinkUrl(kUrLinkHostName, "AB12CD") == "https://ur.io/c?bonus=AB12CD");
  // older, longer codes pass through for the server to validate
  UR_EXPECT_TRUE(ReferralLinkUrl(kUrLinkHostName, "9f1c-22ab") ==
                 "https://ur.io/c?bonus=9f1c-22ab");
}

UR_TEST(referralShareLinkNeedsAPlainCode) {
  UR_EXPECT_TRUE(ReferralLinkUrl(kUrLinkHostName, "").empty());
  // a code can never add a parameter to the link
  UR_EXPECT_TRUE(ReferralLinkUrl(kUrLinkHostName, "A&auth_code=x").empty());
  UR_EXPECT_TRUE(ReferralLinkUrl(kUrLinkHostName, "AB 12").empty());
}

UR_TEST(referralShareTextIsTheMessageThenTheLink) {
  UR_EXPECT_TRUE(ReferralShareText(kMessage, ReferralLinkUrl(kUrLinkHostName, "AB12CD")) ==
                 std::string(kMessage) + "\nhttps://ur.io/c?bonus=AB12CD");
}

UR_TEST(referralShareTextWithoutALinkIsTheMessage) {
  UR_EXPECT_TRUE(ReferralShareText(kMessage, "") == kMessage);
}

}  // namespace
