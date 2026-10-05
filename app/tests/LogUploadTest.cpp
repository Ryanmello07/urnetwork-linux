// "Send feedback with logs" while disconnected (support inbox 2090): which
// device carries the upload, how long a standalone one lives, how long a
// request waits for a tunnel start, the upload in flight that runs off the
// daemon's main loop, and what the GUI does with the daemon's answer and how it
// learns the outcome (LogUpload.hpp). The wire half is in
// ControlProtocolTest.cpp, the call sites in LogUploadWiringTest.cpp.
//
// The flight's thread is held by barriers, never by the clock: an upload that
// runs on the caller's own thread returns at once and fails the check that
// names it, rather than hanging the run.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

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
  // finished: at once
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

// The fallback: the daemon's acceptance ends it, and so does its answer that
// an upload is in flight (the server would refuse a second); anything else (no
// daemon, a daemon that predates the verb, a refusal) gets the path the GUI had
// before the verb existed: the DeviceRemote while one is bound, nothing
// otherwise.
UR_TEST(logUploadGuiFallsBackOnlyWhenTheDaemonDidNotTakeIt) {
  using logupload::DaemonAnswer;
  using logupload::GuiStep;
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(DaemonAnswer::Accepted, false) == GuiStep::Done);
  // not a second upload beside the daemon's: the server would refuse it anyway
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(DaemonAnswer::Accepted, true) == GuiStep::Done);
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(DaemonAnswer::Busy, true) == GuiStep::Done);
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(DaemonAnswer::Busy, false) == GuiStep::Done);
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(DaemonAnswer::NotTaken, true) ==
                 GuiStep::DeviceRemote);
  UR_EXPECT_TRUE(logupload::GuiStepAfterDaemon(DaemonAnswer::NotTaken, false) == GuiStep::Skip);
}

// status' words for where the upload is, and back
UR_TEST(logUploadFlightStateWireNames) {
  using logupload::FlightState;
  for (const FlightState state : {FlightState::None, FlightState::Queued, FlightState::Running,
                                  FlightState::Uploaded, FlightState::Refused,
                                  FlightState::Failed}) {
    UR_EXPECT_TRUE_MSG(logupload::ToString(state),
                       logupload::FlightStateFromString(logupload::ToString(state)) == state);
  }
  UR_EXPECT_TRUE(std::string(logupload::ToString(FlightState::None)).empty());
  UR_EXPECT_TRUE(std::string(logupload::ToString(FlightState::Uploaded)) == "uploaded");
  UR_EXPECT_TRUE(logupload::FlightStateFromString("a state from a newer daemon") ==
                 FlightState::None);
  UR_EXPECT_FALSE(logupload::IsFinished(FlightState::Queued));
  UR_EXPECT_FALSE(logupload::IsFinished(FlightState::Running));
  UR_EXPECT_TRUE(logupload::IsFinished(FlightState::Refused));
}

// The upload runs on a thread of its own: Run returns while the sdk's call
// (here held by a barrier) is still zipping, and the flight answers another
// request meanwhile, so the main loop that runs both is never held by a slow
// upload. Then the call's outcome is what status reads.
UR_TEST(logUploadFlightRunsTheUploadOffTheCallersThread) {
  auto flight = std::make_shared<logupload::Flight>(100);
  const int64_t uploadId = flight->Begin(/*queued=*/false, logupload::Carrier::Tunnel, 1000);
  UR_EXPECT_EQ(100, uploadId);

  const std::thread::id caller = std::this_thread::get_id();
  std::atomic<bool> ranOnCaller{false};
  std::promise<void> entered;
  std::promise<void> release;
  std::shared_future<void> released = release.get_future().share();
  flight->Run(uploadId, /*deviceHandle=*/7, [&ranOnCaller, &entered, released, flight, uploadId,
                                             caller] {
    if (std::this_thread::get_id() == caller) {
      // a Run that calls in place: report it instead of waiting forever
      ranOnCaller = true;
      return;
    }
    entered.set_value();
    released.wait();
    flight->Finish(uploadId, logupload::FlightState::Uploaded);
  });
  UR_EXPECT_FALSE(ranOnCaller.load());
  if (ranOnCaller.load()) return;
  entered.get_future().wait();

  // the call is under way: status and a second request are answered
  UR_EXPECT_TRUE(flight->Read(1001).state == logupload::FlightState::Running);
  UR_EXPECT_EQ(0, flight->Begin(/*queued=*/false, logupload::Carrier::Tunnel, 1001));

  release.set_value();
  UR_EXPECT_TRUE(flight->WaitReturned(std::chrono::seconds(60)));
  const logupload::Flight::Reading done = flight->Read(1002);
  UR_EXPECT_EQ(100, done.id);
  UR_EXPECT_TRUE(done.state == logupload::FlightState::Uploaded);
  UR_EXPECT_TRUE(done.carrier == logupload::Carrier::Tunnel);
}

// One upload at a time: queued, running, and until its thread is out of the
// sdk's call (an outcome can be reported before the call returns). Then the
// next one is admitted, numbered after it.
UR_TEST(logUploadFlightAdmitsOneUploadAtATime) {
  auto flight = std::make_shared<logupload::Flight>(1);
  const int64_t queued = flight->Begin(/*queued=*/true, logupload::Carrier::Queued, 0);
  UR_EXPECT_EQ(1, queued);
  UR_EXPECT_TRUE(flight->Read(1).state == logupload::FlightState::Queued);
  UR_EXPECT_EQ(0, flight->Begin(/*queued=*/false, logupload::Carrier::Tunnel, 1));
  UR_EXPECT_EQ(0, flight->Begin(/*queued=*/true, logupload::Carrier::Queued, 1));
  // the bring-up is over: it runs on the device it left
  UR_EXPECT_TRUE(flight->Start(queued, logupload::Carrier::Standalone, 2));
  UR_EXPECT_FALSE(flight->Start(queued, logupload::Carrier::Standalone, 2));
  UR_EXPECT_EQ(0, flight->Begin(/*queued=*/false, logupload::Carrier::Tunnel, 2));

  const std::thread::id caller = std::this_thread::get_id();
  std::atomic<bool> ranOnCaller{false};
  std::promise<void> reported;
  std::promise<void> release;
  std::shared_future<void> released = release.get_future().share();
  flight->Run(queued, /*deviceHandle=*/9,
              [&ranOnCaller, &reported, released, flight, queued, caller] {
                if (std::this_thread::get_id() == caller) {
                  ranOnCaller = true;
                  return;
                }
                flight->Finish(queued, logupload::FlightState::Refused);
                reported.set_value();
                released.wait();
              });
  UR_EXPECT_FALSE(ranOnCaller.load());
  if (ranOnCaller.load()) return;
  reported.get_future().wait();
  // reported, but its thread is still in the call
  UR_EXPECT_TRUE(flight->Read(3).state == logupload::FlightState::Refused);
  UR_EXPECT_EQ(0, flight->Begin(/*queued=*/false, logupload::Carrier::Tunnel, 3));
  release.set_value();
  UR_EXPECT_TRUE(flight->WaitReturned(std::chrono::seconds(60)));
  UR_EXPECT_EQ(2, flight->Begin(/*queued=*/false, logupload::Carrier::Tunnel, 4));
}

// The call's device stays alive until the call returns: a retire path that
// releases it meanwhile hands it to the flight, which lets go of it on the
// upload's thread after the call. Any other device is handed back at once.
UR_TEST(logUploadFlightKeepsTheCallsDeviceUntilTheCallReturns) {
  auto flight = std::make_shared<logupload::Flight>(1);
  const int64_t uploadId = flight->Begin(/*queued=*/false, logupload::Carrier::Provider, 0);
  const std::thread::id caller = std::this_thread::get_id();
  std::atomic<bool> ranOnCaller{false};
  std::promise<void> entered;
  std::promise<void> release;
  std::shared_future<void> released = release.get_future().share();
  flight->Run(uploadId, /*deviceHandle=*/7, [&ranOnCaller, &entered, released, caller] {
    if (std::this_thread::get_id() == caller) {
      ranOnCaller = true;
      return;
    }
    entered.set_value();
    released.wait();
  });
  UR_EXPECT_FALSE(ranOnCaller.load());
  if (ranOnCaller.load()) return;
  entered.get_future().wait();

  auto device = std::make_shared<std::string>("the call's device");
  std::weak_ptr<std::string> callsDevice = device;
  UR_EXPECT_TRUE(flight->KeepUntilReturned(7, std::move(device)) == nullptr);
  UR_EXPECT_FALSE(callsDevice.expired());
  auto otherDevice = std::make_shared<std::string>("another device");
  UR_EXPECT_TRUE(flight->KeepUntilReturned(8, otherDevice) == otherDevice);

  release.set_value();
  UR_EXPECT_TRUE(flight->WaitReturned(std::chrono::seconds(60)));
  UR_EXPECT_TRUE(callsDevice.expired());
  // out of the call: the device is the retire path's to release
  auto later = std::make_shared<std::string>("the call's device, retired later");
  UR_EXPECT_TRUE(flight->KeepUntilReturned(7, later) == later);
}

// One outcome per upload, the first; none for an upload no longer in flight.
// An upload that never reports is given up on at the bound, so a post that
// hangs cannot keep every later upload out.
UR_TEST(logUploadFlightKeepsTheFirstOutcomeAndGivesUpOnASilentUpload) {
  auto flight = std::make_shared<logupload::Flight>(40);
  UR_EXPECT_TRUE(flight->Read(0).state == logupload::FlightState::None);
  UR_EXPECT_EQ(0, flight->Read(0).id);
  const int64_t first = flight->Begin(/*queued=*/false, logupload::Carrier::Standalone, 0);
  flight->Finish(first + 1, logupload::FlightState::Uploaded);
  UR_EXPECT_TRUE(flight->Read(1).state == logupload::FlightState::Running);
  flight->Finish(first, logupload::FlightState::Running);  // not an outcome
  UR_EXPECT_TRUE(flight->Read(1).state == logupload::FlightState::Running);
  flight->Finish(first, logupload::FlightState::Uploaded);
  flight->Finish(first, logupload::FlightState::Failed);
  UR_EXPECT_TRUE(flight->Read(2).state == logupload::FlightState::Uploaded);

  const int64_t silent = flight->Begin(/*queued=*/false, logupload::Carrier::Standalone, 10);
  UR_EXPECT_EQ(first + 1, silent);
  UR_EXPECT_TRUE(flight->Read(10 + logupload::kStandaloneDeviceMaxMillis - 1).state ==
                 logupload::FlightState::Running);
  UR_EXPECT_TRUE(flight->Read(10 + logupload::kStandaloneDeviceMaxMillis).state ==
                 logupload::FlightState::Failed);
  // its callback, arriving after, changes nothing
  flight->Finish(silent, logupload::FlightState::Uploaded);
  UR_EXPECT_TRUE(flight->Read(10 + logupload::kStandaloneDeviceMaxMillis).state ==
                 logupload::FlightState::Failed);
  UR_EXPECT_EQ(silent + 1, flight->Begin(/*queued=*/true, logupload::Carrier::Queued,
                                         10 + logupload::kStandaloneDeviceMaxMillis));
}

// The GUI learns the outcome of the upload it waits on, by the id the reply
// named, from the daemon's status: only once it is finished, and never another
// upload's.
UR_TEST(logUploadCompletionReachesTheGuiByItsId) {
  using logupload::FlightState;
  UR_EXPECT_FALSE(logupload::CompletionFor(0, 0, FlightState::None).has_value());
  UR_EXPECT_FALSE(logupload::CompletionFor(0, 5, FlightState::Uploaded).has_value());
  UR_EXPECT_FALSE(logupload::CompletionFor(5, 5, FlightState::Running).has_value());
  UR_EXPECT_FALSE(logupload::CompletionFor(5, 5, FlightState::Queued).has_value());
  UR_EXPECT_FALSE(logupload::CompletionFor(5, 6, FlightState::Uploaded).has_value());
  for (const FlightState state :
       {FlightState::Uploaded, FlightState::Refused, FlightState::Failed}) {
    const auto outcome = logupload::CompletionFor(5, 5, state);
    UR_EXPECT_TRUE_MSG(logupload::ToString(state), outcome.has_value() && *outcome == state);
  }
}
