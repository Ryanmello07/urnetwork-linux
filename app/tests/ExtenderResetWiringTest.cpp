// The call sites behind Account > Extenders' Reset extenders (connect
// EXTENDER.md E7). A reset returns this device's extender state to a fresh
// install's, and on Linux that state lives in two processes, so:
//
//   * the Extenders section offers Reset extenders beside Share and Import,
//     needing no device, and the Account page confirms it in its own modal
//     (title and action Reset extenders, the body saying what goes, Cancel the
//     default) before the section runs it;
//   * SdkHost resets the GUI's own network space on a worker, by a handle of
//     its own and outside its lock, then hands the space's key and the reset's
//     id to urnetworkd (reset_extenders), never through the device rpc, and
//     the section reads the form again and says "Extenders reset";
//   * a reset the daemon refused because a bring-up owned its session is noted
//     on the main loop and sent again, once, on the same worker, when the
//     window's health poll reads a status that shows the bring-up settled;
//   * the daemon dispatches the verb behind set_provide_extender's owner gate,
//     looks the space up under opMutex_ (refusing, never waiting, while a
//     bring-up owns the session) and applies the reset outside it, to the one
//     space its tunnel session's device and provider-only device run in.
//
// The protocol's pure half is tested in ControlProtocolTest.cpp; these need
// glib, GTK and the SDK, so this reads their sources: a decision with no
// caller is this project's most-repeated defect.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "ControlClient.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadExtenderResetSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it; empty when the definition is missing.
std::string ExtenderResetBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// `first` occurs, and before `second` (which must occur too).
bool Precedes(const std::string& text, const std::string& first, const std::string& second) {
  const size_t a = text.find(first);
  const size_t b = text.find(second);
  return a != std::string::npos && b != std::string::npos && a < b;
}

// From `from` up to the first `to` after it; empty when either is missing,
// which the callers check.
std::string Between(const std::string& text, const std::string& from, const std::string& to) {
  const size_t start = text.find(from);
  if (start == std::string::npos) return std::string();
  const size_t end = text.find(to, start + from.size());
  if (end == std::string::npos) return std::string();
  return text.substr(start, end - start);
}

}  // namespace

// The daemon's case sits behind set_provide_extender's owner gate, parses the
// request strictly and answers with whether the space it holds was reset.
UR_TEST(ExtenderResetWiring_TheDaemonDispatchesBehindTheOwnerGate) {
  const std::string dispatch =
      ExtenderResetBody(ReadExtenderResetSource("daemon/ControlServer.cpp"),
                        "void ControlServer::DispatchAuthorized(");
  const std::string reset =
      Between(dispatch, "case ctl::Verb::ResetExtenders: {", "case ctl::Verb::SetKillSwitch:");
  UR_EXPECT_TRUE(!reset.empty());
  UR_EXPECT_TRUE(Precedes(reset,
                          "CheckTunnelOwner(conn, id, &denied, &crossUid, authorizedCrossUid)",
                          "request.get<ctl::ResetExtendersRequest>()"));
  UR_EXPECT_TRUE(Precedes(reset, "request.get<ctl::ResetExtendersRequest>()",
                          "tunnel_.ResetExtenders(req)"));
  UR_EXPECT_TRUE(Contains(reset, "result.code"));
  UR_EXPECT_TRUE(Precedes(reset, "payload.reset = result.reset;",
                          "ctl::MakeReply(id, true, nlohmann::json(payload))"));
}

// The daemon looks the space up under opMutex_, without waiting behind a
// bring-up and without building one, and applies the reset outside the lock.
UR_TEST(ExtenderResetWiring_TheDaemonResetsTheHeldSpaceOutsideTheLock) {
  const std::string reset = ExtenderResetBody(
      ReadExtenderResetSource("daemon/TunnelHost.cpp"),
      "TunnelHost::ExtenderResetResult TunnelHost::ResetExtenders(");
  UR_EXPECT_TRUE(!reset.empty());
  const std::string locked =
      Between(reset, "std::unique_lock<std::mutex> lock(opMutex_, std::try_to_lock);", "\n  }\n");
  UR_EXPECT_TRUE(!locked.empty());
  UR_EXPECT_TRUE(Precedes(locked, "if (!lock.owns_lock()) {", "spaceManager_->getNetworkSpace(key)"));
  UR_EXPECT_TRUE(Contains(Between(locked, "if (!lock.owns_lock()) {", "return result;"),
                          "result.code = ctl::kCodeStartInProgress;"));
  UR_EXPECT_TRUE(Contains(locked, "if (spaceManager_) {"));
  UR_EXPECT_TRUE(!Contains(locked, "applyExtenderReset"));
  UR_EXPECT_TRUE(Precedes(reset, "\n  }\n", "space->applyExtenderReset(request.extender_reset_id)"));
  UR_EXPECT_TRUE(Contains(reset, "result.reset = space->applyExtenderReset("));
  // a reset never builds a manager or a space: what is not held is left to the
  // next import, which carries the id
  for (const char* build : {"newNetworkSpaceManager", "importNetworkSpaceFromJson",
                            "LoadNetworkSpaceLocked", "BuildUrNetworkSpace"}) {
    UR_EXPECT_TRUE_MSG(build, !Contains(reset, build));
  }
  // the one log line names the key and the outcome
  UR_EXPECT_TRUE(Contains(reset, "request.host_name.c_str()"));
  UR_EXPECT_TRUE(Contains(reset, "request.env_name.c_str()"));
}

// The client applies the shared rule before sending, may send again on a dead
// socket (an applied reset changes nothing) and waits out a polkit dialog.
UR_TEST(ExtenderResetWiring_TheClientValidatesAndWaitsForTheDialog) {
  const std::string client = ExtenderResetBody(ReadExtenderResetSource("ControlClient.cpp"),
                                               "bool ControlClient::ResetExtenders(");
  UR_EXPECT_TRUE(Precedes(client, "ctl::ValidateResetExtendersRequest(request)",
                          "CallLocked(ctl::Verb::ResetExtenders, nlohmann::json(request), error,"));
  UR_EXPECT_TRUE(Contains(client, "/*allowRetry=*/true, PolkitAwareTimeoutLocked()"));
  UR_EXPECT_TRUE(Contains(client, "reply->get<ctl::ResetExtendersReply>().reset"));
  // a successful reply is a grant, so the authorization verdict reads Authorized
  UR_EXPECT_TRUE(urnw::VerbNeedsAuthorization(urnw::ctl::Verb::ResetExtenders));
}

// Reset extenders sits with Share and Import, needs no device, and is not red:
// red belongs to the confirmation.
UR_TEST(ExtenderResetWiring_TheSectionOffersResetBesideShareAndImport) {
  const std::string section = ReadExtenderResetSource("ExtenderSection.cpp");
  const std::string actions = ExtenderResetBody(section, "void ExtenderSection::BuildActions(");
  UR_EXPECT_TRUE(Precedes(actions, "actions->append(*share_);", "actions->append(*import_);"));
  UR_EXPECT_TRUE(Precedes(actions, "actions->append(*import_);", "actions->append(*reset_);"));
  UR_EXPECT_TRUE(
      Contains(actions, "Gtk::make_managed<Gtk::Button>(T_(\"reset_extenders\", \"Reset extenders\"))"));
  UR_EXPECT_TRUE(Precedes(actions, "reset_->signal_clicked()", "if (on_reset) on_reset();"));
  UR_EXPECT_TRUE(!Contains(actions, "destructive-action"));
  const std::string enabled = ExtenderResetBody(section, "void ExtenderSection::ApplyEnabled()");
  UR_EXPECT_TRUE(Contains(enabled, "reset_->set_sensitive(!resetting_);"));
  UR_EXPECT_TRUE(!Contains(enabled, "reset_->set_sensitive(enabled)"));
  UR_EXPECT_TRUE(Contains(ReadExtenderResetSource("ExtenderSection.hpp"),
                          "std::function<void()> on_reset;"));
}

// The page confirms in its own modal before the section runs the reset, with
// Cancel as the default.
UR_TEST(ExtenderResetWiring_ThePageConfirmsBeforeTheSectionResets) {
  const std::string page = ReadExtenderResetSource("AccountPage.cpp");
  const std::string group = ExtenderResetBody(page, "void AccountPage::BuildExtendersGroup(");
  UR_EXPECT_TRUE(Contains(group, "extenderSection_->on_reset = [this] { ConfirmResetExtenders(); };"));
  const std::string confirm = ExtenderResetBody(page, "void AccountPage::ConfirmResetExtenders()");
  UR_EXPECT_TRUE(!confirm.empty());
  UR_EXPECT_TRUE(Precedes(confirm, "if (!BeginSheet(\"reset extenders\")) return;",
                          "confirmDialog_ = std::make_unique<Gtk::Window>();"));
  UR_EXPECT_TRUE(Contains(confirm, "WireSheet(*confirmDialog_);"));
  UR_EXPECT_TRUE(Contains(confirm, "confirmDialog_->set_modal(true);"));
  UR_EXPECT_TRUE(Contains(confirm, "T_(\"reset_extenders_confirm\","));
  UR_EXPECT_TRUE(Contains(confirm, "T_(\"cancel\", \"Cancel\")"));
  const std::string action = Between(confirm, "auto* reset =", "actions->append(*reset);");
  UR_EXPECT_TRUE(Contains(action, "T_(\"reset_extenders\", \"Reset extenders\")"));
  UR_EXPECT_TRUE(Contains(action, "destructive-action"));
  UR_EXPECT_TRUE(Precedes(action, "confirmDialog_->set_visible(false);",
                          "extenderSection_->ResetExtenders();"));
  UR_EXPECT_TRUE(Contains(confirm, "confirmDialog_->set_default_widget(*cancel);"));
  // the press itself opens only the confirmation
  UR_EXPECT_TRUE(!Contains(ExtenderResetBody(ReadExtenderResetSource("ExtenderSection.cpp"),
                                             "void ExtenderSection::BuildActions("),
                           "ResetExtenders()"));
}

// The section runs the reset through the host, then reads the form again and
// says so; with no device's controller to read, the fields show what a reset
// leaves.
UR_TEST(ExtenderResetWiring_TheSectionRunsTheResetAndSaysSo) {
  const std::string section = ReadExtenderResetSource("ExtenderSection.cpp");
  const std::string run = ExtenderResetBody(section, "void ExtenderSection::ResetExtenders()");
  UR_EXPECT_TRUE(Precedes(run, "if (resetting_) return;", "host_.ResetExtenders("));
  UR_EXPECT_TRUE(Precedes(run, "if (!*alive) return;", "FinishReset(outcome);"));
  UR_EXPECT_TRUE(Precedes(run, "resetting_ = true;", "ApplyEnabled();"));
  const std::string finish = ExtenderResetBody(section, "void ExtenderSection::FinishReset(");
  UR_EXPECT_TRUE(Precedes(finish, "if (!outcome.reset) {", "Load();"));
  UR_EXPECT_TRUE(Precedes(finish, "Load();", "if (!haveSettings_) {"));
  const std::string noDevice = Between(finish, "if (!haveSettings_) {", "kit::ApplySupportingText");
  UR_EXPECT_TRUE(Contains(noDevice, "/*isDefault=*/true"));
  UR_EXPECT_TRUE(Contains(noDevice, "hosts_->get_buffer()->set_text(\"\");"));
  UR_EXPECT_TRUE(Precedes(finish, "T_(\"extenders_reset_done\", \"Extenders reset\")",
                          "Snack(T_(\"extenders_reset_done\", \"Extenders reset\"), false);"));
}

// The host resets its own space by a handle of its own on a worker, then tells
// the daemon by the verb, never by the device rpc, and the worker is joined.
UR_TEST(ExtenderResetWiring_TheHostResetsItsSpaceThenTellsTheDaemon) {
  const std::string source = ReadExtenderResetSource("SdkHost.cpp");
  const std::string reset = ExtenderResetBody(source, "bool SdkHost::ResetExtenders(");
  UR_EXPECT_TRUE(!reset.empty());
  const std::string locked = Between(reset, "std::scoped_lock lock(mutex_);", "\n  }\n");
  UR_EXPECT_TRUE(Contains(locked, "spaceManager_->getNetworkSpace(*key)"));
  UR_EXPECT_TRUE(!Contains(locked, "resetExtenders()"));
  const std::string worker = Between(reset, "extenderResetWorker_ = std::thread(", "return true;");
  UR_EXPECT_TRUE(Precedes(worker, "request.extender_reset_id = space.resetExtenders();",
                          "control_.ResetExtenders(request, &daemonReset, &error, &code)"));
  UR_EXPECT_TRUE(Precedes(worker, "extenderResetBusy_.store(false);", "PostToMain("));
  UR_EXPECT_TRUE(!Contains(worker, "mutex_"));
  // the verb covers the provider-only device and no session; the device rpc
  // would not, and a start_provider would rebuild a provider the verb resets
  for (const char* other : {"extenderVc_->resetExtenders()", "device_->resetExtenders()",
                            "ReconcileProvider"}) {
    UR_EXPECT_TRUE_MSG(other, !Contains(reset, other));
  }
  UR_EXPECT_TRUE(Precedes(reset, "extenderResetWorker_.joinable()",
                          "extenderResetWorker_ = std::thread("));
  const std::string destructor = ExtenderResetBody(source, "SdkHost::~SdkHost()");
  UR_EXPECT_TRUE(Contains(destructor, "if (extenderResetWorker_.joinable()) extenderResetWorker_.join();"));
}

// A reset the daemon refused because a bring-up owned its session is owed from
// the press's answer, noted on the main loop, and sent again by the health poll
// once its status shows the bring-up settled: on the same worker, once, and
// never owed again by the second answer.
UR_TEST(ExtenderResetWiring_ABusyRefusalIsSentAgainAfterTheBringUp) {
  const std::string source = ReadExtenderResetSource("SdkHost.cpp");
  const std::string reset = ExtenderResetBody(source, "bool SdkHost::ResetExtenders(");
  UR_EXPECT_TRUE(
      Contains(reset, "taken = control_.ResetExtenders(request, &daemonReset, &error, &code);"));
  const std::string marshal = Between(reset, "PostToMain(", "return true;");
  UR_EXPECT_TRUE(Precedes(marshal, "if (sent) owedExtenderReset_.NoteAnswer(request, taken, code);",
                          "done(outcome);"));

  const std::string poll =
      ExtenderResetBody(source, "void SdkHost::FollowDaemonExtenderReset() {");
  UR_EXPECT_TRUE(Precedes(poll, "if (!owedExtenderReset_.Owed()) return;", "control_.Status()"));
  UR_EXPECT_TRUE(Contains(poll, "FollowDaemonExtenderReset(*status);"));
  const std::string settle = ExtenderResetBody(
      source, "void SdkHost::FollowDaemonExtenderReset(const ctl::StatusReply& status) {");
  UR_EXPECT_TRUE(Precedes(settle, "extenderResetBusy_.compare_exchange_strong(expected, true)",
                          "owedExtenderReset_.TakeIfSettled(status)"));
  UR_EXPECT_TRUE(Precedes(settle, "extenderResetWorker_.joinable()",
                          "extenderResetWorker_ = std::thread("));
  const size_t worker = settle.find("extenderResetWorker_ = std::thread(");
  const std::string resend = worker == std::string::npos ? std::string() : settle.substr(worker);
  UR_EXPECT_TRUE(Contains(resend, "control_.ResetExtenders(request, &daemonReset, &error, &code)"));
  UR_EXPECT_TRUE(Contains(resend, "extenderResetBusy_.store(false);"));
  UR_EXPECT_TRUE(!Contains(resend, "owedExtenderReset_"));

  // both branches of the window's health poll follow it, with the status the
  // connected branch already read
  const std::string health = ExtenderResetBody(ReadExtenderResetSource("MainWindow.cpp"),
                                               "bool MainWindow::PollDaemonHealth()");
  UR_EXPECT_TRUE(
      Contains(Between(health, "if (!connected_) {", "return true;"), "host_.FollowDaemonExtenderReset();"));
  UR_EXPECT_TRUE(Precedes(health, "if (!status) return true;",
                          "host_.FollowDaemonExtenderReset(*status);"));
}

// The connect page's extender panel stays a read-only status: the reset lives
// in Account > Extenders only.
UR_TEST(ExtenderResetWiring_TheConnectPageHasNoReset) {
  for (const char* file : {"ConnectPage.cpp", "ExtenderPanel.cpp"}) {
    const std::string source = ReadExtenderResetSource(file);
    UR_EXPECT_TRUE_MSG(file, !source.empty());
    UR_EXPECT_TRUE_MSG(file, !Contains(source, "reset_extenders"));
    UR_EXPECT_TRUE_MSG(file, !Contains(source, "ResetExtenders"));
  }
}
