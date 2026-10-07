// System-tray icon via the StatusNotifierItem (SNI) spec, spoken directly over
// D-Bus with GDBus. We deliberately avoid libappindicator/ayatana because those
// pull GTK3 in-process for their menu — this app is GTK4-only, so we serve the
// SNI object and its com.canonical.dbusmenu ourselves. Falls back gracefully
// (no tray) on desktops without a StatusNotifierWatcher.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>

#include <gio/gio.h>

namespace urnw {

class Tray {
 public:
  Tray();
  ~Tray();

  Tray(const Tray&) = delete;
  Tray& operator=(const Tray&) = delete;

  // The connection as the window reads it: the Connect/Disconnect item
  // follows the session (`sessionUp`, so the label and a press agree), the
  // connected icon means a provider is proven to carry it, and the tooltip
  // names the state in the status row's words.
  void SetState(bool sessionUp, bool proven, const std::string& status);

  std::function<void()> on_activate;         // left-click the tray icon
  std::function<void()> on_toggle_connect;   // menu: Connect/Disconnect
  std::function<void()> on_show;             // menu: Show window
  std::function<void()> on_quit;             // menu: Quit

  // Read by the D-Bus vtable callbacks (free functions in the .cpp).
  bool sessionUp() const { return session_up_; }
  bool provenForIcon() const { return proven_; }
  const std::string& statusForToolTip() const { return status_; }
  guint menuRevision() const { return menu_revision_; }

 private:
  void OnBusAcquired(GDBusConnection* conn);
  void RegisterWithWatcher();
  // Fallback when the well-known name cannot be owned (a Flatpak sandbox):
  // register with the watcher under the connection's UNIQUE name.
  void OnNameLost(GDBusConnection* conn);
  void EmitMenuLayoutUpdated();

  GDBusConnection* conn_ = nullptr;
  guint owner_id_ = 0;
  guint sni_reg_ = 0;
  guint menu_reg_ = 0;
  guint menu_revision_ = 1;
  std::string service_name_;  // org.kde.StatusNotifierItem-<pid>-1
  bool session_up_ = false;
  bool proven_ = false;
  std::string status_;

  static const GDBusInterfaceVTable kSniVtable;
  static const GDBusInterfaceVTable kMenuVtable;
};

}  // namespace urnw
