// What the status strip's Advanced Mode fields say (HomeShell), as Windows'
// strip has them: which kind of session there is, whether the daemon's routes
// are in force and why not, and the device rpc's endpoint. They are what is
// asked when the Normal fields look fine and nothing is carried.
//
// None of them may claim more than the daemon said. No session reads "none"
// for the session and the endpoint, never the last session's; routes the
// daemon has not reported yet for a session read "none", and with no session
// and no report "off". Routes on with DNS not applied is a degraded tunnel,
// not a working one, and routes off while the kill switch's floor is armed is
// a machine blocked on purpose, which a bare "off" would hide.
//
// Pure C++17, free of GTK and the SDK, so tests/StatusStripPresentationTest.cpp
// runs it without a display. MainWindow maps each word to its store key.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <optional>
#include <string>

namespace urnw {
namespace status_strip {

// The Session field.
enum class SessionWord { None, Tunnel };

// `haveSession`: a DeviceRemote is bound over the current control session.
inline SessionWord SessionWordFor(bool haveSession) {
  return haveSession ? SessionWord::Tunnel : SessionWord::None;
}

// The daemon's facts the Routes field reads, from its last status reply
// (ctl::StatusReply routes_installed, dns_applied and kill_switch).
struct RouteFacts {
  bool routesInstalled = false;
  bool dnsApplied = false;
  bool killSwitchArmed = false;  // the floor is in force with no tunnel
};

// The Routes field.
enum class RoutesWord { Unknown, Off, KillSwitchArmed, DnsNotApplied, On };

inline RoutesWord RoutesWordFor(bool haveSession, const std::optional<RouteFacts>& facts) {
  if (!facts) return haveSession ? RoutesWord::Unknown : RoutesWord::Off;
  if (!facts->routesInstalled) {
    return facts->killSwitchArmed ? RoutesWord::KillSwitchArmed : RoutesWord::Off;
  }
  return facts->dnsApplied ? RoutesWord::On : RoutesWord::DnsNotApplied;
}

// The RPC field: the endpoint this session dialed, or "" (none) with no
// session, so a stale host:port never stands under "Session none".
inline std::string RpcText(bool haveSession, const std::string& hostPort) {
  return haveSession ? hostPort : std::string();
}

}  // namespace status_strip
}  // namespace urnw
