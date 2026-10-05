// The call sites of "send feedback with logs" while disconnected (support inbox
// 2090). The lifecycle is pure and tested in LogUploadTest.cpp, the wire in
// ControlProtocolTest.cpp; what makes the daemon upload its own logs with no
// tunnel, behind the log's gate, on a device that touches nothing else — and
// what makes the GUI ask it first and fall back to its old path — lives in the
// daemon and the GUI, which need glib, GTK and the SDK. So this reads their
// sources: a decision with no caller is this project's most-repeated defect.
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

std::string ReadUploadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string UploadFunctionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// `first` occurs, and before `second` (which must occur too).
bool Ahead(const std::string& text, const std::string& first, const std::string& second) {
  const size_t a = text.find(first);
  const size_t b = text.find(second);
  return a != std::string::npos && b != std::string::npos && a < b;
}

}  // namespace

// The daemon serves the verb behind the log's gate: read-log was checked in
// Dispatch (ActionIdForVerb), and the log must be this caller's, as for
// log_tail. Sending logs owns nothing, so it claims nothing and passes no
// tunnel-owner gate a disconnected user could fail.
UR_TEST(LogUploadWiring_TheDaemonServesItBehindTheLogsGate) {
  const std::string server = ReadUploadSource("daemon/ControlServer.cpp");
  const std::string dispatch =
      UploadFunctionBody(server, "void ControlServer::DispatchAuthorized(");
  UR_EXPECT_TRUE(Ahead(dispatch, "case ctl::Verb::UploadLogs:",
                       "reply(HandleUploadLogs(conn, id, request));"));
  const std::string handle =
      UploadFunctionBody(server, "nlohmann::json ControlServer::HandleUploadLogs(");
  UR_EXPECT_TRUE(!handle.empty());
  UR_EXPECT_TRUE(Ahead(handle, "conn->peer.uid != 0 && LogBelongsToOtherUid(conn)",
                       "tunnel_.UploadLogs(req)"));
  UR_EXPECT_TRUE(Ahead(handle, "LogBelongsToOtherUid(conn)", "ctl::kCodeAuthNotTunnelOwner"));
  UR_EXPECT_TRUE(Contains(handle, "request.get<ctl::UploadLogsRequest>()"));
  UR_EXPECT_TRUE(Contains(handle, "payload.carrier = result.carrier;"));
  UR_EXPECT_FALSE(Contains(handle, "ClaimTunnelOwnership("));
  UR_EXPECT_FALSE(Contains(handle, "CheckTunnelOwner("));
}

// UploadLogs validates before anything, and a bring-up that owns the session
// queues the request instead of failing it; only a free session starts it.
UR_TEST(LogUploadWiring_TheDaemonValidatesThenQueuesBehindABringUp) {
  const std::string host = ReadUploadSource("daemon/TunnelHost.cpp");
  const std::string upload =
      UploadFunctionBody(host, "TunnelHost::LogUploadResult TunnelHost::UploadLogs(");
  UR_EXPECT_TRUE(!upload.empty());
  UR_EXPECT_TRUE(Ahead(upload, "ctl::ValidateUploadLogsRequest(request)", "lock.try_lock()"));
  UR_EXPECT_TRUE(Ahead(upload, "busy_.load() || !lock.try_lock()", "queuedUpload_ = request;"));
  UR_EXPECT_TRUE(Contains(upload, "logupload::Carrier::Queued"));
  UR_EXPECT_TRUE(
      Ahead(upload, "queuedUpload_ = request;", "return StartLogUploadLocked(request);"));
  UR_EXPECT_FALSE(Contains(upload, "NewDeviceLocked("));
}

// The device that runs carries it; with none, a standalone device is built as
// the provider-only device is — and nothing after that. It provides to nobody,
// it has its own slot, and the line naming it is written before the sdk zips.
UR_TEST(LogUploadWiring_TheUploadRunsOnTheLiveDeviceOrAStandaloneOneThatTouchesNothing) {
  const std::string host = ReadUploadSource("daemon/TunnelHost.cpp");
  const std::string start = UploadFunctionBody(
      host, "TunnelHost::LogUploadResult TunnelHost::StartLogUploadLocked(");
  UR_EXPECT_TRUE(!start.empty());
  UR_EXPECT_TRUE(
      Contains(start, "logupload::CarrierFor(device_.has_value(), providerDevice_.has_value())"));
  UR_EXPECT_TRUE(Contains(start, "device = &*device_;"));
  UR_EXPECT_TRUE(Contains(start, "device = &*providerDevice_;"));
  UR_EXPECT_TRUE(Ahead(start, "RetireUploadDeviceLocked();", "uploadDevice_ = NewDeviceLocked("));
  UR_EXPECT_TRUE(Ahead(start, "LoadNetworkSpaceLocked(request.network_space_json);",
                       "uploadDevice_ = NewDeviceLocked("));
  UR_EXPECT_TRUE(Contains(start,
                          "uploadDevice_ = NewDeviceLocked(request.by_jwt, request.instance_id, "
                          "request.app_version);"));
  UR_EXPECT_TRUE(Ahead(start, "uploadDevice_ = NewDeviceLocked(",
                       "uploadDevice_->setProvideControlMode(\"never\");"));
  // call forms, so a comment that names them does not count
  for (const char* forbidden :
       {"Tunnel::Open(", "egressMarker_.", "ApplyFilterLocked(", "InstallFilterLocked(",
        "setRpcServer(", "newIoLoop(", "setTunnelStarted(", " device_ = ", "providerDevice_ = ",
        "ClaimTunnelOwnership(", "OpenProviderViewControllersLocked("}) {
    UR_EXPECT_TRUE_MSG(forbidden, !Contains(start, forbidden));
  }
  // the sdk's own upload, with the server's feedback id, after the carrier line
  UR_EXPECT_TRUE(Ahead(start, "urnet::logAppInfo(\"log-upload\"",
                       "device->uploadLogs(request.feedback_id,"));
  UR_EXPECT_TRUE(Contains(start, "reported->store(true);"));
  UR_EXPECT_TRUE(Contains(start, "uploadReported_ = reported;"));
  UR_EXPECT_TRUE(Contains(start, "ctl::kCodeLogUploadFailed"));
}

// The standalone device never shares a moment with another device under the
// same identity: every teardown (and so the head of every bring-up) and every
// provider-only start retire it first, the provider's only after its refusals.
UR_TEST(LogUploadWiring_EveryOtherDeviceRetiresTheStandaloneOneFirst) {
  const std::string host = ReadUploadSource("daemon/TunnelHost.cpp");
  const std::string stop =
      UploadFunctionBody(host, "void TunnelHost::StopInternalLocked(const std::string& reason)");
  UR_EXPECT_TRUE(Ahead(stop, "RetireUploadDeviceLocked();", "device_.reset();"));
  const std::string provider = UploadFunctionBody(
      host, "TunnelHost::ProviderStartResult TunnelHost::StartProvider(");
  UR_EXPECT_TRUE(Ahead(provider, "ctl::kCodeKillSwitchArmed", "RetireUploadDeviceLocked();"));
  UR_EXPECT_TRUE(
      Ahead(provider, "RetireUploadDeviceLocked();", "providerDevice_ = NewDeviceLocked("));
  const std::string retire =
      UploadFunctionBody(host, "void TunnelHost::RetireUploadDeviceLocked()");
  UR_EXPECT_TRUE(Ahead(retire, "uploadDevice_->close();", "uploadDevice_.reset();"));
  UR_EXPECT_TRUE(Contains(retire, "uploadDevice_->waitForClose("));
}

// The reaper retires it once its upload reported (or at the bound) and starts
// a queued request once no bring-up owns the session.
UR_TEST(LogUploadWiring_TheReaperRetiresAndStartsQueuedUploads) {
  const std::string host = ReadUploadSource("daemon/TunnelHost.cpp");
  const std::string reap = UploadFunctionBody(host, "void TunnelHost::Reap()");
  UR_EXPECT_TRUE(Ahead(reap, "if (busy_.load()) return;", "MaintainLogUploadLocked();"));
  UR_EXPECT_TRUE(Ahead(reap, "if (!lock.owns_lock()) return;", "MaintainLogUploadLocked();"));
  const std::string maintain =
      UploadFunctionBody(host, "void TunnelHost::MaintainLogUploadLocked()");
  UR_EXPECT_TRUE(Contains(maintain, "logupload::RetireStandaloneDevice("));
  UR_EXPECT_TRUE(Contains(maintain, "uploadReported_ && uploadReported_->load()"));
  UR_EXPECT_TRUE(
      Ahead(maintain, "logupload::RetireStandaloneDevice(", "RetireUploadDeviceLocked();"));
  UR_EXPECT_TRUE(
      Ahead(maintain, "logupload::QueuedUploadExpired(", "StartLogUploadLocked(request);"));
}

// The GUI asks the daemon first, with the credentials start_provider carries,
// without holding its own lock across the call, and falls back to the
// DeviceRemote only when the daemon did not take it.
UR_TEST(LogUploadWiring_TheGuiAsksTheDaemonFirstAndFallsBack) {
  const std::string page = ReadUploadSource("SupportPage.cpp");
  const std::string upload =
      UploadFunctionBody(page, "void SupportPage::UploadLogs(const std::string& feedbackId)");
  UR_EXPECT_TRUE(!upload.empty());
  UR_EXPECT_TRUE(Ahead(upload, "feedbackId.empty()", "host_.UploadDaemonLogs(feedbackId)"));
  UR_EXPECT_TRUE(Contains(
      upload, "logupload::GuiStepAfterDaemon(daemonAccepted, host_.hasDevice())"));
  UR_EXPECT_TRUE(Ahead(upload, "host_.UploadDaemonLogs(feedbackId)", "host_.device().uploadLogs("));
  UR_EXPECT_TRUE(Ahead(upload, "logupload::GuiStep::Done", "host_.device().uploadLogs("));
  // still only after the server accepted the feedback, with the box ticked
  const std::string send = UploadFunctionBody(page, "void SupportPage::OnSendFeedback()");
  UR_EXPECT_TRUE(Contains(send, "if (attachLogs && !feedbackId.empty()) UploadLogs(feedbackId);"));

  const std::string sdk = ReadUploadSource("SdkHost.cpp");
  const std::string daemon =
      UploadFunctionBody(sdk, "bool SdkHost::UploadDaemonLogs(const std::string& feedbackId)");
  UR_EXPECT_TRUE(!daemon.empty());
  for (const char* field :
       {"request.feedback_id = feedbackId;", "request.by_jwt = localState_->getByClientJwt();",
        "request.instance_id = localState_->getInstanceId();",
        "request.app_version = kAppVersion;",
        "request.network_space_json = networkSpace_->toJson();"}) {
    UR_EXPECT_TRUE_MSG(field, Contains(daemon, field));
  }
  // the lock is scoped to reading the session, and the call comes after it
  UR_EXPECT_TRUE(Contains(daemon, "{\n    std::scoped_lock lock(mutex_);"));
  UR_EXPECT_TRUE(Ahead(daemon, "request.network_space_json = networkSpace_->toJson();\n",
                       "control_.UploadLogs(request, &carrier, &error, &code)"));
  const size_t lockAt = daemon.find("std::scoped_lock lock(mutex_);");
  const size_t scopeEnd = daemon.find("\n  }\n", lockAt == std::string::npos ? 0 : lockAt);
  const size_t callAt = daemon.find("control_.UploadLogs(");
  UR_EXPECT_TRUE(lockAt != std::string::npos && scopeEnd != std::string::npos &&
                 callAt != std::string::npos && scopeEnd < callAt);
  // and nothing takes it again before the call returns
  UR_EXPECT_TRUE(scopeEnd != std::string::npos &&
                 daemon.find("mutex_", scopeEnd) == std::string::npos);
}

// The client validates before a frame is sent and does not re-send a frame the
// daemon received: a second one would start a second upload.
UR_TEST(LogUploadWiring_TheClientValidatesAndDoesNotResend) {
  const std::string client = ReadUploadSource("ControlClient.cpp");
  const std::string upload = UploadFunctionBody(client, "bool ControlClient::UploadLogs(");
  UR_EXPECT_TRUE(!upload.empty());
  UR_EXPECT_TRUE(Ahead(upload, "ctl::ValidateUploadLogsRequest(request)",
                       "CallLocked(ctl::Verb::UploadLogs, nlohmann::json(request), error,"));
  UR_EXPECT_TRUE(Contains(upload, "/*allowRetry=*/false"));
  UR_EXPECT_TRUE(Contains(upload, "reply->get<ctl::UploadLogsReply>().carrier"));
}
