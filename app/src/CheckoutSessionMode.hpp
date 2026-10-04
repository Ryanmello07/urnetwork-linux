// The upgrade sheet's checkout session shape (UpgradeSheet.cpp; windows
// CheckoutSessionMode.h parity).
//
// Dependency-free so tests/CheckoutSessionModeTest.cpp runs it without WebKit
// or the SDK.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {

// The checkout session the upgrade sheet asks Stripe for, and how its page
// hands control back. The strings are the SDK's (urnet::StripeUiModeEmbedded,
// urnet::StripeUiModeHosted, urnet::StripeRedirectOnCompletionNever).
//
// Embedded sessions are "never": Stripe fires onComplete on the ur.io/checkout
// bridge instead of redirecting the webview through the server's return_url,
// and the bridge hands back from that callback only when its url says the
// session is "never" (urnet::buildInlineCheckoutBridgeUrl). The two go
// together: a "never" session on the plain bridge url would finish with no
// hand-back. Hosted sessions run in the browser and leave
// redirect_on_completion unset.
struct CheckoutSessionMode {
  std::string uiMode;
  // empty: redirect_on_completion stays unset
  std::string redirectOnCompletion;
  // open the session with urnet::buildInlineCheckoutBridgeUrl
  bool inlineBridge = false;
};

inline CheckoutSessionMode CheckoutSessionModeFor(bool embedded) {
  if (embedded) {
    return {"embedded", "never", true};
  }
  return {"hosted", "", false};
}

}  // namespace urnw
