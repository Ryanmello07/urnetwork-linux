// The redeem sheet's answer for a balance-code redeem (BalanceCodeRedeem.hpp)
// and its wiring to the SDK classifier (UPGRADE.md D2). The server refuses an
// unknown code and one this network already redeemed with the SAME payload,
// and a lost response may have committed, so the sheet must classify through
// urnet::classifyBalanceCodeRedeem with the network's redeemed-code list
// before it says "invalid" -- a retry after a lost-but-credited response must
// read "already redeemed". An empty answer is unknown, never success. The
// sheet needs GTK and the SDK, so its wiring is read as text.
//
// SPDX-License-Identifier: MPL-2.0
#include "BalanceCodeRedeem.hpp"
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::BalanceCodeRedeemNeedsCodeList;
using urnw::BalanceCodeRedeemNotice;
using urnw::BalanceCodeRedeemNoticeFor;

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

size_t Count(const std::string& haystack, const std::string& needle) {
  size_t n = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + needle.size())) {
    ++n;
  }
  return n;
}

UR_TEST(balanceCodeOutcomesMapToTheirNotices) {
  UR_EXPECT_TRUE(BalanceCodeRedeemNoticeFor("redeemed") == BalanceCodeRedeemNotice::Redeemed);
  UR_EXPECT_TRUE(BalanceCodeRedeemNoticeFor("already_redeemed") ==
                 BalanceCodeRedeemNotice::AlreadyRedeemed);
  UR_EXPECT_TRUE(BalanceCodeRedeemNoticeFor("invalid") == BalanceCodeRedeemNotice::Invalid);
  UR_EXPECT_TRUE(BalanceCodeRedeemNoticeFor("unknown") == BalanceCodeRedeemNotice::Unknown);
  // an outcome this build does not know is never success or "invalid"
  UR_EXPECT_TRUE(BalanceCodeRedeemNoticeFor("") == BalanceCodeRedeemNotice::Unknown);
  UR_EXPECT_TRUE(BalanceCodeRedeemNoticeFor("something_new") == BalanceCodeRedeemNotice::Unknown);
}

UR_TEST(balanceCodeOnlyACreditedRedeemSkipsTheCodeList) {
  UR_EXPECT_FALSE(BalanceCodeRedeemNeedsCodeList("redeemed"));
  // a refusal and a lost response both consult the redeemed-code list
  UR_EXPECT_TRUE(BalanceCodeRedeemNeedsCodeList("invalid"));
  UR_EXPECT_TRUE(BalanceCodeRedeemNeedsCodeList("unknown"));
}

UR_TEST(redeemSheetClassifiesWithTheSdkAndTheRedeemedCodeList) {
  const std::string sheet = ReadSource("RedeemCodeSheet.cpp");
  // first without the list, then again with it
  UR_EXPECT_EQ(2, Count(sheet, "urnet::classifyBalanceCodeRedeem("));
  UR_EXPECT_TRUE(Count(sheet, "BalanceCodeRedeemNeedsCodeList(") > 0);
  UR_EXPECT_TRUE(Count(sheet, "getNetworkRedeemedBalanceCodes(") >= 2);  // history + classify
  UR_EXPECT_TRUE(Count(sheet, "\"balance_code_already_redeemed_message\"") > 0);
  // the SDK's format gate, not a hardcoded 26
  UR_EXPECT_TRUE(Count(sheet, "urnet::isBalanceCodeFormatValid(") >= 2);
  UR_EXPECT_EQ(0, Count(sheet, "kBalanceCodeLength"));
  // the sheet never decides success itself from a result without a transfer balance
  UR_EXPECT_EQ(0, Count(sheet, "if (result->error) {"));
}

}  // namespace
