// The signed-in shell's width rules (HomeShell), taken on the window's width
// as MainWindow::ApplyPageBreakpoint fans it out to the pages.
//
// The nav rail is Windows' NavigationView with PaneDisplayMode Auto and
// CompactModeThresholdWidth 1: the expanded rail, icon and label, from
// 1008 dip, and the compact icon rail at every narrower width. There is no
// minimal mode that hides the rail behind a toggle: on Windows that toggle
// painted nothing under the custom title bar, and every destination was out
// of reach at the app's own 480 and 400 dip widths (a3fa3c9, on
// beta/custom-server).
//
// Pure C++17, free of GTK, so tests/ShellLayoutTest.cpp runs it without a
// display.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw {
namespace shell {

// WinUI's default ExpandedModeThresholdWidth, not the pages' 1000 dip fold.
inline constexpr int kNavExpandedMinDip = 1008;

// The compact icon rail rather than the expanded one.
inline constexpr bool NavRailCompact(int windowWidthDip) {
  return windowWidthDip < kNavExpandedMinDip;
}

}  // namespace shell
}  // namespace urnw
