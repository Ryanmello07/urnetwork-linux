// The Manage Subscription row's visibility (ManageSubscription.hpp,
// UPGRADE.md D7): the Stripe customer portal shows only for a Stripe
// subscription, never for a free network or a store or crypto subscription.
// The store and the page need GTK and the SDK, so their wiring is read as text.
//
// SPDX-License-Identifier: MPL-2.0
#include "ManageSubscription.hpp"
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::ShowsManageSubscription;

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

UR_TEST(manageSubscriptionShowsOnlyForStripe) {
  // the families urnet::classifySubscriptionStore returns
  UR_EXPECT_TRUE(ShowsManageSubscription("stripe"));
  UR_EXPECT_FALSE(ShowsManageSubscription(""));
  UR_EXPECT_FALSE(ShowsManageSubscription("apple"));
  UR_EXPECT_FALSE(ShowsManageSubscription("google"));
  UR_EXPECT_FALSE(ShowsManageSubscription("other"));
}

UR_TEST(manageSubscriptionRowFollowsTheSubscriptionStore) {
  const std::string store = ReadSource("SubscriptionBalance.cpp");
  UR_EXPECT_TRUE(Has(store, "urnet::classifySubscriptionStore(result->current_subscription->store)"));
  const std::string window = ReadSource("MainWindow.cpp");
  UR_EXPECT_TRUE(Has(window, "snapshot.subscriptionStoreFamily = balance_.SubscriptionStoreFamily();"));
  const std::string page = ReadSource("AccountPage.cpp");
  UR_EXPECT_TRUE(Has(page, "portalRow_->set_visible(ShowsManageSubscription(balance_.subscriptionStoreFamily))"));
}

}  // namespace
