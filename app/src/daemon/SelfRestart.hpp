// The daemon's way out of an SDK that stopped answering: give the machine back,
// then end the process for systemd to start a clean one, and have that one say
// why the tunnel stopped.
//
// The dead-tunnel failsafe's SdkUnresponsive verdict means no call into the
// SDK has come back for 30 s: the session's device is stuck behind its own
// state lock. Every call left to make on that device takes the same lock
// (setTunnelStarted and close in the teardown, every getter), and nothing else
// in the process can be trusted not to reach it. Made on the main loop, such a
// call stops `status`, stop_tunnel, set_kill_switch and the SIGTERM handler
// with it, until systemd's SIGKILL 45 s into a stop. So once the machine is
// given back (routes, DNS and the firewall's landing, none of which waits on
// the device), TunnelHost ends the process, and Restart=on-failure starts a
// clean daemon 2 s later, as the Windows service restarts itself under its
// service manager. A thread inside the SDK can be neither cancelled nor
// joined, so the exit is _exit: unwinding would run destructors that call
// into it.
//
// The next daemon still has to say why the tunnel stopped, so the stopping one
// leaves a stop record in /run/urnetwork with the reason, code and sentence its
// status carried, and the next one publishes it as its own status and deletes
// it. /run is tmpfs, so a reboot clears it, and a record older than
// kStopRecordMaxAgeMillis is about some earlier stop and is dropped.
//
// Pure: the file and the exit are TunnelHost's.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "ControlProtocol.hpp"

namespace urnw::selfrestart {

// EX_TEMPFAIL (sysexits.h). Not 0, which Restart=on-failure would not
// restart, and not 1, which every other failed start exits with, so the
// journal and `systemctl status` name this exit.
inline constexpr int kExitStatus = 75;

// Beside the control socket and the armed marker, on tmpfs.
inline constexpr const char* kStopRecordPath = "/run/urnetwork/last-stop.json";

// Far longer than a restart takes (RestartSec=2, ExecStopPost's sweep and the
// next start), and short enough that a daemon started by hand much later does
// not report a stop nobody is waiting to hear about.
inline constexpr int64_t kStopRecordMaxAgeMillis = 120000;

// What the stopping daemon's status said. `writtenMillis` is on
// CLOCK_BOOTTIME, which both daemons share and no clock change moves.
struct StopRecord {
  std::string stopReason;
  std::string error;
  std::string code;
  int64_t writtenMillis = 0;
};

// The file's contents. The sentence can carry tool output, so the dump never
// throws on a byte that is not UTF-8 (ctl::DumpForWire).
inline std::string EncodeStopRecord(const StopRecord& record) {
  const nlohmann::json j = {{"stop_reason", record.stopReason},
                            {"error", record.error},
                            {"error_code", record.code},
                            {"written_millis", record.writtenMillis}};
  return ctl::DumpForWire(j);
}

// The record in `text` when it is whole, names a stop and was written within
// kStopRecordMaxAgeMillis of `nowMillis`; nullopt for anything else.
inline std::optional<StopRecord> DecodeStopRecord(const std::string& text, int64_t nowMillis) {
  const nlohmann::json j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (!j.is_object()) return std::nullopt;
  const auto stringAt = [&j](const char* key) -> std::optional<std::string> {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
  };
  const auto stopReason = stringAt("stop_reason");
  const auto error = stringAt("error");
  const auto code = stringAt("error_code");
  const auto written = j.find("written_millis");
  if (!stopReason || stopReason->empty() || !error || !code || written == j.end() ||
      !written->is_number_integer()) {
    return std::nullopt;
  }
  StopRecord record;
  record.stopReason = *stopReason;
  record.error = *error;
  record.code = *code;
  record.writtenMillis = written->get<int64_t>();
  const int64_t age = nowMillis - record.writtenMillis;
  if (age < 0 || age > kStopRecordMaxAgeMillis) return std::nullopt;
  return record;
}

}  // namespace urnw::selfrestart
