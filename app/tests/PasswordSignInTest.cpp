// Change password is offered only to an account with an email or phone
// password sign-in, at that sign-in's address.
//
// SPDX-License-Identifier: MPL-2.0
#include "PasswordSignIn.hpp"
#include "TestHarness.hpp"

#include <optional>
#include <string>
#include <vector>

namespace {

using urnw::account::PasswordResetUserAuth;

const std::optional<std::string> kEmail = std::string("user@example.com");

UR_TEST(passwordResetIsNotOfferedToSsoOnlyAccounts) {
  UR_EXPECT_TRUE_MSG("a Google-only account is not offered a password reset",
                     PasswordResetUserAuth(kEmail, "google", std::vector<std::string>{"google"}).empty());
  UR_EXPECT_TRUE_MSG("an Apple-only account is not offered a password reset",
                     PasswordResetUserAuth(kEmail, "apple", std::vector<std::string>{"apple"}).empty());
}

UR_TEST(passwordResetIsOfferedToPasswordAccountsAtTheirAddress) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(kEmail, "password",
                                       std::vector<std::string>{"email", "google"}) ==
                 "user@example.com");
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::string("+15555550100"), "password",
                                       std::vector<std::string>{"phone"}) == "+15555550100");
}

UR_TEST(passwordResetReadsTheSingleAuthTypeFromAnOldServer) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(kEmail, "password", std::nullopt) == "user@example.com");
  UR_EXPECT_TRUE(PasswordResetUserAuth(kEmail, "google", std::vector<std::string>{}).empty());
}

UR_TEST(passwordResetNeedsAnAddress) {
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::nullopt, "solana",
                                       std::vector<std::string>{"solana", "email"})
                     .empty());
  UR_EXPECT_TRUE(PasswordResetUserAuth(std::string(), "password",
                                       std::vector<std::string>{"email"})
                     .empty());
}

}  // namespace
