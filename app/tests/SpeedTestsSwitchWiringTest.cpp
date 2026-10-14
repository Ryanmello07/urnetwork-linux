// The call sites behind the lag.dev speed tests switch (lag.dev/PLAN.md C4,
// decision 18): under the connect page's Extender switch, whether the extender
// role serves lag.dev's measurement API, on by default. It is the Extender
// switch's twin:
//
//   * the GUI reads and writes it through a bound DeviceRemote, and with none
//     through the daemon (set_measurements_enabled) only while provider_stats
//     said it takes that write (provide::MeasurementsSwitchSourceFor);
//   * the daemon takes the verb behind set_provide_extender's owner gate,
//     writes it where the next start reads it, and reports it beside the role;
//   * the page draws it beside the Extender row as just drawn, hidden, never
//     disabled, and never writes it while hidden (N1).
//
// The pure halves are tested in ControlProtocolTest.cpp, ProvideLifecycleTest.cpp
// and ExtenderProvidePresentationTest.cpp; these need glib, GTK and the SDK, so
// this reads their sources.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"
#include "WiringSource.hpp"

#include <string>

namespace {

using namespace urnw::testing::wiring;

}  // namespace

// The daemon: the verb behind set_provide_extender's gate, the write by the
// provider extender setting's rule, and the setting beside the role.
UR_TEST(SpeedTests_TheDaemonTakesTheSwitch) {
  const std::string server = ReadCode("daemon/ControlServer.cpp");
  const std::string authorized = FunctionBody(server, "void ControlServer::DispatchAuthorized(");
  UR_EXPECT_TRUE(Contains(authorized, "case ctl::Verb::SetMeasurementsEnabled: {"));
  const size_t at = authorized.find("case ctl::Verb::SetMeasurementsEnabled: {");
  const std::string branch = at == std::string::npos
                                 ? std::string()
                                 : authorized.substr(at, authorized.find("case ctl::Verb::", at + 1) - at);
  UR_EXPECT_TRUE(Before(branch, "CheckTunnelOwner(conn, id, &denied, &crossUid, authorizedCrossUid)",
                        "tunnel_.SetMeasurementsEnabled(req.measurements_enabled, &error)"));
  UR_EXPECT_TRUE(Before(branch, "request.get<ctl::SetMeasurementsEnabledRequest>()",
                        "tunnel_.SetMeasurementsEnabled(req.measurements_enabled, &error)"));
  // a write nothing took is an error reply, never ok
  UR_EXPECT_TRUE(Before(branch, "ctl::MakeErrorReply(", "reply(ctl::MakeReply(id, true));"));
  UR_EXPECT_FALSE(Contains(branch, "SetProvideExtender"));

  const std::string host = ReadCode("daemon/TunnelHost.cpp");
  const std::string write =
      FunctionBody(host, "bool TunnelHost::SetMeasurementsEnabled(bool on, std::string* error)");
  // refused while a bring-up owns the session, never waited behind
  UR_EXPECT_TRUE(Before(write, "std::unique_lock<std::mutex> lock(opMutex_, std::try_to_lock);",
                        "provide::ExtenderSettingTargetFor("));
  UR_EXPECT_TRUE(Contains(write, "providerDevice_->setMeasurementsEnabled(on);"));
  UR_EXPECT_TRUE(Contains(write, "device_->setMeasurementsEnabled(on);"));
  UR_EXPECT_TRUE(
      Contains(write, "networkSpace_->getAsyncLocalState().getLocalState().setMeasurementsEnabled(on);"));
  UR_EXPECT_FALSE(Contains(write, "setProvideExtender("));

  // reported beside the setting the Extender switch shows, and said to be
  // written only once read
  UR_EXPECT_TRUE(Before(host, "reply.provide_extender_writable = true;",
                        "reply.measurements_enabled = providerDevice_->getMeasurementsEnabled();"));
  UR_EXPECT_TRUE(Before(host, "reply.measurements_enabled = providerDevice_->getMeasurementsEnabled();",
                        "reply.measurements_enabled_writable = true;"));
}

// The GUI: read and written by the switch's own source, the daemon's write read
// back at once.
UR_TEST(SpeedTests_TheGuiReadsAndWritesByItsOwnSource) {
  const std::string host = ReadCode("SdkHost.cpp");
  const std::string source =
      FunctionBody(host, "provide::ExtenderSwitchSource SdkHost::MeasurementsSwitchSourceLocked() const");
  UR_EXPECT_TRUE(Contains(source, "provide::MeasurementsSwitchSourceFor("));
  UR_EXPECT_TRUE(Contains(source, "daemonProviderStats_->measurementsEnabledWritable"));
  UR_EXPECT_TRUE(Contains(source, "daemonProviderStats_->provideExtenderWritable"));

  const std::string get = FunctionBody(host, "bool SdkHost::GetMeasurementsEnabled()");
  UR_EXPECT_TRUE(Contains(get, "MeasurementsSwitchSourceLocked()"));
  UR_EXPECT_TRUE(Contains(get, "device_->getMeasurementsEnabled()"));
  UR_EXPECT_TRUE(Contains(get, "daemonProviderStats_->measurementsEnabled"));

  const std::string set = FunctionBody(host, "void SdkHost::SetMeasurementsEnabled(bool on)");
  UR_EXPECT_TRUE(Contains(set, "MeasurementsSwitchSourceLocked()"));
  UR_EXPECT_TRUE(Contains(set, "device_->setMeasurementsEnabled(on);"));
  UR_EXPECT_TRUE(Before(set, "control_.SetMeasurementsEnabled(on, &error)",
                        "PollDaemonProviderStatsLocked();"));
  UR_EXPECT_TRUE(Before(set, "PollDaemonProviderStatsLocked();",
                        "EmitDrawerEvent(DrawerEvent::ExtenderProvideStatus);"));
  UR_EXPECT_FALSE(Contains(set, "ExtenderSwitchSourceLocked()"));

  // a poll that moves only this setting or its writer still redraws the page
  const std::string poll = FunctionBody(host, "void SdkHost::PollDaemonProviderStatsLocked()");
  UR_EXPECT_TRUE(Contains(poll, "stats.measurementsEnabled = reply->measurements_enabled;"));
  UR_EXPECT_TRUE(
      Contains(poll, "stats.measurementsEnabledWritable = reply->measurements_enabled_writable;"));
  UR_EXPECT_TRUE(Contains(poll, "daemonProviderStats_->measurementsEnabledWritable != "
                                "stats.measurementsEnabledWritable"));
}

// The connect page: drawn beside the Extender row as just drawn, the setting
// read only for a row that shows, never written while hidden, and named by the
// catalog's strings.
UR_TEST(SpeedTests_TheConnectPageDrawsItBesideTheExtenderRow) {
  const std::string page = ReadCode("ConnectPage.cpp");
  const std::string apply = FunctionBody(page, "void ConnectPage::ApplyExtenderProvideState()");
  UR_EXPECT_TRUE(Before(apply, "DrawExtenderRow(", "DrawSpeedTestsRow("));
  UR_EXPECT_TRUE(Contains(apply, "extender::SpeedTestsRowOf(extenderRowDrawn_,"));
  UR_EXPECT_TRUE(Contains(apply, "host_.MeasurementsSwitchWritable()"));
  UR_EXPECT_TRUE(Contains(apply, "[this] { return host_.GetMeasurementsEnabled(); }"));

  const std::string draw =
      FunctionBody(page, "void ConnectPage::DrawSpeedTestsRow(const extender::SpeedTestsRow& row)");
  UR_EXPECT_TRUE(Contains(draw, "speedTestsRow_->set_visible(row.visible);"));
  UR_EXPECT_TRUE(Before(draw, "updatingControls_ = true;", "speedTestsToggle_->set_active(row.on);"));
  UR_EXPECT_FALSE(Contains(draw, "set_sensitive("));
  UR_EXPECT_FALSE(Contains(draw, "SetMeasurementsEnabled("));

  const std::string toggled = FunctionBody(page, "void ConnectPage::OnSpeedTestsToggled()");
  UR_EXPECT_TRUE(Before(toggled, "if (!speedTestsDrawn_.visible) return;",
                        "host_.SetMeasurementsEnabled(on);"));

  UR_EXPECT_TRUE(Contains(page, "T_(\"extender_speed_tests\", \"lag.dev speed tests\")"));
  UR_EXPECT_TRUE(Contains(page, "\"extender_speed_tests_description\""));
  // the switch's handler is echo-guarded, as every feed-driven switch is
  UR_EXPECT_TRUE(Contains(page, "OnSpeedTestsToggled();"));
  UR_EXPECT_TRUE(Before(page, "moreOptionsHost_->append(*extenderDescription_);",
                        "moreOptionsHost_->append(*speedTestsRow_);"));
}
