// What the upgrade sheet says when its checkout page fails (UpgradeSheet.cpp
// HandleCheckoutCallback; windows CheckoutBridgeError.h parity).
//
// The ur.io checkout page hands a failure back as
// urnetwork://checkout?errorCode=<code>&errorMessage=<its English text>, and the
// SDK's urnet::parseCheckoutRedirect passes the code on as sent
// (CheckoutRedirect::ErrorCode, one of urnet::CheckoutBridgeError*). A code
// this app knows reads in its own words; any other failure reads in the page's
// text (the page's invalid_request and checkout_error, whose text says what, a
// code this app does not know, and the -1 of pages before the codes), and one
// without a text says something went wrong. The pay page's failure carries no
// code and reads the same way.
//
// Dependency-free so tests/CheckoutBridgeErrorTest.cpp runs it without WebKit
// or the SDK.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <string_view>

namespace urnw {

// The words for a failed hand-back: a store key and its English (the gettext
// msgid), or, when the key is null, the page's own text.
struct CheckoutFailureText {
  const char* key;
  const char* english;
  std::string pageText;
};

inline CheckoutFailureText CheckoutFailureTextFor(std::string_view code,
                                                  const std::string& errorMessage) {
  if (code == "checkout_unavailable") {
    return {"checkout_error_unavailable",
            "Checkout isn't available right now. Please try again later.", std::string()};
  }
  if (code == "stripe_unavailable") {
    return {"checkout_error_payment_form_unavailable",
            "The payment form couldn't load. Check your internet connection, then try again.",
            std::string()};
  }
  if (!errorMessage.empty()) return {nullptr, nullptr, errorMessage};
  return {"something_went_wrong_please_try_again_later",
          "Something went wrong. Please try again later.", std::string()};
}

}  // namespace urnw
