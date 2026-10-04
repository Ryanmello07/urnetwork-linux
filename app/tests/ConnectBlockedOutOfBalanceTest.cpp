// Connect with no balance is blocked; a connection that runs out of balance
// stays up (urnetwork/android#483). The decision is balance_notice::BlockConnect
// over the out-of-balance latch; the entry points (tray menu, Connect page,
// location picks, connect on launch, post-sign-in connect) need GTK and the
// SDK, so their wiring is read as text.
//
// SPDX-License-Identifier: MPL-2.0
#include "Health.hpp"
#include "InsufficientBalanceNotice.hpp"
#include "TestHarness.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::balance_notice::BlockConnect;
using urnw::balance_notice::ClassifyConnect;
using urnw::balance_notice::ConnectAttempt;
using urnw::balance_notice::OutOfBalanceLatch;
using urnw::balance_notice::Signals;

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

// The body of the function whose definition starts with `signature`, up to the
// first closing brace in column 0.
std::string Body(const std::string& source, const char* signature) {
  const size_t at = source.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", at);
  return source.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

// The window's start-connect decision, as MainWindow::ConnectBlockedByBalance
// takes it: the live reading or the latch, the plan, the poll, and whether a
// session is up.
bool Blocked(const urnw::health::Signals& reading, const OutOfBalanceLatch& latch, bool pro,
             bool polling) {
  Signals s;
  s.insufficientBalance = reading.insufficientBalance || latch.OutOfBalance();
  s.pro = pro;
  s.polling = polling;
  s.connectRequested = reading.destinationSelected;
  return BlockConnect(ClassifyConnect(urnw::health::SessionUp(reading)), s);
}

OutOfBalanceLatch::Observation Observe(const urnw::health::Signals& reading, bool balanceKnown,
                                       long long availableBytes) {
  OutOfBalanceLatch::Observation o;
  o.insufficientBalance = reading.insufficientBalance;
  o.providersConnected =
      reading.sdk == urnw::health::SdkStatus::Connected && urnw::health::SessionUp(reading);
  o.balanceKnown = balanceKnown;
  o.availableBytes = availableBytes;
  return o;
}

urnw::health::Signals HeldSession() {
  urnw::health::Signals s;
  s.sdk = urnw::health::SdkStatus::Connecting;
  s.destinationSelected = true;
  s.tunnelBound = true;
  s.insufficientBalance = true;
  return s;
}

// What the SDK reports right after the user's Disconnect: the destination is
// gone and the contract status was cleared with it.
urnw::health::Signals AfterDisconnect() { return urnw::health::Signals{}; }

}  // namespace

UR_TEST(connectGateBlocksOnlyAStartWhileOutOfBalance) {
  // insufficient x pro x polling x session up: only a new connect of an
  // unfunded, non-Pro, settled account is blocked
  for (int bits = 0; bits < 16; ++bits) {
    Signals s;
    s.insufficientBalance = bits & 1;
    s.pro = bits & 2;
    s.polling = bits & 4;
    const bool sessionUp = bits & 8;
    s.connectRequested = sessionUp;
    const bool expected = s.insufficientBalance && !s.pro && !s.polling && !sessionUp;
    if (BlockConnect(ClassifyConnect(sessionUp), s) != expected) {
      UR_FAIL("start-connect decision wrong for case " + std::to_string(bits));
    }
  }
}

UR_TEST(connectGateNeverAppliesToAnAlreadyConnectedSession) {
  // running out of balance while connected keeps the connection: the gate has
  // no say over a session that is up, and the button stays Disconnect
  Signals s;
  s.insufficientBalance = true;
  s.connectRequested = true;
  UR_EXPECT_TRUE(ClassifyConnect(true) == ConnectAttempt::AlreadyConnected);
  UR_EXPECT_FALSE(BlockConnect(ConnectAttempt::AlreadyConnected, s));
  OutOfBalanceLatch latch;
  for (int i = 0; i < 5; ++i) latch.Observe(Observe(HeldSession(), true, 0));
  UR_EXPECT_FALSE(Blocked(HeldSession(), latch, false, false));
  UR_EXPECT_TRUE(urnw::health::Render(HeldSession()).action == urnw::health::Action::Disconnect);
}

UR_TEST(trayConnectAfterDisconnectWhileOutOfBalanceIsBlocked) {
  // the reported gap: held, the user disconnects, the tray now says Connect,
  // and the press used to start the tunnel again with no balance behind it
  OutOfBalanceLatch latch;
  latch.Observe(Observe(HeldSession(), true, 0));
  latch.Observe(Observe(AfterDisconnect(), true, 0));
  UR_EXPECT_TRUE(urnw::health::Render(AfterDisconnect()).action == urnw::health::Action::Connect);
  UR_EXPECT_TRUE(latch.OutOfBalance());
  UR_EXPECT_TRUE(Blocked(AfterDisconnect(), latch, false, false));
  // Pro and a purchase poll lift the block, as they do the held alert
  UR_EXPECT_FALSE(Blocked(AfterDisconnect(), latch, true, false));
  UR_EXPECT_FALSE(Blocked(AfterDisconnect(), latch, false, true));
}

UR_TEST(connectGateBlocksAStaleSessionStillReadingOutOfBalance) {
  // a destination still selected with the daemon's tunnel gone reads Connect;
  // pressing it starts a new session, so it is a start
  urnw::health::Signals stale = HeldSession();
  stale.tunnelBound = false;
  OutOfBalanceLatch latch;
  UR_EXPECT_TRUE(urnw::health::Render(stale).action == urnw::health::Action::Connect);
  UR_EXPECT_TRUE(Blocked(stale, latch, false, false));
}

UR_TEST(outOfBalanceLatchClearsOnlyOnEvidence) {
  OutOfBalanceLatch latch;
  // a funded idle account is never latched
  latch.Observe(Observe(AfterDisconnect(), true, 1000));
  UR_EXPECT_FALSE(latch.OutOfBalance());

  latch.Observe(Observe(HeldSession(), true, 500));
  // no evidence: a reset reading, the same or a lower balance, or no fetch yet
  latch.Observe(Observe(AfterDisconnect(), false, 0));
  latch.Observe(Observe(AfterDisconnect(), true, 500));
  latch.Observe(Observe(AfterDisconnect(), true, 200));
  UR_EXPECT_TRUE(latch.OutOfBalance());
  // a rise over the lowest balance seen is a refill
  latch.Observe(Observe(AfterDisconnect(), true, 300));
  UR_EXPECT_FALSE(latch.OutOfBalance());

  // providers attached on a live session mean the contracts went through
  latch.Observe(Observe(HeldSession(), true, 0));
  urnw::health::Signals funded = HeldSession();
  funded.insufficientBalance = false;
  funded.sdk = urnw::health::SdkStatus::Connected;
  latch.Observe(Observe(funded, true, 0));
  UR_EXPECT_FALSE(latch.OutOfBalance());

  // a balance learned only after the latch sets the floor, it does not clear
  latch.Observe(Observe(HeldSession(), false, 0));
  latch.Observe(Observe(AfterDisconnect(), true, 700));
  UR_EXPECT_TRUE(latch.OutOfBalance());
  latch.Reset();
  UR_EXPECT_FALSE(latch.OutOfBalance());
}

UR_TEST(connectEntryPointsAskTheStartConnectGate) {
  const std::string window = ReadSource("MainWindow.cpp");
  const std::string host = ReadSource("SdkHost.cpp");
  const std::string app = ReadSource("main.cpp");
  // the tray menu's Connect and the Connect page both land in ToggleConnect,
  // whose connect half starts through StartTunnelUi
  UR_EXPECT_TRUE(Has(app, "tray->on_toggle_connect = [&] { window->ToggleConnect(); };"));
  UR_EXPECT_TRUE(
      Has(window, "connectPage_->on_connect_action = [this](bool disconnect) { ToggleConnect(disconnect); };"));
  const std::string toggle = Body(window, "void MainWindow::ToggleConnect(bool disconnect) {");
  const size_t startAt = toggle.find("StartTunnelUi(/*connectDestination=*/false)");
  const size_t connectAt = toggle.find("host_.ConnectBestAvailable()");
  UR_EXPECT_TRUE(startAt != std::string::npos && connectAt != std::string::npos &&
                 startAt < connectAt);
  // StartTunnelUi asks the gate before it starts anything (connect on launch
  // and every post-sign-in connect pass through it too)
  const std::string start = Body(window, "TunnelStartResult MainWindow::StartTunnelUi(");
  const size_t gateAt = start.find("if (ConnectBlockedByBalance()) return");
  UR_EXPECT_TRUE(gateAt != std::string::npos);
  UR_EXPECT_TRUE(gateAt < start.find("host_.StartTunnel()"));
  // the location picks connect through the host, which asks the same gate
  // before it touches the device
  UR_EXPECT_TRUE(Has(window, "host_.SetConnectGate([this] { return ConnectBlockedByBalance(); });"));
  for (const char* signature :
       {"void SdkHost::ConnectBestAvailable() {", "void SdkHost::Connect(const std::optional"}) {
    const std::string body = Body(host, signature);
    const size_t at = body.find("if (connectGate_ && connectGate_()) return;");
    UR_EXPECT_TRUE_MSG(signature, at != std::string::npos);
    UR_EXPECT_TRUE_MSG(signature, at < body.find("std::scoped_lock lock(mutex_);"));
  }
}

UR_TEST(connectGateIsTheBalanceDecision) {
  const std::string window = ReadSource("MainWindow.cpp");
  const std::string gate = Body(window, "bool MainWindow::ConnectBlockedByBalance() {");
  UR_EXPECT_TRUE(Has(gate, "reading_.insufficientBalance || outOfBalance_.OutOfBalance()"));
  UR_EXPECT_TRUE(Has(gate, "signals.pro = balance_.IsPro();"));
  UR_EXPECT_TRUE(Has(gate, "signals.polling = balance_.IsPolling();"));
  UR_EXPECT_TRUE(Has(gate, "BlockConnect(balance_notice::ClassifyConnect(sessionUp), signals)"));
  // blocked: the upgrade path, never a disconnect
  UR_EXPECT_TRUE(Has(gate, "OpenUpgrade();"));
  UR_EXPECT_FALSE(Has(gate, "Disconnect"));
  // the latch is fed from every reading and balance change, and dies with the session
  UR_EXPECT_TRUE(Has(Body(window, "void MainWindow::UpdateBalanceNotice() {"),
                     "outOfBalance_.Observe(observation);"));
  UR_EXPECT_TRUE(
      Has(Body(window, "void MainWindow::ApplyAuthState(bool loggedIn) {"), "outOfBalance_.Reset();"));
}

UR_TEST(noConnectPathBypassesTheHost) {
  // the gate holds only if every connect goes through SdkHost::Connect,
  // SdkHost::ConnectBestAvailable or MainWindow::StartTunnelUi
  namespace fs = std::filesystem;
  const fs::path root(UR_SRC_DIR);
  int scanned = 0;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".cpp") continue;
    const std::string name = entry.path().filename().string();
    // the host itself, and the control client it starts the tunnel through
    if (name == "SdkHost.cpp" || name == "ControlClient.cpp") continue;
    ++scanned;
    const std::string source = ReadSource(name);
    UR_EXPECT_TRUE_MSG(name, !Has(source, "connectBestAvailable("));
    UR_EXPECT_TRUE_MSG(name, !Has(source, "->connect(") && !Has(source, ".connect(location"));
    UR_EXPECT_TRUE_MSG(name, !Has(source, "StartTunnelEx("));
    if (name != "MainWindow.cpp") UR_EXPECT_TRUE_MSG(name, !Has(source, "host_.StartTunnel("));
  }
  UR_EXPECT_TRUE(scanned > 10);
}
