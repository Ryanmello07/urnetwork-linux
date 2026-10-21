// Whether the Manage Subscription row (the Stripe customer portal) shows, from
// the current subscription's store family (urnet::classifySubscriptionStore,
// sdk payment_catalog.go). The portal can only manage a Stripe subscription:
// an App Store or Play subscription is managed in that store, a one-time
// crypto year (family "crypto": USDC on Solana, Zcash, Monero) never renews
// and has nothing to manage (Account says "does not renew" instead), and a
// free network has nothing to manage — for those the row only produced the
// portal's raw error (UPGRADE.md D7).
//
// Dependency-free so tests/ManageSubscriptionTest.cpp runs it without the SDK.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {

// storeFamily: urnet::classifySubscriptionStore(current_subscription.store),
// or "" without a current subscription
inline bool ShowsManageSubscription(const std::string& storeFamily) {
  return storeFamily == "stripe";  // urnet::SubscriptionStoreStripe
}

}  // namespace urnw
