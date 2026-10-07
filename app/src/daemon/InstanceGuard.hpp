// One urnetworkd per machine: whether this invocation may touch the machine's
// tunnel state while another daemon may own it.
//
// The startup sweep, --revert and the unit's ExecStopPost all clean up by name
// (the nftables table, the policy rules, the capture route table, a takeover of
// /etc/resolv.conf), and none of them can tell a live tunnel's state from an
// orphan's. Run while another daemon serves a tunnel, each deletes that
// tunnel's routes, firewall and DNS while its app still says Connected; a
// second daemon then also takes the control socket over. So every one of them
// asks first whether another daemon is live, as Windows does.
//
// Two probes say so. The lock: the daemon holds an exclusive flock on
// kInstanceLockPath for its whole life, and the kernel drops it when the
// process dies, whatever kills it. The socket: a daemon that predates the lock
// still answers connect() on the control socket. A wedged daemon answers both,
// which is right: it still owns the state.
//
// Pure: the probes themselves run in daemon/main.cpp.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cerrno>

namespace urnw::instance {

// In the control socket's directory: tmpfs, so a reboot clears it with the
// armed marker, and systemd's RuntimeDirectory creates it before the start.
inline constexpr const char* kInstanceLockPath = "/run/urnetwork/urnetworkd.lock";

enum class Mode {
  // The daemon itself, before its startup sweep.
  Serve,
  // `urnetworkd --revert`, run by a person.
  Revert,
  // `urnetworkd --revert-unless-armed`, the unit's ExecStopPost. The unit's own
  // daemon has exited by then, so a live daemon is another one.
  RevertUnlessArmed,
};

// What the probes found of another daemon.
struct Probe {
  // Another process holds kInstanceLockPath.
  bool lockHeld = false;
  // Something accepts connections on the control socket.
  bool socketAnswers = false;
};

enum class Verdict {
  Proceed,
  // --revert --force while a daemon is live: sweep, and say what it costs.
  ProceedWithWarning,
  // Touch nothing. Serve and --revert exit non-zero; ExecStopPost exits 0, so
  // the stop it belongs to is not reported as failed.
  Refuse,
};

inline bool OtherDaemonLive(const Probe& probe) { return probe.lockHeld || probe.socketAnswers; }

// `force` is --revert --force, and only --revert reads it: a second daemon
// cannot serve beside the first, and ExecStopPost has no person to ask.
inline Verdict Decide(Mode mode, const Probe& probe, bool force) {
  if (!OtherDaemonLive(probe)) return Verdict::Proceed;
  switch (mode) {
    case Mode::Serve:
    case Mode::RevertUnlessArmed:
      return Verdict::Refuse;
    case Mode::Revert:
      return force ? Verdict::ProceedWithWarning : Verdict::Refuse;
  }
  return Verdict::Refuse;
}

// Reads a non-blocking connect() on the control socket: `result` is its return
// and `error` its errno. A listener answers even when its backlog is full
// (EAGAIN). No socket file, a socket nobody listens on (a daemon that died
// without unlinking it, which RuntimeDirectoryPreserve keeps) and any other
// error are no answer.
inline bool SocketAnswers(int result, int error) {
  if (result == 0) return true;
  return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS;
}

}  // namespace urnw::instance
