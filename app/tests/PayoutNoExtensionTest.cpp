// The payout wallet's connect sheet when the ur.io bridge finds no extension of
// the chosen wallet in the browser (extension_not_found). The sheet also takes
// a typed address, which works with any wallet (a Brave Wallet user on android
// was told only to install a wallet, although entering the address worked), so
// its line points at the sheet's Enter address manually control, named in the
// control's own words in every language. Signing in and adding a sign-in
// method have no manual entry and keep the shared words.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "SolanaWalletPresentation.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace solana = urnw::solana;

namespace {

constexpr const char* kPayoutKey = "solana_wallet_error_extension_not_found";
constexpr const char* kPayoutEnglish =
    "The {} extension was not found in this browser. Install it and try again, or choose "
    "“Enter address manually” to paste your wallet address.";

std::string ReadPayoutFile(const std::string& relative) {
  const std::string candidates[] = {
      std::string(UR_SRC_DIR) + "/" + relative,
      "app/src/" + relative,
      "../app/src/" + relative,
      "linux/app/src/" + relative,
  };
  for (const auto& path : candidates) {
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    std::ostringstream out;
    out << in.rdbuf();
    if (!out.str().empty()) return out.str();
  }
  return std::string();
}

std::string PayoutFunctionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// The msgstr of the entry whose context is `key`, or "" when there is none.
std::string Translation(const std::string& po, const std::string& key) {
  const std::string context = "msgctxt \"" + key + "\"\nmsgid \"";
  const size_t at = po.find(context);
  if (at == std::string::npos) return std::string();
  const std::string marker = "\"\nmsgstr \"";
  const size_t msgstr = po.find(marker, at + context.size());
  if (msgstr == std::string::npos) return std::string();
  const size_t start = msgstr + marker.size();
  return po.substr(start, po.find("\"\n", start) - start);
}

}  // namespace

UR_TEST(PayoutNoExtension_PointsAtManualEntry) {
  const solana::BridgeErrorText text = solana::PayoutBridgeErrorTextFor("extension_not_found");
  UR_EXPECT_TRUE(text.key != nullptr && std::string(text.key) == kPayoutKey);
  UR_EXPECT_TRUE(text.english != nullptr && std::string(text.english) == kPayoutEnglish);
  UR_EXPECT_TRUE(text.takesWalletName);
  // the shared words, which signing in keeps, do not change
  const solana::BridgeErrorText shared = solana::BridgeErrorTextFor("extension_not_found");
  UR_EXPECT_TRUE(shared.key != nullptr &&
                 std::string(shared.key) == "bittensor_error_extension_not_found");
  // every other code reads as it does everywhere else
  for (const char* code : {"no_account", "session_not_found", "user_rejected", "invalid_request",
                           "wallet_error", "-1", ""}) {
    const solana::BridgeErrorText payout = solana::PayoutBridgeErrorTextFor(code);
    const solana::BridgeErrorText other = solana::BridgeErrorTextFor(code);
    UR_EXPECT_TRUE_MSG(code, (payout.key == nullptr) == (other.key == nullptr));
    UR_EXPECT_TRUE_MSG(code, payout.key == nullptr || std::strcmp(payout.key, other.key) == 0);
    UR_EXPECT_TRUE_MSG(code, payout.takesWalletName == other.takesWalletName);
  }
}

// The line is in the catalog with the English above as its msgid, and in every
// language it names Enter address manually as that language's catalog does.
UR_TEST(PayoutNoExtension_NamesManualEntryInEveryLanguage) {
  const std::string pot = ReadPayoutFile("../po/urnetwork.pot");
  const std::string linguas = ReadPayoutFile("../po/LINGUAS");
  if (pot.empty() || linguas.empty()) {
    UR_FAIL("could not read po/urnetwork.pot or po/LINGUAS");
    return;
  }
  const std::string entry =
      "msgctxt \"" + std::string(kPayoutKey) + "\"\nmsgid \"" + kPayoutEnglish + "\"\n";
  UR_EXPECT_TRUE_MSG("the pot has no payout line", pot.find(entry) != std::string::npos);
  std::vector<std::string> languages;
  std::istringstream lines(linguas);
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line[0] != '#') languages.push_back(line);
  }
  UR_EXPECT_TRUE(languages.size() >= 28);
  for (const auto& language : languages) {
    const std::string po = ReadPayoutFile("../po/" + language + ".po");
    const std::string line = Translation(po, kPayoutKey);
    const std::string label = Translation(po, "enter_address_manually");
    UR_EXPECT_TRUE_MSG("no payout line in " + language, !line.empty());
    UR_EXPECT_TRUE_MSG("no Enter address manually in " + language, !label.empty());
    if (line.empty() || label.empty()) continue;
    UR_EXPECT_TRUE_MSG("the payout line does not name \"" + label + "\" in " + language + ": " + line,
                       line.find(label) != std::string::npos);
    UR_EXPECT_TRUE_MSG("the payout line drops {} in " + language, line.find("{}") != std::string::npos);
    if (language != "en") {
      UR_EXPECT_TRUE_MSG("the payout line is English in " + language, line != kPayoutEnglish);
    }
  }
}

// Only the payout sheet's connect takes the payout words: the connect step's
// failure reads in them when its Connect offered manual entry, the sign step
// and the sign-in connects never do.
UR_TEST(PayoutNoExtension_OnlyThePayoutConnectOffersManualEntry) {
  const std::string host = ReadPayoutFile("SdkHost.cpp");
  const std::string wallet = ReadPayoutFile("WalletConnect.cpp");
  if (host.empty() || wallet.empty()) {
    UR_FAIL("could not read SdkHost.cpp or WalletConnect.cpp");
    return;
  }
  UR_EXPECT_TRUE(PayoutFunctionBody(host, "void SdkHost::ConnectSolanaWallet(")
                     .find("wallet_.Connect(provider, /*offersManualEntry=*/true);") !=
                 std::string::npos);
  for (const char* signIn : {"void SdkHost::SignInWithSolana(", "void SdkHost::AddSignInWithSolana("}) {
    const std::string body = PayoutFunctionBody(host, signIn);
    UR_EXPECT_TRUE_MSG(signIn, !body.empty());
    UR_EXPECT_TRUE_MSG(signIn, body.find("wallet_.Connect(provider);") != std::string::npos);
    UR_EXPECT_TRUE_MSG(signIn, body.find("offersManualEntry") == std::string::npos);
  }
  UR_EXPECT_TRUE(PayoutFunctionBody(wallet, "void WalletConnect::Connect(")
                     .find("connectOffersManualEntry_ = offersManualEntry;") != std::string::npos);
  UR_EXPECT_TRUE(PayoutFunctionBody(wallet, "std::string LocalizedBridgeError(")
                     .find("offersManualEntry ? solana::PayoutBridgeErrorTextFor(code)") !=
                 std::string::npos);
  UR_EXPECT_TRUE(
      PayoutFunctionBody(wallet, "void WalletConnect::HandleConnect(")
          .find("on_error(LocalizedBridgeError(p, params[\"errorCode\"], pageText, "
                "connectOffersManualEntry_));") != std::string::npos);
  UR_EXPECT_TRUE(PayoutFunctionBody(wallet, "void WalletConnect::HandleSignMessage(")
                     .find("on_error(LocalizedBridgeError(p, params[\"errorCode\"], pageText));") !=
                 std::string::npos);
}
