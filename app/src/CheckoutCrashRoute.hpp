// Where the upgrade sheet goes when the embedded checkout's web process dies
// (WebKit "web-process-terminated"). After the page loaded, the payment form
// was in front of the user and the card may already have been charged with
// only the hand-back lost: confirm with the server (the waiting state with
// checkout_interrupted_confirming and the confirmation poll), never invite a
// second purchase. Before it loaded nothing was shown, so nothing was paid:
// the load-failure rescue (the next checkout path) still saves the purchase,
// once; after that rescue was spent, an inline error (UPGRADE.md D4, windows
// CoreProcessFailed parity).
//
// Dependency-free so tests/CheckoutCrashRouteTest.cpp runs it without WebKit.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw {

enum class CheckoutCrashRoute {
  ConfirmPayment,  // waiting + confirmation poll
  RetryCheckout,   // OnCheckoutLoadFailed: the next checkout path
  ShowError,       // back to the plans with "something went wrong"
};

inline CheckoutCrashRoute RouteCheckoutCrash(bool pageLoaded, bool fallbackTried) {
  if (pageLoaded) return CheckoutCrashRoute::ConfirmPayment;
  if (!fallbackTried) return CheckoutCrashRoute::RetryCheckout;
  return CheckoutCrashRoute::ShowError;
}

}  // namespace urnw
