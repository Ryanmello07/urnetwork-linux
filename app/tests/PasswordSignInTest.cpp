// Change password is offered only to an account with an email or phone
// password sign-in, at that sign-in's address (inbox 7962, 7969).
//
// SPDX-License-Identifier: MPL-2.0
#include "PasswordSignIn.hpp"
#include "TestHarness.hpp"

#include <optional>
#include <string>
#include <vector>

namespace {

using urnw::account::PasswordResetUserAuth;
using urnw::account::PasswordSignIn;
using urnw::account::PasswordSignIns;

const std::optional<std::string> kEmail = std::string("user@example.com");
const std::vector<PasswordSignIn> kNoSignIns = {};

// the shape of the sdk's urnet::NetworkUserUserAuth (urnetwork_sdk.hpp)
struct SdkUserAuth {
  std::optional<std::string> user_auth;
  std::string auth_type{};
};

UR_TEST(passwordResetIsNotOfferedToSsoOnlyAccounts) {
  UR_EXPECT_TRUE_MSG("a Google-only account is not offered a password reset",
                     PasswordResetUserAuth(kEmail, "google", std::vector<std::string>{"google"},
                                           kNoSignIns)
                         .empty());
  UR_EXPECT_TRUE_MSG("an Apple-only account is not offered a password reset",
                     PasswordResetUserAuth(kEmail, "apple", std::vector<std::string>{"apple"},
                                           kNoSignIns)
                         .empty());
}

UR_TEST(passwordResetIsOfferedToPasswordAccountsAtTheirAddress) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(kEmail, "password", std::vector<std::string>{"email"},
                                       {PasswordSignIn{"user@example.com", "email"}}) ==
                 "user@example.com");
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::string("+15555550100"), "password",
                                       std::vector<std::string>{"phone"},
                                       {PasswordSignIn{"+15555550100", "phone"}}) ==
                 "+15555550100");
}

// An account created with a wallet, a seed phrase or as a guest that added an
// email and password later has no user_auth: it was never offered the reset.
UR_TEST(passwordResetIsOfferedToAnAccountThatAddedAnEmailAtThatEmail) {
  const std::vector<PasswordSignIn> added = {PasswordSignIn{"added@example.com", "email"}};
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::nullopt, "solana",
                                       std::vector<std::string>{"email", "solana"},
                                       added) == "added@example.com");
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::string(), "seedphrase",
                                       std::vector<std::string>{"seedphrase", "email"},
                                       added) == "added@example.com");
}

// A Google or Apple account that added an email and password is offered the
// reset at the added email, not at the provider's.
UR_TEST(passwordResetIsOfferedToAnSsoAccountAtTheAddedEmail) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::string("provider@example.com"), "google",
                                       std::vector<std::string>{"google", "email"},
                                       {PasswordSignIn{"added@example.com", "email"}}) ==
                 "added@example.com");
}

UR_TEST(passwordResetPrefersTheEmailOverThePhone) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::string("+15555550100"), "password",
                                       std::vector<std::string>{"phone", "email"},
                                       {PasswordSignIn{"+15555550100", "phone"},
                                        PasswordSignIn{"user@example.com", "email"}}) ==
                 "user@example.com");
}

// The sdk's user_auths, absent or listed, become the password sign-ins.
UR_TEST(passwordSignInsReadTheSdkUserAuths) {
  UR_EXPECT_TRUE(PasswordSignIns(std::optional<std::vector<SdkUserAuth>>()).empty());
  const std::vector<PasswordSignIn> signIns = PasswordSignIns(std::optional<std::vector<SdkUserAuth>>(
      std::vector<SdkUserAuth>{SdkUserAuth{std::string("added@example.com"), "email"},
                               SdkUserAuth{std::nullopt, "phone"}}));
  UR_EXPECT_TRUE(signIns.size() == 2);
  UR_EXPECT_TRUE(signIns.size() == 2 && signIns[0].userAuth == "added@example.com" &&
                 signIns[0].authType == "email" && signIns[1].userAuth.empty() &&
                 signIns[1].authType == "phone");
}

UR_TEST(passwordResetReadsTheSingleAuthTypeFromAnOldServer) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(kEmail, "password", std::nullopt, kNoSignIns) ==
                 "user@example.com");
  UR_EXPECT_TRUE(PasswordResetUserAuth(kEmail, "google", std::vector<std::string>{}, kNoSignIns)
                     .empty());
  // without user_auths, the identity the account was created with is used
  UR_EXPECT_TRUE(PasswordResetUserAuth(kEmail, "password",
                                       std::vector<std::string>{"email", "google"},
                                       kNoSignIns) == "user@example.com");
}

UR_TEST(passwordResetNeedsAnAddress) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::nullopt, "solana",
                                       std::vector<std::string>{"solana", "email"}, kNoSignIns)
                     .empty());
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::string(), "password",
                                       std::vector<std::string>{"email"}, kNoSignIns)
                     .empty());
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::nullopt, "solana", std::vector<std::string>{"email"},
                                       {PasswordSignIn{"", "email"}})
                     .empty());
}

}  // namespace
