// What signing out of URnetwork asks of the system service (urnetworkd), in
// what order, and what keeps the request standing until the daemon has done it
// (owner decision, 2026-10-05: "Windows sign-out: stop the tunnel and
// provider, the same as Quit"; the Linux app does the same).
//
// The request. Quit's (SdkHost::Shutdown): stop_tunnel, which ends this
// user's session, retires the provider-only device with it (TunnelHost::Stop)
// and lifts the kill-switch floor, as every explicit stop does. Another user's
// session is theirs: a status redacted for this uid says nothing of this
// user's runs, and stopping it would ask for an administrator's password
// (kActionTakeOverTunnel), so a delivery leaves it alone.
//
// The obligation. A daemon that was not told still runs what it ran under the
// signed-out account. So a sign-out is recorded as owed before the request
// goes out, in a marker that outlives the app, and it is cleared only once the
// daemon has done it. While it is owed:
//   * the sign-out has still completed in the app: its credentials and its
//     remembered rpc session are gone (SdkHost::Logout);
//   * the provider reconcile delivers it before anything else, and the health
//     poll runs that reconcile every few seconds, signed in or not, paced by
//     the provider step's backoff (MainWindow::PollDaemonHealth); a Connect
//     delivers it first too, and neither a provider nor a tunnel starts while
//     it is owed;
//   * the next launch reads the marker, and its first poll delivers it.
// The daemon starts nothing by itself after a restart or a reboot (it starts
// only on a request), so the old account's work can come back only through a
// start from this app, and every start waits for the obligation.
//
// Pure, header-only, C++17 and free of glib and the sdk: tests/SignOutTest.cpp
// runs it against a fake daemon and marker, and SdkHost binds the real ones.
// Not safe for concurrent use: SdkHost calls it under mutex_.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <utility>

namespace urnw::signout {

enum class Request { StopTunnel };

// What every delivery sends, in this order: Quit's request.
inline constexpr std::array<Request, 1> kRequests{Request::StopTunnel};

// The wire names (ControlProtocol.hpp), for logs.
constexpr const char* ToString(Request request) {
  switch (request) {
    case Request::StopTunnel: return "stop_tunnel";
  }
  return "unknown";
}

enum class Delivery {
  // Nothing is owed: the daemon did it, now or before, or runs nothing of
  // this user's.
  Delivered,
  // The control socket could not be reached, or the hello failed, so nothing
  // was sent.
  Unreachable,
  // The daemon was reached and did not do it: no status, or a request
  // refused (a polkit denial, a tunnel another client controls).
  Refused,
};

// For logs.
constexpr const char* ToString(Delivery delivery) {
  switch (delivery) {
    case Delivery::Delivered: return "delivered";
    case Delivery::Unreachable: return "the daemon could not be reached";
    case Delivery::Refused: return "the daemon did not do it";
  }
  return "unknown";
}

// The daemon as a delivery sees it.
struct Daemon {
  // Connect and hello; true when the session is usable afterwards.
  std::function<bool()> reach;
  // Whether what the daemon runs belongs to another user: its status came back
  // redacted for this uid. nullopt when no status came back.
  std::function<std::optional<bool>()> otherUsersSession;
  // Send one request; true when the daemon answered that it did it.
  std::function<bool(Request)> send;
};

// Where an owed sign-out outlives the app: a relaunch, a reboot. Production
// keeps a file beside the app's preferences (SdkHost::SignOutMarker).
struct Marker {
  std::function<bool()> read;
  std::function<void(bool owed)> write;
};

// One owed sign-out, or none. Begin records it, Settle delivers it.
class Obligation {
 public:
  explicit Obligation(Marker marker) : marker_(std::move(marker)) {}

  // Read what an earlier run left. Once, before the first Settle or Owed.
  void Load() { owed_.store(marker_.read()); }

  // The user signed out: owed from here on, written down before anything is
  // sent, then delivered when the daemon can be told now.
  Delivery Begin(const Daemon& daemon) {
    owed_.store(true);
    marker_.write(true);
    return Settle(daemon);
  }

  // Deliver an owed sign-out. Delivered at once when nothing is owed, and the
  // marker is cleared only once the daemon has done it or runs nothing of this
  // user's.
  Delivery Settle(const Daemon& daemon) {
    if (!owed_.load()) return Delivery::Delivered;
    if (!daemon.reach()) return Delivery::Unreachable;
    const std::optional<bool> otherUsers = daemon.otherUsersSession();
    if (!otherUsers) return Delivery::Refused;
    if (!*otherUsers) {
      bool done = true;
      for (Request request : kRequests) {
        if (!daemon.send(request)) done = false;
      }
      if (!done) return Delivery::Refused;
    }
    owed_.store(false);
    marker_.write(false);
    return Delivery::Delivered;
  }

  // A start may go out only while this is false.
  bool Owed() const { return owed_.load(); }

 private:
  Marker marker_;
  std::atomic<bool> owed_{false};
};

}  // namespace urnw::signout
