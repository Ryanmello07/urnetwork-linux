// How many of the Settings destination's three panes show at a window width
// (docs/parity/settings.md §1.1, windows MainWindow::ApplyBreakpoint), and so
// which of pane A's second doors show. About folds first, below 1400dip, and
// Device below 900dip, and a foldable pane owns no content without a second
// door: Windows' fold rule. So pane A carries a copy of the About pane (its
// version rows, Licenses and Stay in touch) while About is folded, and of the
// Advanced mode toggle while Device is: without them the default 1120dip
// window shows no version anywhere, nor the DePIN Hub and protocol links, and
// below 900dip Advanced mode cannot be turned on. Exactly one copy of each
// shows at any width.
//
// No GTK: SettingsPage::ApplyBreakpoint asks it, and tests/SettingsFoldTest.cpp
// checks the table and its doors.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw::settings_fold {

inline constexpr int kThreePaneDip = 1400;
inline constexpr int kTwoPaneDip = 900;

// 1, 2 or 3 panes.
inline constexpr int PaneCount(int widthDip) {
  if (kThreePaneDip <= widthDip) return 3;
  if (kTwoPaneDip <= widthDip) return 2;
  return 1;
}

// The About pane is folded: pane A shows its version rows, Licenses and
// Stay in touch.
inline constexpr bool AboutDoorsShown(int paneCount) { return paneCount < 3; }

// The Device pane is folded: pane A shows the Advanced mode toggle.
inline constexpr bool DeviceDoorsShown(int paneCount) { return paneCount < 2; }

}  // namespace urnw::settings_fold
