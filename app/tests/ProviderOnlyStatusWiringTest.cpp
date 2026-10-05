// The call sites behind the provider-only device's statistics and settings
// (support inbox 1521, P008, the follow-up to providing while disconnected).
// While disconnected the daemon's provider-only device provides, and it has no
// DeviceRemote, so:
//
//   * the daemon runs the SDK's contract and provider status view controllers
//     on it, opened with the device and closed before it, polls the provider
//     status only while a GUI keeps asking, and answers provider_stats like
//     status (never gated, empty for a foreign uid);
//   * the GUI reads provider_stats while the Earnings destination shows, and
//     its provider statistics accessors and ProviderStatusNow fall back to that
//     snapshot only while no DeviceRemote is bound; the local idle reason is
//     derived while either device runs;
//   * the device's extender role travels with them, read where the connected
//     path reads it (the device's status, the contract controller's extender
//     series), so the Earnings extender row and plot show while disconnected,
//     while the connect page's row, whose switch needs a device, stays hidden;
//   * a saved network space value (DoH servers, VLESS, the private extender,
//     the server) sends start_provider again, and the daemon replaces a device
//     whose request changed.
//
// The pure halves are tested in ControlProtocolTest.cpp, ProvideLifecycleTest.cpp
// and ProviderIdleReasonTest.cpp; these need glib, GTK and the SDK, so this
// reads their sources: a decision with no caller is this project's
// most-repeated defect.
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

std::string ReadProviderOnlySource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it.
std::string ProviderOnlyBody(const std::string& source, const std::string& signature) {
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

}  // namespace

// The controllers come up with the device and go before it: they read it.
UR_TEST(ProviderOnlyStatus_TheDaemonOpensTheControllersWithTheDevice) {
  const std::string host = ReadProviderOnlySource("daemon/TunnelHost.cpp");
  const std::string start =
      ProviderOnlyBody(host, "TunnelHost::ProviderStartResult TunnelHost::StartProvider(");
  UR_EXPECT_TRUE(Precedes(start, "providerDevice_ = NewDeviceLocked(",
                          "OpenProviderViewControllersLocked();"));
  const std::string open =
      ProviderOnlyBody(host, "void TunnelHost::OpenProviderViewControllersLocked()");
  UR_EXPECT_TRUE(Contains(open, "providerContractVc_ = providerDevice_->openContractViewController();"));
  UR_EXPECT_TRUE(Contains(
      open, "providerStatusVc_ = providerDevice_->openProviderStatusViewController();"));
  // opening polls nothing: the lease does
  UR_EXPECT_TRUE(!Contains(open, "->start()"));

  const std::string retire = ProviderOnlyBody(host, "void TunnelHost::RetireProviderDeviceLocked()");
  UR_EXPECT_TRUE(Precedes(retire, "CloseProviderViewControllersLocked();", "providerDevice_->close();"));
  const std::string close =
      ProviderOnlyBody(host, "void TunnelHost::CloseProviderViewControllersLocked()");
  UR_EXPECT_TRUE(Contains(close, "providerStatusLease_.Release();"));
  UR_EXPECT_TRUE(
      Contains(close, "providerDevice_->closeProviderStatusViewController(*providerStatusVc_);"));
  UR_EXPECT_TRUE(Contains(close, "providerDevice_->closeContractViewController(*providerContractVc_);"));
  // the generic close cannot release a C++ handle
  UR_EXPECT_TRUE(!Contains(host, "providerStatusVc_->close()"));
  UR_EXPECT_TRUE(!Contains(host, "providerContractVc_->close()"));
}

// A changed request replaces the running device through the shared rule.
UR_TEST(ProviderOnlyStatus_TheDaemonReplacesADeviceWhoseRequestChanged) {
  const std::string start = ProviderOnlyBody(ReadProviderOnlySource("daemon/TunnelHost.cpp"),
                                             "TunnelHost::ProviderStartResult TunnelHost::StartProvider(");
  UR_EXPECT_TRUE(Contains(start, "ctl::SameProviderDevice(providerConfig_, request)"));
  UR_EXPECT_TRUE(Precedes(start, "ctl::SameProviderDevice(providerConfig_, request)",
                          "RetireProviderDeviceLocked();"));
}

// provider_stats reads the controllers without blocking behind a bring-up, and
// the status controller polls only while the lease is held.
UR_TEST(ProviderOnlyStatus_TheDaemonReadsTheControllersAndLeasesThePolling) {
  const std::string host = ReadProviderOnlySource("daemon/TunnelHost.cpp");
  const std::string stats = ProviderOnlyBody(host, "ctl::ProviderStatsReply TunnelHost::ProviderStats(");
  UR_EXPECT_TRUE(!stats.empty());
  UR_EXPECT_TRUE(Contains(stats, "std::try_to_lock"));
  UR_EXPECT_TRUE(Precedes(stats, "!providerDevice_", "reply.running = true;"));
  for (const char* read :
       {"providerDevice_->getProviderPacketStats()", "providerContractVc_->getProviderThroughputPoints()",
        "providerContractVc_->getProviderTransportDistribution()",
        "providerStatusVc_->getIsLoaded()", "providerStatusVc_->getLastFetchError()",
        "providerStatusVc_->getProviderStatus()"}) {
    UR_EXPECT_TRUE_MSG(read, Contains(stats, read));
  }
  UR_EXPECT_TRUE(Precedes(stats, "pollStatus && providerStatusLease_.Renew(",
                          "providerStatusVc_->start();"));
  const std::string reap = ProviderOnlyBody(host, "void TunnelHost::Reap()");
  UR_EXPECT_TRUE(Precedes(reap, "providerStatusLease_.Expire(", "providerStatusVc_->stop();"));
}

// Answered like status, before the polkit gate, and empty for a caller whose
// status is redacted, which must not renew the polling either.
UR_TEST(ProviderOnlyStatus_TheControlServerAnswersItLikeStatus) {
  const std::string server = ReadProviderOnlySource("daemon/ControlServer.cpp");
  const std::string dispatch = ProviderOnlyBody(server, "void ControlServer::Dispatch(");
  UR_EXPECT_TRUE(Precedes(dispatch, "verb == ctl::Verb::ProviderStats", "ctl::ActionIdForVerb("));
  UR_EXPECT_TRUE(Precedes(dispatch, "if (!StatusMustBeRedactedFor(conn))", "tunnel_.ProviderStats("));
  UR_EXPECT_TRUE(Contains(dispatch, "request.get<ctl::ProviderStatsRequest>().poll_status"));
  const std::string client = ReadProviderOnlySource("ControlClient.cpp");
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(client, "ControlClient::ProviderStats("),
                          "CallLocked(ctl::Verb::ProviderStats, nlohmann::json(request), error)"));
}

// The GUI asks while the Earnings destination shows, with the shared step,
// stops asking an older daemon, and raises the events the DeviceRemote's
// controllers raise.
UR_TEST(ProviderOnlyStatus_TheGuiPollsWhileTheEarningsDestinationShows) {
  const std::string host = ReadProviderOnlySource("SdkHost.cpp");
  const std::string polling = ProviderOnlyBody(host, "void SdkHost::SetProviderStatusPolling(");
  UR_EXPECT_TRUE(Precedes(polling, "providerStatsPollId_ = g_timeout_add(",
                          "self->PollDaemonProviderStatsLocked();"));
  // at once, not a second later
  UR_EXPECT_TRUE(Precedes(polling, "providerStatsPollId_ = g_timeout_add(",
                          "\n    PollDaemonProviderStatsLocked();"));
  UR_EXPECT_TRUE(Contains(polling, "g_source_remove(providerStatsPollId_);"));

  const std::string poll = ProviderOnlyBody(host, "void SdkHost::PollDaemonProviderStatsLocked()");
  UR_EXPECT_TRUE(Contains(
      poll, "provide::DaemonProviderStatsStep(device_.has_value(), daemonProviderRunning_.load(),"));
  UR_EXPECT_TRUE(Precedes(poll, "DropDaemonProviderStatsLocked();", "control_.ProviderStats("));
  UR_EXPECT_TRUE(Precedes(poll, "request.poll_status = true;", "control_.ProviderStats(request, &error)"));
  UR_EXPECT_TRUE(Precedes(poll, "error == ctl::kErrorUnknownVerb",
                          "providerStatsUnsupportedGeneration_ = control_.SessionGeneration();"));
  UR_EXPECT_TRUE(Precedes(poll, "if (!reply->running) {", "daemonProviderStats_ = std::move(stats);"));
  UR_EXPECT_TRUE(Precedes(poll, "daemonProviderStats_ = std::move(stats);",
                          "EmitDrawerEvent(DrawerEvent::Throughput);"));
  UR_EXPECT_TRUE(Contains(poll, "if (statusChanged) EmitDrawerEvent(DrawerEvent::ProviderStatus);"));
  // the timeout holds `this`
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(host, "SdkHost::~SdkHost()"),
                          "g_source_remove(providerStatsPollId_);"));
  // and a sign-out or a quit forgets the snapshot
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(host, "void SdkHost::Logout()"),
                          "DropDaemonProviderStatsLocked();"));
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(host, "void SdkHost::Shutdown()"),
                          "DropDaemonProviderStatsLocked();"));
}

// Every accessor the Earnings page's provider statistics read falls back to the
// daemon's snapshot, and only while no DeviceRemote is bound.
UR_TEST(ProviderOnlyStatus_TheProviderAccessorsReadTheDaemonWithoutADevice) {
  const std::string host = ReadProviderOnlySource("SdkHost.cpp");
  struct Accessor {
    const char* signature;
    const char* fallback;
  };
  const Accessor kAccessors[] = {
      {"std::optional<urnet::ThroughputPointList> SdkHost::ProviderThroughputPoints()",
       "if (!device_ && daemonProviderStats_) return daemonProviderStats_->providerPoints;"},
      {"std::optional<urnet::TransportDistribution> SdkHost::ProviderTransportDistribution()",
       "if (!device_ && daemonProviderStats_) return daemonProviderStats_->providerDistribution;"},
      {"bool SdkHost::HasProviderStats()",
       "return !device_ && daemonProviderStats_ && daemonProviderStats_->hasProviderStats;"},
      {"bool SdkHost::DeviceHasProviderStats()",
       "if (!device_) return daemonProviderStats_ && daemonProviderStats_->hasProviderStats;"},
      {"SdkHost::ProviderStatusSnapshot SdkHost::ProviderStatusNow()",
       "if (!device_ && daemonProviderStats_) return daemonProviderStats_->status;"},
  };
  for (const Accessor& accessor : kAccessors) {
    const std::string body = ProviderOnlyBody(host, accessor.signature);
    UR_EXPECT_TRUE_MSG(accessor.signature, Contains(body, accessor.fallback));
  }
  // the session's own controllers still come first
  const std::string now = ProviderOnlyBody(host, kAccessors[4].signature);
  UR_EXPECT_TRUE(Precedes(now, "if (!providerStatusVc_) {", "daemonProviderStats_->status"));
  const std::string points = ProviderOnlyBody(host, kAccessors[0].signature);
  UR_EXPECT_TRUE(Precedes(points, "if (contractVc_) return contractVc_->getProviderThroughputPoints();",
                          "daemonProviderStats_"));
}

// Disconnected, the provider-only device counts as a provider, so Auto and
// Network show their lines; "no traffic yet" needs a window that was read.
UR_TEST(ProviderOnlyStatus_TheIdleLineCountsTheProviderOnlyDevice) {
  const std::string host = ReadProviderOnlySource("SdkHost.cpp");
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(host, "bool SdkHost::ProviderRuns()"),
                          "return hasDevice() || daemonProviderRunning_.load();"));
  const std::string page = ReadProviderOnlySource("EarningsPage.cpp");
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(page, "void EarningsPage::ApplyStatusLine()"),
                          "SessionIdleReasonFor(host_.ProviderRuns(),"));
  const std::string pull =
      ProviderOnlyBody(page, "void EarningsPage::PullProviderThroughput(bool forced)");
  UR_EXPECT_TRUE(Contains(
      pull, "distribution ? std::optional<int64_t>(distribution->ByteCount) : std::nullopt"));
}

// A saved network space value reaches a running provider-only device: each
// writer reconciles with settingsChanged after a real save, posted.
UR_TEST(ProviderOnlyStatus_ASavedSpaceValueReachesTheProvider) {
  const std::string host = ReadProviderOnlySource("SdkHost.cpp");
  const std::string doh = ProviderOnlyBody(host, "std::optional<std::string> SdkHost::SetControlDohUrls(");
  UR_EXPECT_TRUE(Precedes(doh, "networkSpace_->setControlDohUrls(",
                          "if (answer && answer->empty()) ReconcileProviderAfterSpaceChange("));
  const std::string vless = ProviderOnlyBody(host, "std::optional<std::string> SdkHost::SetVlessSettings(");
  UR_EXPECT_TRUE(Precedes(vless, "networkSpace_->setVlessSettings(settings)",
                          "if (answer && answer->empty()) ReconcileProviderAfterSpaceChange("));
  const std::string extender = ProviderOnlyBody(host, "bool SdkHost::SetPrivateExtender(");
  UR_EXPECT_TRUE(Precedes(extender, "spaceManager_->updateNetworkSpaceValues(stored->key, values);",
                          "ReconcileProviderAfterSpaceChange("));
  // saving what is stored writes nothing, and reconciles nothing
  UR_EXPECT_TRUE(Precedes(extender, "return true;\n    }", "ReconcileProviderAfterSpaceChange("));
  const std::string server = ProviderOnlyBody(host, "bool SdkHost::ApplyNetworkServer(");
  UR_EXPECT_TRUE(Precedes(server, "control_.StopTunnel();", "ReconcileProviderAfterSpaceChange("));

  const std::string after =
      ProviderOnlyBody(host, "void SdkHost::ReconcileProviderAfterSpaceChange(const char* reason)");
  UR_EXPECT_TRUE(Precedes(after, "PostToMain(",
                          "ReconcileProvider(reason, /*userInitiated=*/true, /*settingsChanged=*/true);"));
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(host, "void SdkHost::ReconcileProvider(const char* reason,"),
                          "ReconcileProviderLocked(reason, userInitiated, settingsChanged);"));
  // and settingsChanged is what makes the step start a running provider again
  const std::string reconcile = ProviderOnlyBody(host, "void SdkHost::ReconcileProviderLocked(");
  UR_EXPECT_TRUE(Contains(reconcile, "ctl::ProviderFactsFrom(*status), settingsChanged)"));
}

// The provider-only device's extender role: the daemon reports what the
// connected path reads, the device's own extender status and the contract view
// controller's extender series, each read guarded on its own so a failure
// costs only its own field.
UR_TEST(ProviderOnlyStatus_TheDaemonReportsTheExtenderRole) {
  const std::string stats = ProviderOnlyBody(ReadProviderOnlySource("daemon/TunnelHost.cpp"),
                                             "ctl::ProviderStatsReply TunnelHost::ProviderStats(");
  UR_EXPECT_TRUE(Precedes(stats, "reply.running = true;",
                          "providerDevice_->getExtenderProvideStatus()"));
  UR_EXPECT_TRUE(
      Contains(stats, "reply.extender_provide_status_json = nlohmann::json(*status).dump();"));
  UR_EXPECT_TRUE(Precedes(stats, "if (providerContractVc_) {",
                          "providerContractVc_->getExtenderThroughputPoints()"));
  UR_EXPECT_TRUE(
      Contains(stats, "reply.extender_throughput_points_json = nlohmann::json(*points).dump();"));
  UR_EXPECT_TRUE(Contains(stats, "noteReadFailure(\"extender status\", e);"));
  UR_EXPECT_TRUE(Contains(stats, "noteReadFailure(\"extender series\", e);"));
  // the extender series' failure is its own: it never clears the provider series
  UR_EXPECT_TRUE(Precedes(stats, "noteReadFailure(\"series\", e);",
                          "providerContractVc_->getExtenderThroughputPoints()"));
}

// The GUI keeps both in its snapshot, and raises the event the DeviceRemote's
// listener raises when the role changes, and when the snapshot goes.
UR_TEST(ProviderOnlyStatus_TheGuiKeepsTheExtenderRole) {
  const std::string host = ReadProviderOnlySource("SdkHost.cpp");
  const std::string poll = ProviderOnlyBody(host, "void SdkHost::PollDaemonProviderStatsLocked()");
  UR_EXPECT_TRUE(
      Contains(poll, "stats.extenderProvideStatus = ProviderStatsPart<urnet::ExtenderProvideStatus>("));
  UR_EXPECT_TRUE(Contains(poll, "reply->extender_provide_status_json, \"extender status\")"));
  UR_EXPECT_TRUE(Contains(poll, "stats.extenderPoints = ProviderStatsPart<urnet::ThroughputPointList>("));
  UR_EXPECT_TRUE(Contains(poll, "reply->extender_throughput_points_json, \"extender series\")"));
  UR_EXPECT_TRUE(
      Contains(poll, "daemonProviderStats_->extenderProvideStatusJson != stats.extenderProvideStatusJson"));
  UR_EXPECT_TRUE(Precedes(poll, "daemonProviderStats_ = std::move(stats);",
                          "if (extenderChanged) EmitDrawerEvent(DrawerEvent::ExtenderProvideStatus);"));
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(host, "void SdkHost::DropDaemonProviderStatsLocked()"),
                          "EmitDrawerEvent(DrawerEvent::ExtenderProvideStatus);"));
}

// The Earnings extender row, the running state behind its extender statistics
// and its extender plot read the snapshot while no DeviceRemote is bound, and
// the session device first. The connect page's row keeps the device alone: its
// switch writes the setting through the device, so it hides without one (N1).
UR_TEST(ProviderOnlyStatus_TheEarningsExtenderReadsTheDaemonWithoutADevice) {
  const std::string host = ReadProviderOnlySource("SdkHost.cpp");
  const std::string points =
      ProviderOnlyBody(host, "std::optional<urnet::ThroughputPointList> SdkHost::ExtenderThroughputPoints()");
  UR_EXPECT_TRUE(Precedes(points, "if (contractVc_) return contractVc_->getExtenderThroughputPoints();",
                          "if (!device_ && daemonProviderStats_) return daemonProviderStats_->extenderPoints;"));
  const std::string role = ProviderOnlyBody(
      host, "std::optional<urnet::ExtenderProvideStatus> SdkHost::ProviderExtenderProvideStatus()");
  UR_EXPECT_TRUE(Precedes(role, "if (device_) return DeviceExtenderProvideStatusLocked();",
                          "if (daemonProviderStats_) return daemonProviderStats_->extenderProvideStatus;"));
  const std::string deviceOnly = ProviderOnlyBody(
      host, "std::optional<urnet::ExtenderProvideStatus> SdkHost::GetExtenderProvideStatus()");
  UR_EXPECT_TRUE(Precedes(deviceOnly, "if (!device_) return std::nullopt;",
                          "return DeviceExtenderProvideStatusLocked();"));
  UR_EXPECT_TRUE(!Contains(deviceOnly, "daemonProviderStats_"));

  const std::string page = ReadProviderOnlySource("EarningsPage.cpp");
  const std::string apply = ProviderOnlyBody(page, "void EarningsPage::ApplyExtenderProvideState()");
  UR_EXPECT_TRUE(Contains(apply, "host_.ProviderExtenderProvideStatus()"));
  UR_EXPECT_TRUE(!Contains(apply, "GetExtenderProvideStatus"));
  UR_EXPECT_TRUE(Contains(apply, "const bool running = status && status->Enabled;"));
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(page, "void EarningsPage::PullProviderThroughput(bool forced)"),
                          "host_.ExtenderThroughputPoints()"));
  UR_EXPECT_TRUE(Contains(ProviderOnlyBody(page, "void EarningsPage::OnHostEvent(DrawerEvent event)"),
                          "case DrawerEvent::ExtenderProvideStatus:\n      ApplyExtenderProvideState();"));

  const std::string connect = ProviderOnlyBody(ReadProviderOnlySource("ConnectPage.cpp"),
                                               "void ConnectPage::ApplyExtenderProvideState()");
  UR_EXPECT_TRUE(Contains(connect, "host_.GetExtenderProvideStatus()"));
  UR_EXPECT_TRUE(!Contains(connect, "ProviderExtenderProvideStatus"));
}
