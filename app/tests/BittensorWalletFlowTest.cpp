// The Bittensor wallet-connect adoption (UPGRADE.md 4.6): the SDK session
// judges every wallet answer; the app maps each refusal onto the flow (drop,
// retry on the manual sheet, fail) and onto a store string, and the bridge
// return goes through the session rather than an ad-hoc query parse.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "BittensorWalletFlow.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace bt = urnw::bittensor;

namespace {

std::string ReadFile(const std::string& relative) {
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

std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

// A proof continues the flow; an answer for another purpose, a late return
// after cancel or a replay is not this flow's and must not end it.
UR_TEST(BittensorWalletFlow_ProofsContinueAndForeignAnswersAreDropped) {
  UR_EXPECT_TRUE(bt::Classify("", false) == bt::Outcome::Accept);
  UR_EXPECT_TRUE(bt::Classify("", true) == bt::Outcome::Accept);
  for (const char* code : {"purpose_mismatch", "not_bittensor_return", "not_awaiting_wallet"}) {
    UR_EXPECT_TRUE_MSG(code, bt::Classify(code, false) == bt::Outcome::Ignore);
  }
}

// A typo on the manual sheet is corrected in place; on the bridge the same
// refusal ends the attempt (the page cannot be asked again).
UR_TEST(BittensorWalletFlow_ManualTyposRetryBridgeRefusalsFail) {
  for (const char* code : {"invalid_signature", "address_mismatch", "invalid_ss58_address"}) {
    UR_EXPECT_TRUE_MSG(code, bt::Classify(code, true) == bt::Outcome::Retry);
    UR_EXPECT_TRUE_MSG(code, bt::Classify(code, false) == bt::Outcome::Fail);
  }
  for (const char* code : {"challenge_expired", "message_mismatch", "wallet_error",
                           "unsupported_wallet"}) {
    UR_EXPECT_TRUE_MSG(code, bt::Classify(code, true) == bt::Outcome::Fail);
    UR_EXPECT_TRUE_MSG(code, bt::Classify(code, false) == bt::Outcome::Fail);
  }
}

// Every refusal with a string names a key the linux catalog carries, with the
// English as its msgid (g_dpgettext2 looks up context AND msgid: a drifted
// English silently shows untranslated text).
UR_TEST(BittensorWalletFlow_RefusalStringsAreInTheCatalog) {
  const std::string pot = ReadFile("../po/urnetwork.pot");
  if (pot.empty()) {
    UR_FAIL("could not read po/urnetwork.pot");
    return;
  }
  int checked = 0;
  for (const char* code : {"invalid_signature", "challenge_expired", "message_mismatch",
                           "address_mismatch", "invalid_ss58_address"}) {
    const bt::ErrorText text = bt::ErrorTextFor(code);
    UR_EXPECT_TRUE_MSG(code, !text.key.empty());
    const std::string entry = "msgctxt \"" + std::string(text.key) + "\"\nmsgid \"" +
                              std::string(text.english) + "\"";
    UR_EXPECT_TRUE_MSG(std::string(code) + " -> " + std::string(text.key),
                       pot.find(entry) != std::string::npos);
    ++checked;
  }
  UR_EXPECT_TRUE(checked == 5);
  // the wallet's own words, or the generic failure
  UR_EXPECT_TRUE(bt::ErrorTextFor("wallet_error").key.empty());
  UR_EXPECT_TRUE(bt::ErrorTextFor("").key.empty());
}

// The bridge returns on the scheme the .desktop file already registers; no
// new handler is added.
UR_TEST(BittensorWalletFlow_TheRedirectIsTheRegisteredScheme) {
  UR_EXPECT_TRUE(bt::kRedirectLink == "urnetwork://bittensor-sign-message");
  const std::string desktop = ReadFile("../packaging/com.bringyour.network.desktop");
  if (desktop.empty()) {
    UR_FAIL("could not read packaging/com.bringyour.network.desktop");
    return;
  }
  UR_EXPECT_TRUE(desktop.find("x-scheme-handler/urnetwork;") != std::string::npos);
}

// The manual sheet lives as long as its challenge (5 minutes) plus the
// submit; the bridge keeps its 3 minute watchdog.
UR_TEST(BittensorWalletFlow_TimeoutsFollowTheTransport) {
  UR_EXPECT_TRUE(bt::TimeoutMsFor(bt::kTransportManual) == 330'000u);
  UR_EXPECT_TRUE(bt::TimeoutMsFor(bt::kTransportManual) > 300'000u);
  UR_EXPECT_TRUE(bt::TimeoutMsFor(bt::kTransportBrowserBridge) == 180'000u);
}

// The root cause this adoption removes: the app parsed the bridge return
// itself and accepted any address + signature, without checking that the
// message was the challenge it issued, that the purpose was its own, or that
// the signature was a signature. The return must go through the session.
UR_TEST(BittensorWalletFlow_TheBridgeReturnIsJudgedByTheSession) {
  const std::string wallet = ReadFile("WalletConnect.cpp");
  if (wallet.empty()) {
    UR_FAIL("could not read WalletConnect.cpp");
    return;
  }
  const std::string ret = FunctionBody(wallet, "void WalletConnect::HandleBittensorReturn(");
  UR_EXPECT_TRUE_MSG("the bittensor return is handled", !ret.empty());
  UR_EXPECT_TRUE_MSG("the session judges the return",
                     ret.find("->handleBridgeReturn(url,") != std::string::npos);
  UR_EXPECT_TRUE_MSG("the return is not parsed by hand",
                     ret.find("ParseQuery") == std::string::npos &&
                         wallet.find("params[\"signature\"]") == std::string::npos);
  const std::string open = FunctionBody(wallet, "void WalletConnect::SignWithBittensor(");
  UR_EXPECT_TRUE_MSG("the bridge url is the session's", open.find("->bridgeUrl()") != std::string::npos);
  UR_EXPECT_TRUE_MSG("no WalletConnect project id is sent for Bittensor",
                     wallet.find("wc_project_id") == std::string::npos);
  const std::string deliver = FunctionBody(wallet, "void WalletConnect::DeliverBittensorResult(");
  const size_t classify = deliver.find("bittensor::Classify(");
  const size_t signature = deliver.find("on_signature(result.Proof->Signature)");
  UR_EXPECT_TRUE_MSG("only an accepted proof reaches on_signature",
                     classify != std::string::npos && signature != std::string::npos &&
                         classify < signature);
}

// Every Bittensor flow starts a session for its own purpose on linux, and the
// manual sheet's cancel only ends the flow it belongs to.
UR_TEST(BittensorWalletFlow_TheHostRunsEveryFlowOnASession) {
  const std::string host = ReadFile("SdkHost.cpp");
  if (host.empty()) {
    UR_FAIL("could not read SdkHost.cpp");
    return;
  }
  const std::string start = FunctionBody(host, "void SdkHost::StartBittensorSession(");
  UR_EXPECT_TRUE_MSG("the session is made for linux on the registered redirect",
                     start.find("urnet::newBittensorWalletSession(") != std::string::npos &&
                         start.find("bittensor::kPlatform") != std::string::npos &&
                         start.find("bittensor::kRedirectLink") != std::string::npos);
  UR_EXPECT_TRUE_MSG("the challenge comes from the session's args",
                     start.find("session->challengeArgs(expectedAddress)") != std::string::npos &&
                         start.find("session->setChallenge(") != std::string::npos);
  for (const auto& [fn, purpose] :
       {std::pair<const char*, const char*>{"void SdkHost::SignInWithBittensor(", "kPurposeLogin"},
        {"void SdkHost::SignBittensorConnect(", "kPurposeConnect"},
        {"void SdkHost::CreateNetworkWithPendingWallet(", "kPurposeCreate"}}) {
    const std::string body = FunctionBody(host, fn);
    UR_EXPECT_TRUE_MSG(std::string(fn) + " starts a session for " + purpose,
                       body.find("StartBittensorSession(") != std::string::npos &&
                           body.find(purpose) != std::string::npos);
  }
  const std::string cancel = FunctionBody(host, "void SdkHost::CancelBittensorManual(");
  UR_EXPECT_TRUE_MSG("cancel leaves a newer flow alone",
                     cancel.find("walletFlows_.IsCurrent(flow)") != std::string::npos);
}
