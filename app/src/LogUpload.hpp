// LogUpload — which device carries "send feedback with logs", and for how long
// (support inbox 2090). The wire contract is ControlProtocol.hpp's upload_logs;
// this is the lifecycle both halves decide on, pure so it is unit-tested
// without a daemon, a GUI or the SDK (LogUploadTest.cpp), the way
// ProvideLifecycle.hpp is for the provider-only device.
//
// The logs support reads are urnetworkd's: the sdk's UploadLogs zips the glog
// files of the process it runs in. The GUI's DeviceRemote reaches the daemon's
// DeviceLocal only while a tunnel session runs, so a report sent while
// disconnected, held by the kill switch or failing to connect carried no logs.
// The daemon now uploads its own logs on request, on whichever device runs:
//
//   tunnel      the tunnel session's DeviceLocal (what the DeviceRemote reached)
//   provider    the provider-only device, while disconnected
//   standalone  neither runs: a device built for the upload from the request's
//               credentials, as start_provider builds its device, with provide
//               mode never and nothing else, retired once the upload reports
//   queued      a tunnel start is in progress: the device it leaves behind (the
//               session's, or a standalone one after a failure) carries it
//
// The upload runs off the daemon's main loop (Flight): the sdk zips the log
// directory inside its UploadLogs call, up to the upload's cap read from disk,
// and the main loop serves every control request, the reaper and the kill
// switch. The request is answered once the upload is admitted, and its outcome
// reaches the GUI through status (CompletionFor).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace urnw::logupload {

enum class Carrier {
  Tunnel,
  Provider,
  Standalone,
  Queued,
};

// The wire value (ctl::UploadLogsReply::carrier) and the log's word for it.
constexpr const char* ToString(Carrier carrier) {
  switch (carrier) {
    case Carrier::Tunnel: return "tunnel";
    case Carrier::Provider: return "provider";
    case Carrier::Standalone: return "standalone";
    case Carrier::Queued: return "queued";
  }
  return "standalone";
}

// The device that carries an upload the daemon can start now. The tunnel
// session's device first: it is the one the DeviceRemote path always used, and
// a provider-only device never runs beside it. A standalone device only when
// neither runs, because a second device under the same identity would compete
// with the first.
constexpr Carrier CarrierFor(bool tunnelDevice, bool providerDevice) {
  if (tunnelDevice) return Carrier::Tunnel;
  if (providerDevice) return Carrier::Provider;
  return Carrier::Standalone;
}

// How long a standalone device may wait for its upload to report. The upload
// has no deadline of its own (the sdk's streaming POST is bounded by its dial
// and HTTP/2 progress limits only), and the device connects to the platform
// like any other, so it must not outlive a stuck upload by much. 30 minutes
// carries the server's 100 MB cap at under half a megabit per second; a
// zipped glog directory is a small fraction of that.
inline constexpr int64_t kStandaloneDeviceMaxMillis = 30LL * 60 * 1000;

// The standalone device is retired once its upload has finished (reported,
// or given up on by the flight), or once it has run kStandaloneDeviceMaxMillis.
// (Every bring-up and every provider-only start retires it first as well: see
// TunnelHost.)
constexpr bool RetireStandaloneDevice(bool uploadFinished, int64_t builtMillis,
                                      int64_t nowMillis) {
  return uploadFinished || nowMillis - builtMillis >= kStandaloneDeviceMaxMillis;
}

// How long a request may wait for a tunnel start in progress. A bring-up takes
// seconds; one still running after this has hung, and an upload started long
// after the user pressed Send would carry logs from a different moment.
inline constexpr int64_t kQueuedUploadMaxMillis = 5LL * 60 * 1000;

constexpr bool QueuedUploadExpired(int64_t queuedMillis, int64_t nowMillis) {
  return nowMillis - queuedMillis >= kQueuedUploadMaxMillis;
}

// ---- the upload in flight ----------------------------------------------------

// Where the upload in flight is, as status reports it (log_upload_state).
enum class FlightState {
  None,      // nothing asked since the daemon started
  Queued,    // waits for a tunnel start in progress
  Running,   // the zip or the post is under way
  Uploaded,  // the server took it
  Refused,   // the server answered with an error (its rate limit, its cap)
  Failed,    // it did not reach the server, or never reported
};

// The wire value ("" for none).
constexpr const char* ToString(FlightState state) {
  switch (state) {
    case FlightState::None: return "";
    case FlightState::Queued: return "queued";
    case FlightState::Running: return "running";
    case FlightState::Uploaded: return "uploaded";
    case FlightState::Refused: return "refused";
    case FlightState::Failed: return "failed";
  }
  return "";
}

// The state for a wire value; one this build does not know is none.
constexpr FlightState FlightStateFromString(std::string_view value) {
  if (value == "queued") return FlightState::Queued;
  if (value == "running") return FlightState::Running;
  if (value == "uploaded") return FlightState::Uploaded;
  if (value == "refused") return FlightState::Refused;
  if (value == "failed") return FlightState::Failed;
  return FlightState::None;
}

// The upload has an outcome.
constexpr bool IsFinished(FlightState state) {
  return state == FlightState::Uploaded || state == FlightState::Refused ||
         state == FlightState::Failed;
}

// The daemon's upload in flight: one at a time, run on a thread of its own.
// The server admits one upload per network per 5 minutes and keeps one file
// per feedback, so a second upload while one runs would only be refused; a
// request then is answered busy.
//
// The daemon's main loop admits (Begin, Start) and runs (Run) an upload, and
// reads the flight into status (Read). The upload's thread runs the sdk's call
// and its callback ends the upload (Finish); they hold a share of the flight
// and nothing of the daemon. The flight's lock is innermost, and it is never
// held across the call or across a device's release.
//
// Made with std::make_shared: the upload's thread holds a share of it.
//
// The call's device stays alive until the call returns: a retire path that
// releases a device the call is on hands it to the flight instead
// (KeepUntilReturned), and the upload's thread releases it once out of the call.
// The device is closed by its retire path as always; the sdk's upload goes on
// on a closed device, through its network space's api.
class Flight : public std::enable_shared_from_this<Flight> {
 public:
  // What status reports of the flight.
  struct Reading {
    // 0 before the first upload
    int64_t id = 0;
    FlightState state = FlightState::None;
    // meaningful while running and after
    Carrier carrier = Carrier::Queued;
  };

  // `firstId` numbers the first upload. The daemon starts it from its start
  // time, so that a GUI waiting on an id across a daemon restart does not take
  // another upload's outcome for its own.
  explicit Flight(int64_t firstId) : nextId_(firstId) {}

  Flight(const Flight&) = delete;
  Flight& operator=(const Flight&) = delete;

  // Admits an upload, queued behind a tunnel start or running on `carrier`
  // now: its id, or 0 when one is in flight, or its thread is still in the
  // sdk's call.
  int64_t Begin(bool queued, Carrier carrier, int64_t nowMillis) {
    std::scoped_lock lock(mutex_);
    ExpireSilentWithLock(nowMillis);
    if (BusyWithLock()) return 0;
    reading_.id = nextId_++;
    reading_.state = queued ? FlightState::Queued : FlightState::Running;
    reading_.carrier = queued ? Carrier::Queued : carrier;
    sinceMillis_ = nowMillis;
    return reading_.id;
  }

  // The queued upload `id` runs on `carrier` now; false when it is not the
  // upload queued.
  bool Start(int64_t id, Carrier carrier, int64_t nowMillis) {
    std::scoped_lock lock(mutex_);
    if (reading_.id != id || reading_.state != FlightState::Queued) return false;
    reading_.state = FlightState::Running;
    reading_.carrier = carrier;
    sinceMillis_ = nowMillis;
    return true;
  }

  // Runs `call`, the sdk's upload on the device behind `deviceHandle`, on a
  // thread of its own, and returns at once. Once `call` returns, the devices
  // kept for it (KeepUntilReturned) are released on that thread. `call` must
  // touch nothing of the caller; it ends the upload through Finish.
  void Run(int64_t id, uint64_t deviceHandle, std::function<void()> call) {
    {
      std::scoped_lock lock(mutex_);
      calling_ = true;
      callingId_ = id;
      callingDeviceHandle_ = deviceHandle;
    }
    std::thread([flight = shared_from_this(), id, call = std::move(call)] {
      try {
        call();
      } catch (...) {
        flight->Finish(id, FlightState::Failed);
      }
      // released here, outside the lock: a release is a call into the sdk
      std::vector<std::shared_ptr<void>> keptDevices = flight->Returned(id);
      keptDevices.clear();
      flight->Released(id);
    }).detach();
  }

  // A retire path is about to release `device`, the device behind
  // `deviceHandle`. While the upload's call is on it, the flight keeps it until
  // the call returns, and this answers null. Otherwise it is handed back for
  // the caller to release now.
  std::shared_ptr<void> KeepUntilReturned(uint64_t deviceHandle, std::shared_ptr<void> device) {
    std::scoped_lock lock(mutex_);
    if (!calling_ || callingDeviceHandle_ != deviceHandle || deviceHandle == 0) return device;
    keptDevices_.push_back(std::move(device));
    return nullptr;
  }

  // The upload `id` ended with `state`, once. Nothing for an upload no longer
  // in flight, or one that has ended already.
  void Finish(int64_t id, FlightState state) {
    std::scoped_lock lock(mutex_);
    if (reading_.id != id || IsFinished(reading_.state) || !IsFinished(state)) return;
    reading_.state = state;
  }

  // The flight now. An upload that has run kStandaloneDeviceMaxMillis without
  // an outcome is given up on (failed), so that a post that never reports
  // cannot keep every later upload out.
  Reading Read(int64_t nowMillis) {
    std::scoped_lock lock(mutex_);
    ExpireSilentWithLock(nowMillis);
    return reading_;
  }

  // Waits up to `budget` for the upload's thread to be out of the sdk's call,
  // with the devices kept for it released; true once it is. For the daemon's
  // teardown and for tests.
  bool WaitReturned(std::chrono::milliseconds budget) {
    std::unique_lock lock(mutex_);
    return returned_.wait_for(lock, budget, [this] { return !calling_ && !releasing_; });
  }

 private:
  // The thread is out of the sdk's call: the devices kept for it, to release.
  // From here a device is handed back to its retire path (KeepUntilReturned).
  std::vector<std::shared_ptr<void>> Returned(int64_t id) {
    std::scoped_lock lock(mutex_);
    if (callingId_ != id) return {};
    calling_ = false;
    releasing_ = true;
    callingDeviceHandle_ = 0;
    return std::exchange(keptDevices_, {});
  }

  // The kept devices are released: the thread is done with the upload.
  void Released(int64_t id) {
    {
      std::scoped_lock lock(mutex_);
      if (callingId_ != id) return;
      releasing_ = false;
    }
    returned_.notify_all();
  }

  bool BusyWithLock() const {
    return calling_ || releasing_ || reading_.state == FlightState::Queued ||
           reading_.state == FlightState::Running;
  }

  void ExpireSilentWithLock(int64_t nowMillis) {
    if ((reading_.state == FlightState::Queued || reading_.state == FlightState::Running) &&
        nowMillis - sinceMillis_ >= kStandaloneDeviceMaxMillis) {
      reading_.state = FlightState::Failed;
    }
  }

  std::mutex mutex_;
  std::condition_variable returned_;
  int64_t nextId_ = 1;
  Reading reading_;
  int64_t sinceMillis_ = 0;
  // the upload's thread is in the sdk's call, on this upload's device; then
  // releases the devices kept for it
  bool calling_ = false;
  bool releasing_ = false;
  int64_t callingId_ = 0;
  uint64_t callingDeviceHandle_ = 0;
  std::vector<std::shared_ptr<void>> keptDevices_;
};

// ---- the GUI's half --------------------------------------------------------
// After the server accepted the feedback with the box ticked, the GUI asks the
// daemon first. The daemon's acceptance ends it, and so does its answer that
// an upload is in flight already: the server would refuse a second one. Any
// other answer — no daemon, a daemon that predates the verb (`unknown verb`),
// a refusal — falls back to what the GUI did before the verb existed: the
// DeviceRemote's UploadLogs while one is bound (it reaches the same daemon
// device over the device RPC), nothing otherwise. A lost reply can therefore
// cost a second upload, which the server refuses (one upload per network per 5
// minutes, one file per feedback).
enum class DaemonAnswer {
  Accepted,  // the daemon admitted the upload
  Busy,      // an upload is in flight already
  NotTaken,  // no daemon, an older daemon, a refusal
};

enum class GuiStep {
  Done,          // the daemon took it, or has one in flight
  DeviceRemote,  // the old path
  Skip,          // nothing can carry it
};

constexpr GuiStep GuiStepAfterDaemon(DaemonAnswer answer, bool deviceRemoteBound) {
  if (answer != DaemonAnswer::NotTaken) return GuiStep::Done;
  if (deviceRemoteBound) return GuiStep::DeviceRemote;
  return GuiStep::Skip;
}

// The upload's outcome, once the daemon's status names the upload the GUI
// waits on (`pendingId`, from the reply) as finished; none before, and none
// for another upload.
constexpr std::optional<FlightState> CompletionFor(int64_t pendingId, int64_t statusId,
                                                   FlightState statusState) {
  if (pendingId == 0 || statusId != pendingId || !IsFinished(statusState)) return std::nullopt;
  return statusState;
}

}  // namespace urnw::logupload
