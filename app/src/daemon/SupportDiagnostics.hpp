// SupportDiagnostics — urnetworkd's support diagnostics, written into the
// sdk's glog through urnet::logAppInfo, which is what "send feedback with logs"
// uploads (DeviceLocal.UploadLogs zips this process's glog files). DaemonLogf's
// journal and ring lines are not in that upload, so the facts a support reply
// needs are written here as well, at the moments they explain:
//
//   daemon start    the split tunnel's cgroup v2 hierarchy, the kill-switch
//                   floor and nft, the DNS host (tier, systemd-resolved's
//                   DNS-over-TLS and DNSSEC modes, the default-route link),
//                   NetworkManager's connectivity
//   network         the network country and where it came from (P052)
//   tunnel up       the bring-up's result, the DNS host again, the tunnel's
//                   link and the default-route link, connectivity
//   tunnel end      a failed start or a session that ended unasked: its code
//                   and stop reason, and connectivity after a failed start
//   kill switch     every change of the table in force (and what status says)
//   DNS override    lost and re-applied, or not; a systemd-resolved restart
//   path change     connectivity
//
// The lines themselves, and what they may carry, are DiagnosticLines.hpp:
// tokens, counts and a country, never an address, uid, path or id. The same
// line again under one key is written only after ten minutes, so a reconnect
// loop or a flapping link costs a line per change.
//
// Reads only. resolvectl's listing verbs are forked where systemd-resolved
// runs, at daemon start and after a tunnel's up edge, each bounded by
// timeout(1) where it exists; NetworkManager's Connectivity property is read
// asynchronously with G_DBUS_CALL_FLAGS_NO_AUTO_START. Callable from any
// thread (the bring-up runs on a worker); asynchronous replies are delivered on
// the main loop.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

#include "DiagnosticLines.hpp"
#include "NetworkCountry.hpp"
#include "Tunnel.hpp"

namespace urnw {
namespace support {

// The DNS tier's name in a line ("direct-file" for the /etc/resolv.conf tier).
const char* DnsBackendName(DnsBackend backend);

// At daemon start, before the control socket is served.
void LogDaemonStart(const std::string& tunnelInterface, bool armedFloorCarriedOver);

// A new network country reading (NetworkCountryWatcher's listener).
void LogNetworkCountry(const NetworkCountryReading& reading);

// After a tunnel's up edge. Forks resolvectl: call it without opMutex_ held.
void LogTunnelUp(const diag::TunnelUpFacts& facts, const std::string& tunnelInterface);

// A failed start ("start-failed") or a session that ended unasked ("stopped").
void LogTunnelEnded(const char* what, const std::string& reason, const std::string& code);

// The kill switch and the nftables table, after every install or removal.
void LogKillSwitch(const diag::KillSwitchFacts& facts);

// A DNS override event mid-session ("lost", "resolved-restarted").
void LogDnsOverride(const char* event, bool applied, DnsBackend backend);

// NetworkManager's connectivity, read asynchronously and written when it
// answers (or "absent" when it does not run).
void LogConnectivity(const char* moment);

}  // namespace support
}  // namespace urnw
