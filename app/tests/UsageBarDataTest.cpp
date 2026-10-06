// The connect drawer's usage bar filled its figures from the balance store but
// never its referral terms, so its "+N GiB/Day" always used the bar's own
// defaults (20 referrals, 3 GiB/day) instead of the terms the server returned
// with the referral code. Every usage bar now reads its figures and the
// server's terms together through UsageBarDataFrom, and the bar keeps no terms
// of its own; the store-fed bar left is onboarding's. Onboarding and UsageBar
// need GTK and the SDK, so their sources are read as text, the same way
// UpdateWiringTest does.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"
#include "UsageBarData.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::ReferralTotalsView;
using urnw::UsageBarDataFrom;
using urnw::UsageBarReferralKind;
using urnw::UsageBarReferralRowFor;

// The balance store's getters, with server terms that differ from the
// defaults (20 referrals, 3 GiB/day).
struct FakeBalance {
  int64_t UsedByteCount() const { return 1; }
  int64_t PendingByteCount() const { return 2; }
  int64_t AvailableByteCount() const { return 3; }
  int64_t StartBalanceByteCount() const { return 4; }
  ReferralTotalsView TotalsView() const { return ReferralTotalsView::Count; }
  int64_t TotalReferrals() const { return 8; }
  int64_t MaxReferrals() const { return 5; }
  int64_t BonusGibPerDay() const { return 7; }
};

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

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

}  // namespace

UR_TEST(usageBarDataCarriesTheServersReferralTerms) {
  const auto data = UsageBarDataFrom(FakeBalance{});
  UR_EXPECT_EQ(int64_t{1}, data.usedByteCount);
  UR_EXPECT_EQ(int64_t{2}, data.pendingByteCount);
  UR_EXPECT_EQ(int64_t{3}, data.availableByteCount);
  UR_EXPECT_EQ(int64_t{4}, data.dailyBalanceByteCount);
  UR_EXPECT_EQ(int64_t{5}, data.maxReferrals);
  UR_EXPECT_EQ(int64_t{7}, data.bonusGibPerDay);
  // 8 referrals, 5 paid at 7 GiB/day: 35, not the default terms' 8 * 3 = 24
  const auto row = UsageBarReferralRowFor(data);
  UR_EXPECT_TRUE(row.kind == UsageBarReferralKind::Earned);
  UR_EXPECT_EQ(int64_t{8}, row.totalReferrals);
  UR_EXPECT_EQ(int64_t{35}, row.bonusGibPerDay);
}

UR_TEST(theOnboardingUsageBarGetsTheServersReferralTerms) {
  const std::string onboarding = ReadSource("Onboarding.cpp");
  const std::string bar = ReadSource("UsageBar.hpp");
  if (onboarding.empty() || bar.empty()) {
    UR_FAIL("could not read Onboarding.cpp / UsageBar.hpp to check the usage bar wiring");
    return;
  }
  const std::string refresh = FunctionBody(onboarding, "void OnboardingWindow::RefreshBalance()");
  UR_EXPECT_TRUE_MSG("onboarding's usage bar is not filled from UsageBarDataFrom(balance_), "
                     "so it misses the server's referral terms",
                     Has(refresh, "usage_->SetData(UsageBarDataFrom(balance_))"));
  // a bar with its own terms shows them whenever a caller forgets to set them
  UR_EXPECT_FALSE(Has(bar, "maxReferrals_"));
  UR_EXPECT_FALSE(Has(bar, "bonusGibPerDay_"));
  UR_EXPECT_FALSE(Has(bar, "SetReferralTerms"));
}
