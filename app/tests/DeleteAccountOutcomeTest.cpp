// The delete-account sheet's verdict: a deletion the server refuses (HTTP 200
// with an error in the result) keeps the user signed in and shows the generic
// error with the server's reason; only a result with no error signs out.
//
// SPDX-License-Identifier: MPL-2.0
#include "DeleteAccountOutcome.hpp"
#include "TestHarness.hpp"

#include <string>

namespace {

using urnw::account::DecideDeleteAccount;
using urnw::account::DeleteAccountErrorText;

const std::string kGeneric = "Sorry, there was an error deleting your account.";

UR_TEST(deleteAccountServerRefusalKeepsTheSession) {
  const std::string reason = "Could not cancel your Google Play subscription. Please try again.";
  const auto outcome = DecideDeleteAccount(nullptr, true, true, reason);
  UR_EXPECT_FALSE(outcome.deleted);
  UR_EXPECT_TRUE_MSG("a server error carries the server's message", outcome.detail == reason);
  UR_EXPECT_TRUE_MSG("the server's message follows the generic error on its own line",
                     DeleteAccountErrorText(kGeneric, outcome.detail) == kGeneric + "\n" + reason);
}

UR_TEST(deleteAccountServerRefusalWithoutMessageShowsTheGenericError) {
  const auto outcome = DecideDeleteAccount(nullptr, true, true, "  ");
  UR_EXPECT_FALSE(outcome.deleted);
  UR_EXPECT_TRUE_MSG("a blank server message falls back to the generic error alone",
                     DeleteAccountErrorText(kGeneric, outcome.detail) == kGeneric);
}

UR_TEST(deleteAccountResultWithoutErrorSignsOut) {
  UR_EXPECT_TRUE(DecideDeleteAccount(nullptr, true, false, "").deleted);
}

UR_TEST(deleteAccountTransportErrorOrNoResultKeepsTheSession) {
  const std::string err = "connection reset";
  const auto transport = DecideDeleteAccount(&err, false, false, "");
  UR_EXPECT_FALSE(transport.deleted);
  UR_EXPECT_TRUE_MSG("a transport error carries its message", transport.detail == err);
  const auto missing = DecideDeleteAccount(nullptr, false, false, "");
  UR_EXPECT_FALSE(missing.deleted);
  UR_EXPECT_TRUE_MSG("no result shows the generic error",
                     DeleteAccountErrorText(kGeneric, missing.detail) == kGeneric);
}

}  // namespace
