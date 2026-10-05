// Removing the payout wallet makes another active Solana or Polygon wallet of
// the network the payout wallet when there is one (server
// fix/remove-wallet-promote). The Earnings page keeps a removal that landed
// for the round of reads it starts, and that round's commit says "Payouts now
// go to <short address>." (payouts_now_go_to, from the localizations store).
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadAppFile(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of the function whose definition starts with `signature`, braces
// balanced from the first '{' after it, or "".
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

bool Has(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// The msgstr of the entry with msgctxt `key`, or "" when the catalog has none.
std::string PoTranslation(const std::string& po, const std::string& key) {
  const size_t at = po.find("msgctxt \"" + key + "\"");
  if (at == std::string::npos) return std::string();
  const size_t str = po.find("msgstr \"", at);
  if (str == std::string::npos) return std::string();
  const size_t begin = str + 8;
  const size_t end = po.find("\"\n", begin);
  if (end == std::string::npos) return std::string();
  return po.substr(begin, end - begin);
}

}  // namespace

UR_TEST(PayoutPromotion_TheRoundAfterARemovalNamesThePromotedWallet) {
  const std::string page = ReadAppFile("src/EarningsPage.cpp");
  const std::string remove = FunctionBody(page, "void EarningsPage::RemoveSolanaWallet(");
  const size_t kept = remove.find("payoutRemoval_ = removal;");
  UR_EXPECT_TRUE(kept != std::string::npos);
  UR_EXPECT_TRUE(kept < remove.find("LoadLegacyWallets(/*reset=*/true);", kept));

  UR_EXPECT_TRUE(Has(FunctionBody(page, "void EarningsPage::ApplyLegacyWallets("),
                     "NotifyPromotedPayoutWallet();"));
  const std::string notify = FunctionBody(page, "void EarningsPage::NotifyPromotedPayoutWallet(");
  UR_EXPECT_TRUE(Has(notify, "solana::PromotedPayoutWallet(removal, legacyCommitted_)"));
  UR_EXPECT_TRUE(Has(notify, "T_(\"payouts_now_go_to\", \"Payouts now go to {}.\")"));
  UR_EXPECT_TRUE(Has(notify, "solana::ShortAddress(promoted->address)"));
}

UR_TEST(PayoutPromotion_TheLineIsInTheCatalogAndTranslated) {
  UR_EXPECT_TRUE(Has(ReadAppFile("po/urnetwork.pot"), "msgctxt \"payouts_now_go_to\""));
  UR_EXPECT_TRUE(PoTranslation(ReadAppFile("po/en.po"), "payouts_now_go_to") == "Payouts now go to {}.");
  for (const char* locale : {"ar", "de", "es", "fr", "ja", "ru", "uk", "zh_CN", "zh_HK"}) {
    const std::string value = PoTranslation(ReadAppFile(std::string("po/") + locale + ".po"), "payouts_now_go_to");
    UR_EXPECT_TRUE_MSG(locale, Has(value, "{}") && value != "Payouts now go to {}.");
  }
}
