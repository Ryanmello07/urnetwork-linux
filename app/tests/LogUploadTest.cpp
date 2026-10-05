// "Send feedback with logs" while disconnected (support inbox 2090): which
// device carries the upload, how long a standalone one lives, how long a
// request waits for a tunnel start, and what the GUI does with the daemon's
// answer (LogUpload.hpp). The wire half is in ControlProtocolTest.cpp, the call
// sites in LogUploadWiringTest.cpp.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "LogUpload.hpp"

namespace logupload = urnw::logupload;

// The device that runs carries it: the tunnel session's first (it is the one
// the DeviceRemote path always reached), then the provider-only device, and a
// standalone device only when neither runs — never a second device beside one.
UR_TEST(logUploadCarrierIsTheDeviceThatRuns) {
  UR_EXPECT_TRUE(logupload::CarrierFor(true, false) == logupload::Carrier::Tunnel);
  UR_EXPECT_TRUE(logupload::CarrierFor(false, true) == logupload::Carrier::Provider);
  UR_EXPECT_TRUE(logupload::CarrierFor(false, false) == logupload::Carrier::Standalone);
  // the two never run together, and if they did the session's device wins
  UR_EXPECT_TRUE(logupload::CarrierFor(true, true) == logupload::Carrier::Tunnel);
}

// The reply's carrier and the uploaded log line use these words; the GUI logs
// them. Distinct, and stable on the wire.
UR_TEST(logUploadCarrierWireNames) {
  UR_EXPECT_TRUE(std::string(logupload::ToString(logupload::Carrier::Tunnel)) == "tunnel");
  UR_EXPECT_TRUE(std::string(logupload::ToString(logupload::Carrier::Provider)) == "provider");
  UR_EXPECT_TRUE(std::string(logupload::ToString(logupload::Carrier::Standalone)) ==
                 "standalone");
  UR_EXPECT_TRUE(std::string(logupload::ToString(logupload::Carrier::Queued)) == "queued");
}

// A standalone device goes as soon as its upload reports, whatever its age,
// and otherwise at the bound: it connects to the platform like any device, so
// a stuck upload must not keep it up.
UR_TEST(logUploadStandaloneDeviceIsRetiredOnReportOrAtTheBound) {
  const int64_t built = 1000000;
  UR_EXPECT_TRUE(logupload::kStandaloneDeviceMaxMillis == 30LL * 60 * 1000);
  // reported: at once
  UR_EXPECT_TRUE(logupload::RetireStandaloneDevice(true, built, built));
  UR_EXPECT_TRUE(logupload::RetireStandaloneDevice(true, built, built + 1));
  // not reported: kept while the upload may still be running
  UR_EXPECT_FALSE(logupload::RetireStandaloneDevice(false, built, built));
  UR_EXPECT_FALSE(logupload::RetireStandaloneDevice(
      false, built, built + logupload::kStandaloneDeviceMaxMillis - 1));
  // ...and no longer
  UR_EXPECT_TRUE(logupload::RetireStandaloneDevice(
      false, built, built + logupload::kStandaloneDeviceMaxMillis));
  UR_EXPECT_TRUE(logupload::RetireStandaloneDevice(
      false, built, built + 2 * logupload::kStandaloneDeviceMaxMillis));
}

// A request queued behind a tunnel start runs once the start is over, unless
// it waited so long that the start has hung.
UR_TEST(logUploadQueuedRequestExpires) {
  const int64_t queued = 5000;
  UR_EXPECT_TRUE(logupload::kQueuedUploadMaxMillis == 5LL * 60 * 1000);
  UR_EXPECT_FALSE(logupload::QueuedUploadExpired(queued, queued));
  UR_EXPECT_FALSE(
      logupload::QueuedUploadExpired(queued, queued + logupload::kQueuedUploadMaxMillis - 1));
  UR_EXPECT_TRUE(
      logupload::QueuedUploadExpired(queued, queued + logupload::kQueuedUploadMaxMillis));
}

// The fallback: the daemon's acceptance ends it; anything else (no daemon, a
// daemon that predates the verb, a refusal) gets the path the GUI had before
// the verb existed: the DeviceRemote while one is bound, nothing otherwise.
UR_TEST(logUploadGuiFallsBackOnlyWhenTheDaemonDidNotTakeIt) {
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(true, false) == logupload::GuiStep::Done);
  // not a second upload beside the daemon's: the server would refuse it anyway
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(true, true) == logupload::GuiStep::Done);
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(false, true) ==
                 logupload::GuiStep::DeviceRemote);
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(false, false) == logupload::GuiStep::Skip);
}
