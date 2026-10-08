// How many of the connect page's three panes show at a given pane-grid width
// (docs/parity/connect-page.md §0), and so whether pane A carries the second
// doors to the statistics pane's sheets. Simple Mode is always the one centred
// pane. Advanced Mode adds the activity pane from 640dip, and the statistics
// pane only once the activity pane still keeps kActivityFloorDip beside the
// two fixed rails and their rules, so opening the third pane never squeezes
// the activity list below a rail's width.
//
// The width is the one the panes share, the page's own allocation, never the
// window's: the page sits behind the shell's 220dip nav rail (ApplyFold).
// No GTK: ConnectPage::ApplyFold asks it, and tests/ConnectFoldTest.cpp
// checks the table and its second doors.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw::connect_fold {

inline constexpr int kPaneAWidth = 330;  // the connect rail
inline constexpr int kPaneCWidth = 380;  // the statistics rail
inline constexpr int kRuleWidth = 1;     // the rule between two panes
// the least the activity pane keeps once the statistics pane opens
inline constexpr int kActivityFloorDip = 330;
inline constexpr int kTwoPaneDip = 640;
inline constexpr int kThreePaneDip =
    kPaneAWidth + kRuleWidth + kActivityFloorDip + kRuleWidth + kPaneCWidth;
static_assert(kThreePaneDip == 1042, "the three-pane gate is Windows' 1042dip");

// 1, 2 or 3 panes.
inline constexpr int PaneCount(bool advanced, int paneWidth) {
  if (!advanced) return 1;
  if (kThreePaneDip <= paneWidth) return 3;
  if (kTwoPaneDip <= paneWidth) return 2;
  return 1;
}

// The statistics pane holds the only doors to the client contracts, split
// rules, custom DNS and provider locations sheets, and pane B the transport
// settings'. While the statistics pane is folded (Simple Mode, or under the
// three-pane gate), pane A carries a second set, so no sheet is out of reach
// at any width: Windows' fold rule, a foldable pane owns no content without
// a second door. Exactly one set shows.
inline constexpr bool FoldDoorsShown(int paneCount) { return paneCount < 3; }

}  // namespace urnw::connect_fold
