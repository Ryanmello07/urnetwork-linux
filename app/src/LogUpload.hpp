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
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

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

// The standalone device is retired once its upload has reported, or once it
// has run kStandaloneDeviceMaxMillis without a report. (Every bring-up and
// every provider-only start retires it first as well: see TunnelHost.)
constexpr bool RetireStandaloneDevice(bool uploadReported, int64_t builtMillis,
                                      int64_t nowMillis) {
  return uploadReported || nowMillis - builtMillis >= kStandaloneDeviceMaxMillis;
}

// How long a request may wait for a tunnel start in progress. A bring-up takes
// seconds; one still running after this has hung, and an upload started long
// after the user pressed Send would carry logs from a different moment.
inline constexpr int64_t kQueuedUploadMaxMillis = 5LL * 60 * 1000;

constexpr bool QueuedUploadExpired(int64_t queuedMillis, int64_t nowMillis) {
  return nowMillis - queuedMillis >= kQueuedUploadMaxMillis;
}

// ---- the GUI's half --------------------------------------------------------
// After the server accepted the feedback with the box ticked, the GUI asks the
// daemon first. Anything but its acceptance — no daemon, a daemon that predates
// the verb (`unknown verb`), a refusal — falls back to what the GUI did before
// the verb existed: the DeviceRemote's UploadLogs while one is bound (it
// reaches the same daemon device over the device RPC), nothing otherwise. A
// lost reply can therefore cost a second upload, which the server refuses (one
// upload per network per 5 minutes, one file per feedback).
enum class GuiStep {
  Done,          // the daemon took it
  DeviceRemote,  // the old path
  Skip,          // nothing can carry it
};

constexpr GuiStep GuiStepAfterDaemon(bool daemonAccepted, bool deviceRemoteBound) {
  if (daemonAccepted) return GuiStep::Done;
  if (deviceRemoteBound) return GuiStep::DeviceRemote;
  return GuiStep::Skip;
}

}  // namespace urnw::logupload
