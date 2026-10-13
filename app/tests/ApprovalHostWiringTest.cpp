// The browser approval's host half as wired (SdkHost.cpp): the code an approval
// opened is redeemed once and what comes back is held, not registered or
// stored, until the person has said yes to its network; the attempt rides the
// wallet flows' number, which every browser or wallet flow, a network change
// and a sign-out take, so an answer that comes back after any of them is
// dropped; the network the approval is for is a snapshot of the active space
// the UI thread reads without the host's lock; and every sign-in that starts
// says so first, which is how a password, a code or a sign-up ends an approval
// still waiting. SdkHost needs the SDK, so this reads its source with the
// comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::CountOf;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

namespace {

// Each needle occurs after the one before it.
bool HostInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

// The text with every run of whitespace made one space: a stretch of
// statements pinned this way has nothing between them but layout (and the
// comments ReadCode blanked).
std::string Squash(const std::string& text) {
  std::string out;
  bool space = false;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      space = true;
      continue;
    }
    if (space && !out.empty()) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  return out;
}

// The body's first statement, after its opening brace.
std::string FirstStatement(const std::string& body) {
  const std::string squashed = Squash(body);
  if (squashed.size() < 2 || squashed[0] != '{') return std::string();
  const size_t end = squashed.find(';');
  if (end == std::string::npos) return std::string();
  const size_t from = squashed[1] == ' ' ? 2 : 1;
  return squashed.substr(from, end + 1 - from);
}

const char* const kRedeem =
    "void SdkHost::RedeemApprovedCode(const std::string& code,\n"
    "                                 std::function<void(ApprovedNetwork)> done) {";

}  // namespace

// The redeem takes the code to the server once, with no credential, and holds
// what comes back: it registers nothing, stores nothing and announces nothing,
// refuses a credential with no network to show, and is held to the flow its
// approval began in before the code goes out and again before the credential
// is kept.
UR_TEST(ApprovalHostWiring_TheRedeemHoldsTheCredentialUntilConfirmed) {
  const std::string host = ReadCode("SdkHost.cpp");
  const std::string redeem = FunctionBody(host, kRedeem);
  UR_EXPECT_TRUE_MSG("RedeemApprovedCode is defined", !redeem.empty());
  for (const char* forbidden :
       {"RegisterNetworkClient(", "onAuth_", "asyncLocalState_", "localState_",
        "LoginWithCode(", "NoteSignInStarted(", "for (", "while ("}) {
    UR_EXPECT_TRUE_MSG(std::string("RedeemApprovedCode has ") + forbidden,
                       !Contains(redeem, forbidden));
  }
  UR_EXPECT_TRUE(HostInOrder(
      redeem,
      {"args.auth_code = code;",
       "const uint64_t flow = approvalFlow_.load(std::memory_order_acquire);",
       "if (err || !result) {", "ApprovedNetwork::Failure::Transport",
       "g_warning(\"sdkhost: redeeming the approved code did not complete\");",
       "if (result->error && !result->error->message.empty()) {",
       "ApprovedNetwork::Failure::Refused",
       "g_warning(\"sdkhost: the server refused the approved code\");",
       "sso::JwtClaimString(result->by_jwt, \"network_name\")",
       "if (!networkName || networkName->empty()) {",
       "g_warning(\"sdkhost: the approved code gave no network to confirm\");",
       "std::scoped_lock lock(mutex_);", "superseded = flow == 0 || !walletFlows_.IsCurrent(flow);",
       "if (!superseded) pendingApprovedJwt_ = result->by_jwt;", "if (superseded) {",
       "out.ok = true;", "out.networkName = *networkName;"}));
  // the dispatch: under mutex_, as a network change replaces api_ under it, and
  // only while the approval's flow is still the one that started last; the
  // handle's credential (a failed registration leaves one, and a launch restores
  // it) is taken off it first, so the code goes out with no Authorization
  UR_EXPECT_TRUE(Contains(Squash(redeem),
                          "std::scoped_lock lock(mutex_); if (api_) { superseded = flow == 0 || "
                          "!walletFlows_.IsCurrent(flow); if (!superseded) { api_->setByJwt(\"\"); "
                          "api_->authCodeLogin(args, answer); return; } } }"));
  // and the credential is only ever taken off, never handed to the api
  UR_EXPECT_EQ(std::size_t{1}, CountOf(redeem, "setByJwt("));
  UR_EXPECT_TRUE(Before(redeem, "std::scoped_lock lock(mutex_);", "api_->authCodeLogin("));
  // a code is spent by its first redeem, and nothing retries
  UR_EXPECT_EQ(std::size_t{1}, CountOf(redeem, "authCodeLogin("));
  // judged twice and no more, against the one number read before either
  UR_EXPECT_EQ(std::size_t{2}, CountOf(redeem, "walletFlows_.IsCurrent(flow)"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(redeem, "approvalFlow_"));
  // every outcome is answered, outside the lock
  UR_EXPECT_TRUE(CountOf(redeem, "if (done) done(out);") >= 5);
  UR_EXPECT_TRUE(Contains(redeem,
                          "g_warning(\"sdkhost: the approved code was not redeemed: "
                          "another flow took over since the \"\n"
                          "              \"approval began\");"));
  UR_EXPECT_TRUE(Contains(redeem,
                          "g_message(\"sdkhost: the approved code was redeemed; its "
                          "network is held for the person to \"\n"
                          "              \"confirm\");"));
  // the log lines are closed: no format, no argument
  for (const char* log : {"g_warning(", "g_message("}) {
    for (size_t at = redeem.find(log); at != std::string::npos; at = redeem.find(log, at + 1)) {
      const size_t end = redeem.find(");", at);
      const std::string call = redeem.substr(at, end - at);
      UR_EXPECT_TRUE_MSG(call, !Contains(call, "%") && !Contains(call, "\","));
    }
  }
}

// Confirm is the one place the held credential is registered, and a refused
// registration leaves a signed-out app with neither credential; discard drops
// it; and nothing but the functions that own the hold touch it.
UR_TEST(ApprovalHostWiring_OnlyTheConfirmRegistersTheHeldNetwork) {
  const std::string host = ReadCode("SdkHost.cpp");
  const std::string confirm = FunctionBody(
      host, "void SdkHost::ConfirmApprovedNetwork(std::function<void(AuthResult)> done) {");
  UR_EXPECT_TRUE(HostInOrder(
      confirm,
      {"std::scoped_lock lock(mutex_);", "jwt = std::move(*pendingApprovedJwt_);",
       "pendingApprovedJwt_.reset();", "if (jwt.empty()) {",
       "done({false, false, \"no approved sign-in is pending\"});",
       "RegisterNetworkClient(jwt, [this, done](AuthResult r) {", "if (!r.ok) {",
       "std::scoped_lock lock(mutex_);", "if (api_) api_->setByJwt(\"\");",
       "if (asyncLocalState_) asyncLocalState_->logout([](bool) {});", "if (done) done(r);"}));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(confirm, "RegisterNetworkClient("));
  // the attempt was consumed before the sheet asked: a confirm is no sign-in
  // starting, and nothing would be left to end
  UR_EXPECT_TRUE(!Contains(confirm, "NoteSignInStarted("));
  const std::string discard = FunctionBody(host, "void SdkHost::DiscardApprovedNetwork() {");
  UR_EXPECT_TRUE(
      HostInOrder(discard, {"std::scoped_lock lock(mutex_);", "pendingApprovedJwt_.reset();"}));

  size_t owned = 0;
  for (const char* owner :
       {kRedeem, "void SdkHost::ConfirmApprovedNetwork(", "void SdkHost::DiscardApprovedNetwork(",
        "bool SdkHost::ApplyNetworkServer(", "void SdkHost::Logout() {"}) {
    const std::string body = FunctionBody(host, owner);
    UR_EXPECT_TRUE_MSG(std::string(owner) + " is defined", !body.empty());
    owned += CountOf(body, "pendingApprovedJwt_");
  }
  UR_EXPECT_EQ(CountOf(host, "pendingApprovedJwt_"), owned);
  UR_EXPECT_EQ(std::size_t{1}, CountOf(host, "std::move(*pendingApprovedJwt_)"));
  // a network change and a sign-out drop it
  UR_EXPECT_TRUE(Contains(FunctionBody(host, "bool SdkHost::ApplyNetworkServer("),
                          "pendingApprovedJwt_.reset();"));
  UR_EXPECT_TRUE(
      Contains(FunctionBody(host, "void SdkHost::Logout() {"), "pendingApprovedJwt_.reset();"));
  // the header holds it under mutex_, an optional nobody else names
  const std::string header = ReadCode("SdkHost.hpp");
  UR_EXPECT_EQ(std::size_t{1}, CountOf(header, "pendingApprovedJwt_"));
  UR_EXPECT_TRUE(Contains(header, "std::optional<std::string> pendingApprovedJwt_;"));
}

// The attempt rides the wallet flows' number: starting it supersedes every
// flow still waiting and takes the next number, which the redeem is held to;
// a network change and a sign-out take the next one too.
UR_TEST(ApprovalHostWiring_TheAttemptRidesTheFlowNumber) {
  const std::string host = ReadCode("SdkHost.cpp");
  const std::string begin = FunctionBody(host, "uint64_t SdkHost::BeginBrowserApproval() {");
  UR_EXPECT_TRUE(HostInOrder(
      begin, {"const uint64_t flow = CancelPendingWalletFlows(\"superseded by a ur.io approval\");",
              "approvalFlow_.store(flow, std::memory_order_release);", "DiscardApprovedNetwork();",
              "return flow;"}));
  for (const char* forbidden : {"std::scoped_lock", "RegisterNetworkClient(", "LoginWith",
                                "SignInWith", "api_", "NoteSignInStarted("}) {
    UR_EXPECT_TRUE_MSG(std::string("BeginBrowserApproval has ") + forbidden,
                       !Contains(begin, forbidden));
  }
  // written by the start of an approval, read by its redeem, and by nothing else
  UR_EXPECT_EQ(std::size_t{2}, CountOf(host, "approvalFlow_"));

  // superseding answers: the connect and the add first (each takes mutex_),
  // the number under the lock, every waiting answer outside it
  const std::string cancel =
      FunctionBody(host, "uint64_t SdkHost::CancelPendingWalletFlows(const std::string& reason) {");
  UR_EXPECT_TRUE(HostInOrder(
      cancel, {"CancelPendingSolanaConnect(reason);", "CancelPendingAddSignIn(reason);",
               "std::scoped_lock lock(mutex_);", "flow = walletFlows_.Begin();",
               "authDone = std::exchange(walletAuthDone_, nullptr);",
               "createDone = std::exchange(walletCreateDone_, nullptr);",
               "signDone = std::exchange(walletSignDone_, nullptr);",
               "pendingWalletNetworkName_.clear();", "pendingWalletReferralCode_.clear();",
               "if (ssoOwner_ == addsignin::Owner::SignIn) {", "ssoState_.clear();",
               "ssoNonce_.clear();", "if (auto session = wallet_.bittensorSession()) {",
               "if (session->state() != \"signed\") session->cancel();", "if (signDone) {",
               "out.error = reason;", "if (createDone) createDone({false, false, reason});",
               "if (authDone) authDone({false, false, reason});", "return flow;"}));
  // its two callers, and their reasons start as bridge::IsSuperseded reads
  UR_EXPECT_EQ(std::size_t{3}, CountOf(host, "CancelPendingWalletFlows("));
  for (size_t at = host.find("CancelPendingWalletFlows(\""); at != std::string::npos;
       at = host.find("CancelPendingWalletFlows(\"", at + 1)) {
    UR_EXPECT_TRUE(host.compare(at, 40, "CancelPendingWalletFlows(\"superseded by ") == 0);
  }

  // a network change: answered before the lock, the hold and the snapshot
  // cleared, the snapshot read again only from a space in use
  const std::string server = FunctionBody(host, "bool SdkHost::ApplyNetworkServer(");
  UR_EXPECT_TRUE(Contains(Squash(server),
                          "{ if (hostName.empty()) return false; "
                          "CancelPendingWalletFlows(\"superseded by a network change\"); bool ok "
                          "= false;"));
  UR_EXPECT_TRUE(Before(server, "CancelPendingWalletFlows(\"superseded by a network change\");",
                        "std::scoped_lock lock(mutex_);"));
  const std::string squashed = Squash(server);
  for (const char* stretch :
       {"pendingInstantJwt_.reset(); pendingApprovedJwt_.reset(); "
        "SetApprovalNetwork(approval::Network()); bool spaceReplaced = false; try {",
        "networkSpace_ = spaceManager_->updateNetworkSpaceValues(key, values); spaceReplaced = "
        "true;",
        "ok = true; } catch (const std::exception& e) { std::fprintf(stderr, \"[sdk] switch "
        "network space to '%s' failed: %s\\n\", hostName.c_str(), e.what()); ok = false; } if (ok "
        "|| !spaceReplaced) UpdateApprovalNetworkLocked(); } if (!ok) return false;"}) {
    UR_EXPECT_TRUE_MSG(std::string("ApplyNetworkServer has, as written: ") + stretch,
                       Contains(squashed, stretch));
  }
  UR_EXPECT_EQ(std::size_t{3}, CountOf(server, "spaceReplaced"));
  // ...and the window hears of it through the auth state, after the lock
  UR_EXPECT_TRUE(
      Before(server, "UpdateApprovalNetworkLocked();", "if (onAuth_) onAuth_(loggedIn);"));

  // a sign-out: the hold dropped and an approval still out superseded
  const std::string logout = FunctionBody(host, "void SdkHost::Logout() {");
  UR_EXPECT_TRUE(
      HostInOrder(logout, {"std::scoped_lock lock(mutex_);", "pendingInstantJwt_.reset();",
                           "pendingApprovedJwt_.reset();", "walletFlows_.Begin();",
                           "asyncLocalState_->logout(", "if (onAuth_) onAuth_(false);"}));

  // read without the lock, from any thread
  const std::string header = ReadCode("SdkHost.hpp");
  for (const char* line : {
           "uint64_t CurrentBrowserFlow() const { return walletFlows_.Latest(); }",
           "approval::Network CurrentApprovalNetwork() const;",
           "uint64_t BeginBrowserApproval();",
           "void RedeemApprovedCode(const std::string& code, std::function<void(ApprovedNetwork)> "
           "done);",
           "void ConfirmApprovedNetwork(std::function<void(AuthResult)> done);",
           "void DiscardApprovedNetwork();",
           "void SetSignInStartedHandler(std::function<void()> h) { onSignInStarted_ = "
           "std::move(h); }",
           "std::atomic<uint64_t> approvalFlow_{0};",
           "mutable std::mutex approvalNetworkMutex_;",
           "approval::Network approvalNetwork_;",
           "void UpdateApprovalNetworkLocked();",
           "void SetApprovalNetwork(approval::Network network);",
           "uint64_t CancelPendingWalletFlows(const std::string& reason);",
           "void NoteSignInStarted();",
           "std::function<void()> onSignInStarted_;",
           "#include \"ApprovalNetwork.hpp\"",
       }) {
    UR_EXPECT_TRUE_MSG(std::string("SdkHost.hpp is missing ") + line, Contains(header, line));
  }
  // the flow number itself is atomic (WalletBridgeRoute.hpp)
  UR_EXPECT_TRUE(Contains(ReadCode("WalletBridgeRoute.hpp"), "std::atomic<uint64_t> latest_{0};"));
  UR_EXPECT_TRUE(
      HostInOrder(header, {" public:", "CurrentBrowserFlow()", " private:", "approvalFlow_{0}"}));
}

// The network an approval is for: a snapshot of the active space, written
// where the space changes and read under its own small lock.
UR_TEST(ApprovalHostWiring_TheNetworkIsASnapshotOfTheActiveSpace) {
  const std::string host = ReadCode("SdkHost.cpp");
  const std::string space = FunctionBody(host, "void SdkHost::UpdateApprovalNetworkLocked() {");
  UR_EXPECT_TRUE(HostInOrder(
      space,
      {"try {", "if (networkSpace_.has_value()) {", "networkSpace_->getHostName() == kUrHostName",
       "networkSpace_->getConfiguredApiUrl().empty()",
       "networkSpace_->getConfiguredPlatformUrl().empty()",
       "network = approval::NetworkOf(official, networkSpace_->getApiUrl(), kUrHostName);",
       "} catch (const std::exception&) {",
       "g_warning(\"sdkhost: the network space could not be read for the approval\");",
       "network = approval::Network();", "SetApprovalNetwork(std::move(network));"}));
  // once each: whether it is the operator's host, and the API url the client
  // uses; never the name that was typed, nor the environment's name
  UR_EXPECT_EQ(std::size_t{1}, CountOf(space, "getHostName()"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(space, "getApiUrl()"));
  for (const char* forbidden : {"hostName", "getEnvName", "kUrEnvName", "CurrentNetworkServer",
                                "LaunchOverride", "std::scoped_lock"}) {
    UR_EXPECT_TRUE_MSG(std::string("UpdateApprovalNetworkLocked reads ") + forbidden,
                       !Contains(space, forbidden));
  }
  // what the network server sheet shows as the live API url is that same getter
  UR_EXPECT_TRUE(
      Contains(FunctionBody(host, "SdkHost::NetworkServer SdkHost::CurrentNetworkServer() {"),
               "out.apiUrl = networkSpace_->getApiUrl();"));

  // read at the launch from the space it bound, before the api is taken from it
  const std::string init = FunctionBody(
      host, "bool SdkHost::Initialize(const std::string& storageDir, const std::string& logDir) {");
  UR_EXPECT_TRUE(
      HostInOrder(init, {"networkSpace_ = LaunchUrNetworkSpace(*spaceManager_, launchOverride);",
                         "UpdateApprovalNetworkLocked();", "api_ = networkSpace_->getApi();"}));
  // there, and after a switch, and nowhere else
  UR_EXPECT_EQ(std::size_t{2}, CountOf(host, "UpdateApprovalNetworkLocked();"));
  // the definition, the read and a switch's clear
  UR_EXPECT_EQ(std::size_t{3}, CountOf(host, "SetApprovalNetwork("));
  // the accessor reads it and SetApprovalNetwork writes it, each under its own lock
  UR_EXPECT_EQ(std::size_t{2}, CountOf(host, "approvalNetwork_"));
  const std::string reader =
      FunctionBody(host, "approval::Network SdkHost::CurrentApprovalNetwork() const {");
  UR_EXPECT_TRUE(HostInOrder(
      reader, {"std::scoped_lock lock(approvalNetworkMutex_);", "return approvalNetwork_;"}));
  for (const char* forbidden :
       {"(mutex_)", "api_", "networkSpace_", "getApiUrl", "CurrentNetworkServer"}) {
    UR_EXPECT_TRUE_MSG(std::string("CurrentApprovalNetwork reads ") + forbidden,
                       !Contains(reader, forbidden));
  }
  const std::string writer =
      FunctionBody(host, "void SdkHost::SetApprovalNetwork(approval::Network network) {");
  UR_EXPECT_TRUE(HostInOrder(writer, {"std::scoped_lock lock(approvalNetworkMutex_);",
                                      "approvalNetwork_ = std::move(network);"}));
}

// Every sign-in that starts says so first: the window ends a browser approval
// still waiting when it hears it (Linux's sign-in state is a bool, with no
// "signing in" to push). The confirm of an approval is not one of them.
UR_TEST(ApprovalHostWiring_EverySignInSaysItStartsFirst) {
  const std::string host = ReadCode("SdkHost.cpp");
  const char* const starts[] = {
      "void SdkHost::LoginWithPassword(",
      "void SdkHost::LoginWithCode(",
      "void SdkHost::LoginWithSeedphrase(",
      "void SdkHost::CreateInstantAccount(",
      "void SdkHost::ConfirmInstantAccount(",
      "void SdkHost::CreateNetwork(",
      "void SdkHost::CreateNetworkWithPendingWallet(",
      "void SdkHost::VerifyCode(",
      "void SdkHost::SignInWithSolana(",
      "void SdkHost::SignInWithBittensor(",
      "void SdkHost::SignInWithSso(",
      "void SdkHost::CreateNetworkWithPendingSso(",
  };
  for (const char* start : starts) {
    const std::string body = FunctionBody(host, start);
    UR_EXPECT_TRUE_MSG(std::string(start) + " is defined", !body.empty());
    UR_EXPECT_TRUE_MSG(std::string(start) + " says it starts first",
                       FirstStatement(body) == "NoteSignInStarted();");
  }
  // in those twelve and no other function
  UR_EXPECT_EQ(std::size_t{12}, CountOf(host, "NoteSignInStarted();"));
  UR_EXPECT_TRUE(Contains(FunctionBody(host, "void SdkHost::NoteSignInStarted() {"),
                          "if (onSignInStarted_) onSignInStarted_();"));
  UR_EXPECT_EQ(std::size_t{2}, CountOf(host, "onSignInStarted_"));
}

// A wallet or sso sign-in answered "superseded by ..." (a browser approval, a
// network change or another sign-in took over) is quiet on the sign-in page,
// as the Account page's add is: nothing failed.
UR_TEST(ApprovalHostWiring_ASupersededWalletSignInIsQuiet) {
  const std::string window = ReadCode("MainWindow.cpp");
  const std::string answer =
      FunctionBody(window, "void MainWindow::OnWalletAuth(const AuthResult& result) {");
  UR_EXPECT_TRUE(Contains(Squash(answer),
                          "SetLoginBusy(false); if (!result.ok && "
                          "bridge::IsSuperseded(result.error)) { loginError_.set_text(\"\"); "
                          "return; }"));
  UR_EXPECT_TRUE(Before(answer, "bridge::IsSuperseded(result.error)", "SetLoginError("));
  UR_EXPECT_TRUE(Before(answer, "bridge::IsSuperseded(result.error)", "StartTunnelUi("));
  UR_EXPECT_TRUE(Before(answer, "bridge::IsSuperseded(result.error)", "NavigateCreate("));
}
