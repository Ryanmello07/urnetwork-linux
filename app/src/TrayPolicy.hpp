// Never leave the app running with no way back to it. Closing the window hides
// it to the tray, and an autostart launch shows only the tray, but a tray icon
// needs a StatusNotifierWatcher on the session bus to take it: stock GNOME has
// none without the AppIndicator extension, and a desktop's tray host can stop
// or restart while the app runs. With no tray a hidden window is a running app,
// tunnel and all, with nothing on screen.
//
// So the window hides only while a watcher has taken the tray icon
// (Tray::Available). Without one, closing minimizes the window, and a window
// that is hidden when there is no tray, at an autostart launch or when the
// tray goes away, shows again minimized once the tray has had
// kNoTrayGraceMillis to come or come back: a desktop's tray starts with the
// session, often after the autostart entries, and comes back a few seconds
// after a restart.
//
// Header-only and free of GTK (tests/TrayPolicyTest.cpp); main.cpp applies it.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw::tray_policy {

inline constexpr int kNoTrayGraceMillis = 5000;

enum class CloseAction {
  Hide,      // to the tray, which brings it back
  Minimize,  // no tray: the window stays in the shell's window list
};

constexpr CloseAction OnClose(bool trayAvailable) {
  return trayAvailable ? CloseAction::Hide : CloseAction::Minimize;
}

// Whether the window has to show (minimized): it is hidden and no tray can
// bring it back. Asked once the grace has passed, and again when it ends.
constexpr bool MustSurface(bool windowVisible, bool trayAvailable) {
  return !windowVisible && !trayAvailable;
}

}  // namespace urnw::tray_policy
