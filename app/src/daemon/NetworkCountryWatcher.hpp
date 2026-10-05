// NetworkCountryWatcher — the daemon's reader of the network country (P052,
// NetworkCountry.hpp): which interface the default route leaves through, and,
// while ModemManager runs, which interfaces its modems carry data on and which
// operator each is registered to.
//
// WHY THE DAEMON READS IT, AND NOT THE GUI. The value is process-wide in the
// sdk and has to be in force where the devices dial: the daemon builds both
// DeviceLocals (the tunnel's and the provider-only one) and keeps them running
// after the GUI quits, across network changes nobody in the GUI would see.
// ModemManager is on the system bus the daemon already talks to, while a
// Flatpak GUI cannot reach it at all. The daemon publishes what it applied in
// `status` (network_country_code), and the GUI applies the same value to its
// own sign-in and api dials.
//
// MAIN LOOP ONLY, and it never blocks the loop: every ModemManager call is
// asynchronous, carries G_DBUS_CALL_FLAGS_NO_AUTO_START (an installed but
// disabled ModemManager is never started by being asked something), and is
// made only while its bus name has an owner. The default route is two /proc
// reads on a 5 s tick; the modems are read again when it changes, when
// ModemManager appears, and once a minute. The listener runs on the main loop
// whenever the reading changes, starting with the first complete one.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gio/gio.h>

#include "NetworkCountry.hpp"

namespace urnw {

class NetworkCountryWatcher {
 public:
  using Listener = std::function<void(const NetworkCountryReading&)>;

  // excludedInterface: the tun, which is never the network this machine is on.
  NetworkCountryWatcher(std::string excludedInterface, Listener listener);
  ~NetworkCountryWatcher();

  NetworkCountryWatcher(const NetworkCountryWatcher&) = delete;
  NetworkCountryWatcher& operator=(const NetworkCountryWatcher&) = delete;

  // Watches ModemManager's bus name and starts the tick. The first reading
  // arrives once the loop runs.
  void Start();

 private:
  // What every async callback carries: a box that reads null once the watcher
  // is gone, the read it belongs to, and the modem a bearer reply is for.
  struct CallCtx {
    std::shared_ptr<NetworkCountryWatcher*> box;
    uint64_t generation = 0;
    size_t modem = 0;
  };

  static void OnModemManagerAppeared(GDBusConnection* connection, const gchar* name,
                                     const gchar* nameOwner, gpointer data);
  static void OnModemManagerVanished(GDBusConnection* connection, const gchar* name,
                                     gpointer data);
  static gboolean OnTick(gpointer data);
  static void OnManagedObjects(GObject* source, GAsyncResult* result, gpointer data);
  static void OnBearerProperties(GObject* source, GAsyncResult* result, gpointer data);

  // Re-reads the default route; true when its interface changed.
  bool ReadDefaultRoute();
  // Asks ModemManager for its modems (and their bearers); one read at a time.
  void ReadModems();
  void FinishRead(bool answered);
  // Delivers the reading when it changed.
  void Decide();

  std::string excludedInterface_;
  Listener listener_;
  std::shared_ptr<NetworkCountryWatcher*> box_;
  GDBusConnection* bus_ = nullptr;
  GCancellable* cancellable_ = nullptr;
  guint watchId_ = 0;
  guint tickId_ = 0;

  std::string defaultRouteInterface_;
  bool modemManagerRunning_ = false;
  bool modemManagerAnswered_ = false;
  std::vector<ModemReading> modems_;

  // The read in flight: its generation (bumped when ModemManager vanishes, so
  // a late reply is dropped), the modems it found, and the bearer replies it
  // still waits for.
  uint64_t readGeneration_ = 0;
  bool readInFlight_ = false;
  bool readAgain_ = false;
  int64_t lastReadMonotonicSeconds_ = 0;
  std::vector<ModemReading> pendingModems_;
  int pendingBearers_ = 0;
  // The failure the journal last heard about, so a ModemManager that keeps
  // refusing is not a line a minute.
  std::string lastReadError_;

  NetworkCountryReading reading_;
  bool delivered_ = false;
};

}  // namespace urnw
