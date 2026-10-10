// The signed-in shell's width rules (HomeShell::ApplyBreakpoint), taken on the
// window's width as MainWindow::ApplyPageBreakpoint fans it out to the pages.
//
// The nav rail is Windows' NavigationView with PaneDisplayMode Auto and
// CompactModeThresholdWidth 1: the expanded rail, icon and label, from
// 1008 dip, and the compact icon rail at every narrower width. There is no
// minimal mode that hides the rail behind a toggle: on Windows that toggle
// painted nothing under the custom title bar, and every destination was out
// of reach at the app's own 480 and 400 dip widths (a3fa3c9, on
// beta/custom-server).
//
// The status strip spans the window, and what it has no room for is dropped
// rather than left to ellipsize with everything else into "X…". Off Advanced
// Mode that is the Normal fields' captions (their values name themselves, and
// the captions stay their accessible names, so a screen reader loses nothing)
// and then traffic, which the Connect page shows as well. In Advanced Mode the
// Advanced row shows from Windows' 1000 dip breakpoint (8c68f68) and makes its
// room field by field: the Normal captions go first, then Raw, then RPC, then
// Network, Session and Routes at the breakpoint, and the Advanced tag, which
// says which reading the strip is in, stays down to the Normal captions'
// width. The Advanced fields keep their captions whenever they show: "none"
// and "Off" name nothing on their own. Traffic goes below Windows' 520 dip
// floor (8ccab5b).
//
// The widths are measured on Linux, at the strip's 12 px values and 11 px
// captions with the longest common reading, separators and the strip's
// padding included: "Connecting to providers", "Best available provider" and
// "↓ 1.2 Mbps  ↑ 340.9 Kbps" want 514 without captions and 640 with them; the
// Advanced tag 86 more; Network ("Guest"), Session ("tunnel") and Routes
// ("on, dns not applied"), captioned, 399; RPC ("127.0.0.1:12025") 138; Raw
// ("DESTINATION_SET") 201. A network name is the user's own, so a long one
// ellipsizes like any name.
//
// Pure C++17, free of GTK, so tests/ShellLayoutTest.cpp runs it without a
// display.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {
namespace shell {

// The rail item a page lights. A page reached FROM a destination has no item
// of its own and keeps that destination's lit: "referrals" (Refer and earn)
// and "sessions" (Account -> Sessions) belong to Account.
inline std::string RailTagFor(const std::string& pageTag) {
  if (pageTag == "referrals" || pageTag == "sessions") return "account";
  return pageTag;
}

// WinUI's default ExpandedModeThresholdWidth, not the pages' 1000 dip fold.
inline constexpr int kNavExpandedMinDip = 1008;

// The compact icon rail rather than the expanded one.
inline constexpr bool NavRailCompact(int windowWidthDip) {
  return windowWidthDip < kNavExpandedMinDip;
}

inline constexpr int kStripTrafficMinDip = 520;   // 514
inline constexpr int kStripCaptionsMinDip = 640;  // 640, and the Advanced tag's 600
// Advanced Mode
inline constexpr int kStripSessionFieldsMinDip = 1000;  // 999, Windows' breakpoint
inline constexpr int kStripRpcMinDip = 1140;            // 1137
inline constexpr int kStripRawMinDip = 1340;            // 1338
inline constexpr int kStripAdvancedCaptionsMinDip = 1470;  // 1464, the Normal captions

// What the status strip shows at a window width.
struct StatusStripLayout {
  bool captions = true;  // the Normal fields' captions
  bool traffic = true;
  // Advanced Mode only, each field with its caption: the row closed by its
  // Advanced tag, Network, Session and Routes, RPC, and Raw status
  bool advancedRow = false;
  bool sessionFields = false;
  bool rpc = false;
  bool raw = false;
};

inline constexpr StatusStripLayout StatusStripLayoutFor(int windowWidthDip, bool advanced) {
  StatusStripLayout layout;
  layout.advancedRow = advanced && windowWidthDip >= kStripCaptionsMinDip;
  layout.sessionFields = advanced && windowWidthDip >= kStripSessionFieldsMinDip;
  layout.rpc = advanced && windowWidthDip >= kStripRpcMinDip;
  layout.raw = advanced && windowWidthDip >= kStripRawMinDip;
  layout.captions =
      windowWidthDip >= (advanced ? kStripAdvancedCaptionsMinDip : kStripCaptionsMinDip);
  layout.traffic = windowWidthDip >= kStripTrafficMinDip;
  return layout;
}

}  // namespace shell
}  // namespace urnw
