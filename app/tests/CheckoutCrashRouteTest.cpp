// The upgrade sheet's answer to a dead checkout web process
// (CheckoutCrashRoute.hpp, UPGRADE.md D4): after the page loaded the card may
// have been charged, so the sheet confirms with the server instead of
// returning to the plans with "something went wrong" (which invites a second
// purchase). The sheet needs GTK and WebKit, so its wiring is read as text.
//
// SPDX-License-Identifier: MPL-2.0
#include "CheckoutCrashRoute.hpp"
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::CheckoutCrashRoute;
using urnw::RouteCheckoutCrash;

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of the function whose definition starts with `signature`, or ""
// (braces balanced from the first '{' after the signature).
std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t at = source.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t open = source.find('{', at);
  if (open == std::string::npos) return std::string();
  int depth = 0;
  for (size_t i = open; i < source.size(); ++i) {
    if (source[i] == '{') ++depth;
    if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
  }
  return std::string();
}

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

UR_TEST(checkoutCrashAfterLoadConfirmsThePayment) {
  UR_EXPECT_TRUE(RouteCheckoutCrash(/*pageLoaded=*/true, /*fallbackTried=*/false) ==
                 CheckoutCrashRoute::ConfirmPayment);
  // a loaded page confirms even when an earlier load failure already fell back
  UR_EXPECT_TRUE(RouteCheckoutCrash(true, true) == CheckoutCrashRoute::ConfirmPayment);
}

UR_TEST(checkoutCrashBeforeLoadRetriesOnceThenErrors) {
  UR_EXPECT_TRUE(RouteCheckoutCrash(false, false) == CheckoutCrashRoute::RetryCheckout);
  UR_EXPECT_TRUE(RouteCheckoutCrash(false, true) == CheckoutCrashRoute::ShowError);
}

UR_TEST(webProcessTerminationIsRoutedThroughTheCrashRoute) {
  const std::string sheet = ReadSource("UpgradeSheet.cpp");
  UR_EXPECT_TRUE(Has(sheet, "\"web-process-terminated\""));
  UR_EXPECT_TRUE(Has(sheet, "OnWebProcessTerminated()"));
  const std::string body = FunctionBody(sheet, "void UpgradeSheet::OnWebProcessTerminated()");
  UR_EXPECT_TRUE(Has(body, "RouteCheckoutCrash(pageLoaded_, webFallbackTried_)"));
  UR_EXPECT_TRUE(Has(body, "\"checkout_interrupted_confirming\""));
  UR_EXPECT_TRUE(Has(body, "balance_.StartConfirmationPolling()"));
  UR_EXPECT_TRUE(Has(body, "SetState(State::Waiting)"));
  UR_EXPECT_TRUE(Has(body, "OnCheckoutLoadFailed()"));
}

}  // namespace
