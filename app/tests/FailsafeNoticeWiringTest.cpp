// The call sites of the failsafe notices (FailsafeNotice.hpp): the health poll
// relays the countdown on every status it reads and clears it without a tunnel,
// and after a failsafe stop it shows the store's line for the kill switch the
// daemon reports, whichever branch reads the stop first; the connect page puts
// a daemon notice ahead of the warning; and the tray offers its recovery items
// as the poll decides them. The decisions are pure and run in
// FailsafeNoticeTest.cpp; the surfaces need GTK, so this reads their sources
// with the line comments blanked, so prose cannot satisfy a contract.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

// The source with every // comment blanked; string literals are kept.
std::string ReadNoticeSource(const std::string& relative) {
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

std::string NoticeBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Each needle occurs after the one before it.
bool InOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

}  // namespace

// Relayed from every status the poll's worker reads, ahead of the return that
// ends a healthy poll, and cleared while there is no tunnel to count down on.
UR_TEST(FailsafeNoticeWiring_ThePollRelaysTheCountdown) {
  const std::string window = ReadNoticeSource("MainWindow.cpp");
  const std::string poll = NoticeBody(window, "void MainWindow::ApplyDaemonHealth(");
  UR_EXPECT_TRUE(!poll.empty());
  UR_EXPECT_TRUE(InOrder(poll, {"if (!connected_) {", "SetFailsafeArmed(false);", "return;",
                                "if (!status) return;",
                                "failsafe_notice::ShowsArmedWarning(status->failsafe_armed, "
                                "status->tunnel_state)",
                                "status->tunnel_state != ctl::TunnelState::Error"}));
}

// After a failsafe stop the notice is the store's line for the kill switch the
// daemon reports, translated, in place of the daemon's English.
UR_TEST(FailsafeNoticeWiring_AFailsafeStopShowsTheStoresLine) {
  const std::string window = ReadNoticeSource("MainWindow.cpp");
  const std::string poll = NoticeBody(window, "void MainWindow::ApplyDaemonHealth(");
  UR_EXPECT_TRUE(InOrder(poll, {"Glib::ustring(status->error)",
                                "failsafe_notice::StoppedCopy(status->stop_reason, "
                                "status->kill_switch)",
                                "detail = T_(failsafe->key, failsafe->english);",
                                "connectPage_->SetDaemonNotice(detail);"}));
}

UR_TEST(FailsafeNoticeWiring_TheConnectPageWarnsBehindADaemonNotice) {
  const std::string page = ReadNoticeSource("ConnectPage.cpp");
  const std::string status = NoticeBody(page, "void ConnectPage::ApplyConnectStatus() {");
  UR_EXPECT_TRUE(InOrder(status, {"Glib::ustring notice = daemonNotice_;",
                                  "if (notice.empty() && failsafeArmed_)",
                                  "failsafe_notice::ArmedCopy()", "T_(armed.key, armed.english)",
                                  "kit::SetTextOrCollapse(*daemonNoticeText_, notice);"}));
  const std::string set = NoticeBody(page, "void ConnectPage::SetFailsafeArmed(bool armed) {");
  UR_EXPECT_TRUE(InOrder(set, {"failsafeArmed_ = armed;", "ApplyConnectStatus();"}));
}

// A session the window saw end owes its explanation to the disconnected poll,
// which reads it once a status comes back: the connect feed can report the
// disconnect first, and the daemon can be restarting.
UR_TEST(FailsafeNoticeWiring_TheDisconnectedPollExplainsAStopItOwes) {
  const std::string window = ReadNoticeSource("MainWindow.cpp");
  const std::string apply = NoticeBody(window, "void MainWindow::ApplyConnectReading(");
  UR_EXPECT_TRUE(InOrder(apply, {"const bool wasConnected = connected_;",
                                 "connected_ = view.action != health::Action::Connect;",
                                 "if (connected_ != wasConnected) stopExplanationOwed_ = "
                                 "wasConnected;"}));
  const std::string poll = NoticeBody(window, "void MainWindow::ApplyDaemonHealth(");
  const size_t idle = poll.find("if (!connected_) {");
  const size_t idleEnd = poll.find("return;", idle);
  UR_EXPECT_TRUE(idle != std::string::npos && idleEnd != std::string::npos);
  if (idle == std::string::npos || idleEnd == std::string::npos) return;
  UR_EXPECT_TRUE(InOrder(poll.substr(idle, idleEnd - idle),
                         {"if (status && stopExplanationOwed_) {",
                          "stopExplanationOwed_ = false;",
                          "failsafe_notice::StoppedCopy(status->stop_reason, "
                          "status->kill_switch)",
                          "connectPage_->SetDaemonNotice(T_(failsafe->key, failsafe->english));"}));
  // The connected branch, when it explains the stop itself, leaves nothing owed.
  UR_EXPECT_TRUE(InOrder(poll.substr(idleEnd), {"connectPage_->DisconnectPending()",
                                                "stopExplanationOwed_ = false;",
                                                "ApplyConnectReading(DaemonTunnelGoneReading());",
                                                "stopExplanationOwed_ = false;",
                                                "connectPage_->SetDaemonNotice(detail);"}));
}

// The poll decides the tray's items from every status it reads, none while the
// window holds the session, and the tray offers each only while it is decided:
// a click on an item the menu no longer offers does nothing.
UR_TEST(FailsafeNoticeWiring_TheTrayOffersWhatThePollDecides) {
  const std::string window = ReadNoticeSource("MainWindow.cpp");
  const std::string poll = NoticeBody(window, "void MainWindow::ApplyDaemonHealth(");
  UR_EXPECT_TRUE(InOrder(poll, {"if (!connected_) {",
                                "failsafe_notice::TrayRecoveryFor(/*windowConnected=*/false, "
                                "*status)",
                                "return;", "if (!status) return;",
                                "failsafe_notice::TrayRecoveryFor(/*windowConnected=*/true, "
                                "*status)"}));
  const std::string force = NoticeBody(window, "void MainWindow::ForceTunnelOff() {");
  UR_EXPECT_TRUE(InOrder(force, {"if (connected_ || !trayRecovery_.forceTunnelOff) return;",
                                 "host_.Control().StopTunnel(&error)", "++daemonStatusEpoch_;",
                                 "PollDaemonHealth();"}));
  const std::string lift = NoticeBody(window, "void MainWindow::LiftKillSwitch() {");
  UR_EXPECT_TRUE(InOrder(lift, {"if (connected_ || !trayRecovery_.liftKillSwitch) return;",
                                "host_.SetKillSwitch(false);"}));

  const std::string tray = ReadNoticeSource("Tray.cpp");
  const std::string menu = NoticeBody(tray, "static void MenuMethod(");
  UR_EXPECT_TRUE(InOrder(menu, {"if (self->offersForceTunnelOff()) {",
                                "failsafe_notice::TrayRecovery::ForceTunnelOffCopy()",
                                "T_(copy.key, copy.english)", "if (self->offersLiftKillSwitch()) {",
                                "failsafe_notice::TrayRecovery::LiftKillSwitchCopy()",
                                "T_(copy.key, copy.english)"}));
  UR_EXPECT_TRUE(InOrder(menu, {"id == kIdForceTunnelOff && self->offersForceTunnelOff()",
                                "self->on_force_tunnel_off();",
                                "id == kIdLiftKillSwitch && self->offersLiftKillSwitch()",
                                "self->on_lift_kill_switch();"}));
  const std::string set = NoticeBody(tray, "void Tray::SetRecovery(");
  UR_EXPECT_TRUE(InOrder(set, {"force_tunnel_off_ = forceTunnelOff;",
                               "lift_kill_switch_ = liftKillSwitch;", "EmitMenuLayoutUpdated();"}));

  const std::string app = ReadNoticeSource("main.cpp");
  UR_EXPECT_TRUE(InOrder(app, {"tray->on_force_tunnel_off = [&] { window->ForceTunnelOff(); };",
                               "tray->on_lift_kill_switch = [&] { window->LiftKillSwitch(); };",
                               "window->on_tray_recovery_change =",
                               "tray->SetRecovery(recovery.forceTunnelOff, "
                               "recovery.liftKillSwitch);"}));
}
