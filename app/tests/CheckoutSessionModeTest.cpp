// The upgrade sheet's embedded checkout is a redirect_on_completion "never"
// session opened on the SDK's inline bridge url (CheckoutSessionMode.hpp), so
// the payment hands back from Stripe's onComplete on ur.io/checkout instead of
// a return_url round trip, and a completed hand-back still starts the
// confirmation poll. The sheet needs GTK, WebKit and the SDK, so its wiring is
// read as text.
//
// SPDX-License-Identifier: MPL-2.0
#include "CheckoutSessionMode.hpp"
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::CheckoutSessionModeFor;

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

UR_TEST(embeddedCheckoutCompletesThroughOnComplete) {
  const auto embedded = CheckoutSessionModeFor(true);
  UR_EXPECT_TRUE(embedded.uiMode == "embedded");
  UR_EXPECT_TRUE(embedded.redirectOnCompletion == "never");
  UR_EXPECT_TRUE(embedded.inlineBridge);
}

UR_TEST(hostedCheckoutLeavesRedirectOnCompletionUnset) {
  const auto hosted = CheckoutSessionModeFor(false);
  UR_EXPECT_TRUE(hosted.uiMode == "hosted");
  UR_EXPECT_TRUE(hosted.redirectOnCompletion == "");
  UR_EXPECT_FALSE(hosted.inlineBridge);
}

UR_TEST(upgradeSheetOpensNeverSessionsOnTheInlineBridge) {
  const std::string sheet = ReadSource("UpgradeSheet.cpp");
  UR_EXPECT_TRUE(Has(sheet, "CheckoutSessionModeFor(embedded)"));
  UR_EXPECT_TRUE(Has(sheet, "args.redirect_on_completion = mode.redirectOnCompletion"));
  UR_EXPECT_TRUE(Has(sheet, "urnet::buildInlineCheckoutBridgeUrl(clientSecret)"));
  // a plain bridge url would never hand back for a "never" session
  UR_EXPECT_FALSE(Has(sheet, "urnet::buildCheckoutBridgeUrl(clientSecret)"));
  UR_EXPECT_FALSE(Has(sheet, "redirect_on_completion stays unset"));
  // the hand-back still waits for the webhook
  UR_EXPECT_TRUE(Has(sheet, "balance_.StartConfirmationPolling();"));
}

}  // namespace
