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
  // unsupported_wallet: a return naming another wallet's page (a stale tab
  // from an earlier choice) is not this flow's either
  for (const char* code : {"purpose_mismatch", "not_bittensor_return", "not_awaiting_wallet",
                           "unsupported_wallet"}) {
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
  for (const char* code : {"challenge_expired", "message_mismatch", "wallet_error"}) {
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

// WalletConnect is the third wallet (Nova, Nightly and other WalletConnect v2
// substrate wallets), after Talisman and manual entry; the chooser's response
// id is the wallet id and nothing else chooses a wallet.
UR_TEST(BittensorWalletFlow_TheChooserOffersThreeWallets) {
  const std::string_view expected[] = {"talisman", "taocom", "walletconnect"};
  size_t n = 0;
  for (const auto& wallet : bt::kChooserWallets) {
    UR_EXPECT_TRUE_MSG(std::string(wallet.walletId), n < 3 && wallet.walletId == expected[n]);
    ++n;
  }
  UR_EXPECT_TRUE(n == 3);
  UR_EXPECT_TRUE(bt::kWalletWalletConnect == "walletconnect");
  for (const auto& id : expected) UR_EXPECT_TRUE_MSG(std::string(id), bt::ChosenWallet(id) == id);
  UR_EXPECT_TRUE(bt::ChosenWallet("cancel").empty());
  UR_EXPECT_TRUE(bt::ChosenWallet("").empty());
  UR_EXPECT_TRUE(bt::ChosenWallet("subwallet-js").empty());
  // only the WalletConnect page is given the project id
  UR_EXPECT_TRUE(bt::SendsWalletConnectProjectId("walletconnect"));
  UR_EXPECT_FALSE(bt::SendsWalletConnectProjectId("talisman"));
  UR_EXPECT_FALSE(bt::SendsWalletConnectProjectId("taocom"));
  // pairing, approving the session and the signature get the challenge's life
  UR_EXPECT_TRUE(bt::TimeoutMsFor(bt::kTransportBrowserBridge, "walletconnect") == 330'000u);
  UR_EXPECT_TRUE(bt::TimeoutMsFor(bt::kTransportBrowserBridge, "talisman") == 180'000u);
}

// The chooser hints and the browser hand-off texts are store keys the linux
// catalog carries, English as msgid.
UR_TEST(BittensorWalletFlow_WalletConnectStringsAreInTheCatalog) {
  const std::string pot = ReadFile("../po/urnetwork.pot");
  if (pot.empty()) {
    UR_FAIL("could not read po/urnetwork.pot");
    return;
  }
  auto has = [&](std::string_view key, std::string_view english) {
    return pot.find("msgctxt \"" + std::string(key) + "\"\nmsgid \"" + std::string(english) + "\"") !=
           std::string::npos;
  };
  int hints = 0;
  for (const auto& wallet : bt::kChooserWallets) {
    if (wallet.hintKey.empty()) continue;
    UR_EXPECT_TRUE_MSG(std::string(wallet.hintKey), has(wallet.hintKey, wallet.hintEnglish));
    ++hints;
  }
  UR_EXPECT_TRUE(hints == 2);
  const bt::ContinueText wc = bt::ContinueTextFor("walletconnect");
  UR_EXPECT_TRUE(wc.key == "bittensor_walletconnect_continue" && !wc.takesWalletName);
  UR_EXPECT_TRUE_MSG(std::string(wc.key), has(wc.key, wc.english));
  const bt::ContinueText talisman = bt::ContinueTextFor("talisman");
  UR_EXPECT_TRUE(talisman.key == "bittensor_continue_in_browser" && talisman.takesWalletName);
  UR_EXPECT_TRUE_MSG(std::string(talisman.key), has(talisman.key, talisman.english));
}

// Both choosers (sign-in, Earnings) come from the shared three-row chooser,
// and the session is given the build's project id for WalletConnect only.
UR_TEST(BittensorWalletFlow_ChoosersAndSessionCarryWalletConnect) {
  const std::string window = ReadFile("MainWindow.cpp");
  const std::string earnings = ReadFile("EarningsPage.cpp");
  const std::string host = ReadFile("SdkHost.cpp");
  const std::string sheet = ReadFile("BittensorManualSheet.cpp");
  if (window.empty() || earnings.empty() || host.empty() || sheet.empty()) {
    UR_FAIL("could not read the sources");
    return;
  }
  const std::string chooser = FunctionBody(sheet, "GtkWidget* NewBittensorWalletChooser(");
  UR_EXPECT_TRUE_MSG("the chooser lists every kChooserWallets row",
                     chooser.find("bittensor::kChooserWallets") != std::string::npos);
  for (const auto& [source, fn] :
       {std::pair<const std::string*, const char*>{&window, "void MainWindow::OnBittensor("},
        {&earnings, "void EarningsPage::ChooseBittensorWallet("}}) {
    const std::string body = FunctionBody(*source, fn);
    UR_EXPECT_TRUE_MSG(std::string(fn) + " uses the shared chooser",
                       body.find("NewBittensorWalletChooser(") != std::string::npos &&
                           body.find("bittensor::ChosenWallet(") != std::string::npos);
  }
  const std::string start = FunctionBody(host, "void SdkHost::StartBittensorSession(");
  const size_t guard = start.find("bittensor::SendsWalletConnectProjectId(walletId)");
  const size_t set = start.find("session->setWalletConnectProjectId(kWalletConnectProjectId)");
  UR_EXPECT_TRUE_MSG("the project id goes to the WalletConnect session only",
                     guard != std::string::npos && set != std::string::npos && guard < set);
}

// POST /sn/wallet refuses a pasted signature that is not from the entered
// address (signature_mismatch, kept by the sdk in SnError.code): after a
// manual entry the page says to sign again in that wallet, in the catalog's
// words; a bridge signature and every other refusal read as before.
UR_TEST(BittensorWalletFlow_ASignatureFromAnotherAccountNamesTheManualWallet) {
  const bt::ErrorText text = bt::ConnectErrorTextFor("signature_mismatch", bt::kTransportManual);
  UR_EXPECT_TRUE(text.key == "bittensor_error_signature_mismatch");
  UR_EXPECT_TRUE(text.english ==
                 "This signature isn't from the address you entered. In {}, sign the message "
                 "with that address, then paste the signature again.");
  UR_EXPECT_TRUE(bt::ConnectErrorTextFor("signature_mismatch", bt::kTransportBrowserBridge).key.empty());
  UR_EXPECT_TRUE(bt::ConnectErrorTextFor("signature_mismatch", "").key.empty());
  UR_EXPECT_TRUE(bt::ConnectErrorTextFor("server_error", bt::kTransportManual).key.empty());
  UR_EXPECT_TRUE(bt::ConnectErrorTextFor("", bt::kTransportManual).key.empty());

  // the key with its English as msgid (g_dpgettext2 matches both)
  const std::string pot = ReadFile("../po/urnetwork.pot");
  if (pot.empty()) {
    UR_FAIL("could not read po/urnetwork.pot");
    return;
  }
  const std::string entry = "msgctxt \"" + std::string(text.key) + "\"\nmsgid \"" +
                            std::string(text.english) + "\"";
  UR_EXPECT_TRUE_MSG(std::string(text.key), pot.find(entry) != std::string::npos);

  // a refused connect asks it with the wallet the connect signed with
  const std::string earnings = ReadFile("EarningsPage.cpp");
  const std::string apply = FunctionBody(earnings, "void EarningsPage::ApplyWalletConnectResult(");
  UR_EXPECT_TRUE(apply.find("bittensor::ConnectErrorTextFor(") != std::string::npos);
  const std::string start = FunctionBody(earnings, "void EarningsPage::StartWalletSignature(");
  UR_EXPECT_TRUE(start.find("connectWalletId_ = walletId;") != std::string::npos);
  // the Api fallback (no bound device) keeps the refusal's code, as the device does
  const std::string setWallet = FunctionBody(earnings, "void SetWallet(SdkHost& host,");
  UR_EXPECT_TRUE(setWallet.find("result->error->code.value_or(std::string())") != std::string::npos);
}

// Sign-in, network create and an added sign-in method refuse the same pasted
// signature with the same code (sign-in and create when they ask for
// result_errors). The host carries the code and the Bittensor wallet that
// signed, and each page words it with WalletProofRefusalText (the manual
// sheet's ConnectErrorTextFor and the wallet's name); any other refusal reads
// as sent.
UR_TEST(BittensorWalletFlow_SignInCreateAndAddNameTheManualWallet) {
  const std::string host = ReadFile("SdkHost.cpp");
  for (const char* signature : {"void SdkHost::AuthLoginWithWallet(",
                                "void SdkHost::FinishCreateNetworkWithWallet("}) {
    const std::string body = FunctionBody(host, signature);
    UR_EXPECT_TRUE_MSG(signature, body.find("args.result_errors = true;") != std::string::npos);
    UR_EXPECT_TRUE_MSG(signature, body.find("bittensorWalletId = bittensorWalletId_;") != std::string::npos);
  }
  const std::string login = FunctionBody(host, "void SdkHost::AuthLoginWithWallet(");
  UR_EXPECT_TRUE(login.find("r.errorCode = result->error->code.value_or(std::string());") !=
                 std::string::npos);
  const std::string create = FunctionBody(host, "void SdkHost::HandleNetworkCreateResult(");
  UR_EXPECT_TRUE(create.find("r.errorCode = result->error->code.value_or(std::string());") !=
                 std::string::npos);
  const std::string add = FunctionBody(host, "void SdkHost::AddAuthMethod(");
  UR_EXPECT_TRUE(add.find("result->error->code.value_or(std::string())") != std::string::npos);

  const std::string sheet = ReadFile("BittensorManualSheet.cpp");
  const std::string words = FunctionBody(sheet, "std::string WalletProofRefusalText(");
  UR_EXPECT_TRUE(words.find("bittensor::ConnectErrorTextFor(") != std::string::npos);
  UR_EXPECT_TRUE(words.find("urnet::bittensorWalletTransportFor(bittensorWalletId,") != std::string::npos);
  UR_EXPECT_TRUE(words.find("urnet::bittensorWalletDisplayName(bittensorWalletId)") != std::string::npos);

  const std::string window = ReadFile("MainWindow.cpp");
  UR_EXPECT_TRUE_MSG("sign-in", FunctionBody(window, "void MainWindow::OnWalletAuth(")
                                        .find("WalletProofRefusalText(result.errorCode, result.error, "
                                              "result.bittensorWalletId)") != std::string::npos);
  UR_EXPECT_TRUE_MSG("create", ReadFile("AuthViews.cpp")
                                   .find("WalletProofRefusalText(r.errorCode, r.error, r.bittensorWalletId)") !=
                                   std::string::npos);
  const std::string account = ReadFile("AccountPage.cpp");
  UR_EXPECT_TRUE_MSG("add", account.find("WalletProofRefusalText(result.code, result.error, bittensorWalletId)") !=
                                std::string::npos);
  UR_EXPECT_TRUE_MSG("add names its wallet",
                     account.find("Answer(addsignin::Method::Wallet, walletId)") != std::string::npos);
}
