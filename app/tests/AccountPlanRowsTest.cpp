// The Account page's Plan pane figures (AccountPlanRows.hpp): the daily
// balance and the usage bar's Used, Pending and Available show their figure
// once a snapshot has landed and their state before it, never "0 B"; the
// referral rule sits under the referral numbers; and the Balance Codes count
// shows for every answered fetch, zero included, and hides otherwise. The page
// needs GTK and the SDK, so the wiring cases read AccountPage.cpp with the
// comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "AccountPlanRows.hpp"
#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::account_plan::CodeCountText;
using urnw::account_plan::FigureFor;
using urnw::account_plan::FigureView;

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadPlanRowsSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  bool inString = false;
  for (size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (inString) {
      if (c == '\\') {
        ++at;
      } else if (c == '"' || c == '\n') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
      while (at < text.size() && text[at] != '\n') text[at++] = ' ';
    }
  }
  return text;
}

std::string PlanRowsBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Each needle occurs after the one before it.
bool PlanRowsInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

}  // namespace

UR_TEST(AccountPlanRows_AFigureWaitsForItsSnapshot) {
  UR_EXPECT_TRUE(FigureFor(/*snapshotLoaded=*/true, /*canCallApi=*/true) == FigureView::Figure);
  // a snapshot already landed is the figure, even after the session went
  UR_EXPECT_TRUE(FigureFor(true, false) == FigureView::Figure);
  UR_EXPECT_TRUE(FigureFor(false, true) == FigureView::Loading);
  UR_EXPECT_TRUE(FigureFor(false, false) == FigureView::NoSession);
}

UR_TEST(AccountPlanRows_EveryAnsweredFetchHasACount) {
  UR_EXPECT_TRUE(CodeCountText(/*answered=*/true, 3) == std::optional<std::string>("3"));
  // zero is an answer
  UR_EXPECT_TRUE(CodeCountText(true, 0) == std::optional<std::string>("0"));
  // no session, loading, failed: no row, whatever the list held before
  UR_EXPECT_FALSE(CodeCountText(false, 0).has_value());
  UR_EXPECT_FALSE(CodeCountText(false, 5).has_value());
}

// Pane A's rows in Windows' order: the daily balance, the bar's three figures
// under the legend's keys, the referral pair and its rule, the codes count,
// then Redeem.
UR_TEST(AccountPlanRowsWiring_ThePaneBuildsTheRowsInOrder) {
  const std::string page = ReadPlanRowsSource("AccountPage.cpp");
  const std::string build = PlanRowsBody(page, "void AccountPage::BuildPlanPane() {");
  UR_EXPECT_TRUE(PlanRowsInOrder(
      build, {"dailyValue_ = row.value;", "T_(\"used_data_key\", \"Used\")), &usedValue_",
              "T_(\"pending_data_key\", \"Pending\")), &pendingValue_",
              "T_(\"available_data_key\", \"Available\")), &availableValue_",
              "referralBonus_ = row.value;", "referralDetail_ = row.line;",
              "T_(\"balance_codes_title\", \"Balance Codes\")", "codesCountRow_ = row.root;",
              "codesCountRow_->set_visible(false);", "T_(\"redeem_balance_code\""}));
}

// Every figure goes through the one rule, and the referral rule reads the
// terms the bonus is computed with, not a fixed amount.
UR_TEST(AccountPlanRowsWiring_TheBalanceRelayFillsEveryFigure) {
  const std::string page = ReadPlanRowsSource("AccountPage.cpp");
  const std::string figure = PlanRowsBody(page, "void ApplyBalanceFigure(");
  UR_EXPECT_TRUE(PlanRowsInOrder(
      figure, {"account_plan::FigureFor(snapshotLoaded, canCallApi)", "FigureView::Figure:",
               "FormatByteCountCompact(byteCount)", "FigureView::Loading:",
               "AccountFieldState::Loading", "FigureView::NoSession:",
               "AccountFieldState::NoSession", "SetAccessibleLabel(value, key + \", \""}));
  const std::string relay =
      PlanRowsBody(page, "void AccountPage::ApplyBalance(const AccountBalance& snapshot) {");
  UR_EXPECT_TRUE(PlanRowsInOrder(
      relay, {"ApplyBalanceFigure(*dailyValue_,", "balance_.startBalanceByteCount, balance_.loaded",
              "ApplyBalanceFigure(*usedValue_,", "balance_.usedByteCount",
              "ApplyBalanceFigure(*pendingValue_,", "balance_.pendingByteCount",
              "ApplyBalanceFigure(*availableValue_,", "balance_.availableByteCount",
              "referralDetail_->set_text(Format(T_(\"referral_panel_detail\"",
              "CurrentReferralTerms().bonusGibPerDay"}));
  // no figure row writes a byte count of its own
  UR_EXPECT_TRUE(relay.find("FormatByteCountCompact") == std::string::npos);
}

// The count row is set on every render, before the table's early return, so a
// sign-out (NoSession) and a failed fetch hide it.
UR_TEST(AccountPlanRowsWiring_TheCodesRenderSetsTheCountRowFirst) {
  const std::string page = ReadPlanRowsSource("AccountPage.cpp");
  const std::string render = PlanRowsBody(page, "void AccountPage::RenderBalanceCodes() {");
  UR_EXPECT_TRUE(PlanRowsInOrder(
      render, {"account_plan::CodeCountText(",
               "codesState_ == AccountFieldState::Loaded || codesState_ == AccountFieldState::Empty",
               "codes_.size()", "codesCountRow_->set_visible(count.has_value());",
               "if (!loaded) {", "return;"}));
}
