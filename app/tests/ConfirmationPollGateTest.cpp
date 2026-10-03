// The purchase-confirmation poll gate (ConfirmationPollGate.hpp): the 2-minute
// give-up budget only burns while the window is visible AND the app is
// focused, so a hosted checkout paid in the browser (the window stays visible
// behind it) never comes back to a false "timed out" (UPGRADE.md D1). The
// store and MainWindow need GTK, so their wiring is read as text.
//
// SPDX-License-Identifier: MPL-2.0
#include "ConfirmationPollGate.hpp"
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::ConfirmationPollGate;
using urnw::kConfirmationBudgetMillis;

constexpr int64_t kSecond = 1000;

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

UR_TEST(confirmationBudgetPausesWhileTheBrowserHasFocus) {
  // the report: visible behind the browser for ten minutes of card typing
  ConfirmationPollGate gate(kConfirmationBudgetMillis);
  gate.SetVisible(true, 0);
  UR_EXPECT_TRUE(gate.Start(0));
  gate.SetFocused(false, 1 * kSecond);
  UR_EXPECT_FALSE(gate.Running());
  UR_EXPECT_FALSE(gate.ExpiredAt(601 * kSecond));
  UR_EXPECT_EQ(kConfirmationBudgetMillis - 1 * kSecond, gate.RemainingAt(601 * kSecond));
  // back to the app: resume (immediate poll) with the banked remainder
  UR_EXPECT_TRUE(gate.SetFocused(true, 601 * kSecond));
  UR_EXPECT_FALSE(gate.ExpiredAt(601 * kSecond + 118 * kSecond));
  UR_EXPECT_TRUE(gate.ExpiredAt(601 * kSecond + 119 * kSecond));
}

UR_TEST(confirmationStartedUnfocusedWaitsForFocus) {
  ConfirmationPollGate gate(kConfirmationBudgetMillis);
  gate.SetVisible(true, 0);
  gate.SetFocused(false, 0);
  UR_EXPECT_FALSE(gate.Start(0));
  UR_EXPECT_FALSE(gate.ExpiredAt(30 * 60 * kSecond));
  UR_EXPECT_TRUE(gate.SetFocused(true, 30 * 60 * kSecond));
  UR_EXPECT_EQ(kConfirmationBudgetMillis, gate.RemainingAt(30 * 60 * kSecond));
}

UR_TEST(confirmationBudgetPausesWhileHidden) {
  ConfirmationPollGate gate(kConfirmationBudgetMillis);
  gate.SetVisible(true, 0);
  gate.Start(0);
  gate.SetVisible(false, 10 * kSecond);
  UR_EXPECT_FALSE(gate.Running());
  UR_EXPECT_FALSE(gate.SetFocused(true, 20 * kSecond));  // focus alone is not enough
  UR_EXPECT_TRUE(gate.SetVisible(true, 500 * kSecond));
  UR_EXPECT_EQ(kConfirmationBudgetMillis - 10 * kSecond, gate.RemainingAt(500 * kSecond));
  UR_EXPECT_FALSE(gate.SetVisible(true, 501 * kSecond));  // no second resume
}

UR_TEST(confirmationBudgetStillGivesUpInFront) {
  ConfirmationPollGate gate(kConfirmationBudgetMillis);
  gate.SetVisible(true, 0);
  gate.Start(0);
  UR_EXPECT_FALSE(gate.ExpiredAt(kConfirmationBudgetMillis - 1));
  UR_EXPECT_TRUE(gate.ExpiredAt(kConfirmationBudgetMillis));
  gate.Stop();
  UR_EXPECT_FALSE(gate.Confirming());
  UR_EXPECT_FALSE(gate.ExpiredAt(10 * kConfirmationBudgetMillis));
  UR_EXPECT_FALSE(gate.SetFocused(true, 0));
  gate.Start(100 * kSecond);
  UR_EXPECT_EQ(kConfirmationBudgetMillis, gate.RemainingAt(100 * kSecond));
}

UR_TEST(confirmationGateIsWiredToAppFocus) {
  const std::string store = ReadSource("SubscriptionBalance.cpp");
  UR_EXPECT_TRUE(Has(store, "gate_.SetFocused("));
  UR_EXPECT_TRUE(Has(store, "gate_.SetVisible("));
  UR_EXPECT_TRUE(Has(store, "gate_.ExpiredAt("));
  const std::string window = ReadSource("MainWindow.cpp");
  UR_EXPECT_TRUE(Has(window, "\"notify::is-active\""));
  UR_EXPECT_TRUE(Has(window, "balance_.SetAppFocused("));
}

}  // namespace
