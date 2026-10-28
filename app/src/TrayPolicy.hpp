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
// The first time closing hides the window to the tray, a desktop notification
// says the app is still running there (MainWindow::NoteHiddenToTray), once
// ever: the close is the moment the user asks where it went. Quit never
// reaches it, and neither does a close with no tray.
//
// Header-only and free of GTK (tests/TrayPolicyTest.cpp); main.cpp applies it.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw::tray_policy {

inline constexpr int kNoTrayGraceMillis = 5000;

// The app_prefs.json key that records the hide notice as shown, set before it
// is sent so a crash cannot show it twice (Windows' key for its balloon).
inline constexpr const char* kHideNoticeSeenKey = "onb_tray_balloon_seen";

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

// The menu's Connect/Disconnect item (apple e46555b7). With no status there is
// no connection to end and none to start: signed out, a Connect started
// nothing but a "not signed in" refusal, and a Disconnect had nothing to
// stop. So the item is offered only with a status (signed in), and then its
// word follows the session, as the window's action does.
enum class ConnectItem {
  None,
  Connect,
  Disconnect,
};

constexpr ConnectItem ConnectItemFor(bool hasStatus, bool sessionUp) {
  if (!hasStatus) return ConnectItem::None;
  return sessionUp ? ConnectItem::Disconnect : ConnectItem::Connect;
}

// The icon (a bare name the host resolves under IconThemePath). While the
// connection is slow it is the slow-internet tunnel at that level, 1 to 4
// (slow_internet::Level, apple/DELAY.md §2.1 rendered at 32 px), the way the
// Connect page's pill shows it; otherwise connected means a provider is
// proven.
inline std::string IconNameFor(bool proven, int slowLevel) {
  if (0 < slowLevel) return "urnetwork-tray-slow-" + std::to_string(slowLevel < 4 ? slowLevel : 4);
  return proven ? "urnetwork-tray-connected" : "urnetwork-tray-disconnected";
}

// The tooltip's text: the state's words, then the slow line when there is one
// ("Connected · Slow internet").
inline std::string ToolTipFor(const std::string& status, const std::string& slow) {
  if (slow.empty()) return status;
  if (status.empty()) return slow;
  return status + " · " + slow;
}

}  // namespace urnw::tray_policy
