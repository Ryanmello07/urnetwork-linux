// The provide indicator (apple/android parity), from the live effective provide
// mode: "●" solid dot = Network tier (also Auto while idle), "◉" dot with an
// outer ring = Public tier (amber while paused — pause stops public only),
// neutral muted = not providing. Not providing is a setting the user chose,
// not an error, so it spends no coral: red is kept for danger (windows
// 572c876). ProvideMode is a bit set (0 none, 1 network, 2
// friends-and-family, 3 public) — per-case only. One rule shared by the
// connect page's provide group and the earnings page's provide-mode row, so
// they never disagree.
//
// Header-only and free of GTK so the unit tests can build it
// (tests/ProvideModeGlyphTest.cpp); the colors are Ui.hpp's kUrGreen,
// kUrAmber and kUrTextMuted, as markup hex, and the test reads them from
// Ui.hpp so the two cannot drift.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

struct ProvideModeGlyph {
  const char* glyph;
  const char* colorHex;
};

inline ProvideModeGlyph ProvideModeGlyphFor(int64_t provideMode, bool paused) {
  switch (provideMode) {
    case 3:  // public
      return {"◉", paused ? "#F5C242" : "#87FB67"};
    case 1:  // network (also Auto while idle)
    case 2:  // friends-and-family
      return {"●", "#87FB67"};
    default:
      return {"●", "#989898"};
  }
}
