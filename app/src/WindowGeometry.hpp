// The main window's size across runs. GTK keeps a window's default size
// following its size while it is not maximized, so that, and whether it is
// maximized, is what a run saves when the window closes, when the app quits
// and shortly after the user last resized or maximized it (a session's logout
// ends the app with neither), and what the next run opens at. There is no
// position: Wayland lets no client place its own window, so the shell
// chooses, as it does for any app.
//
// A saved size the window could not have had (outside the minimum and any
// monitor) opens at the default instead. The preview harness neither restores
// nor saves: a review's size is not the user's.
//
// Header-only and free of GTK (tests/WindowGeometryTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

namespace urnw::window_geometry {

// Wide of the 1000dip breakpoint, so the brand art shows on a first launch
// (Windows' shell has the same default and minimum).
inline constexpr int kDefaultWidth = 1120;
inline constexpr int kDefaultHeight = 820;
inline constexpr int kMinWidth = 400;
inline constexpr int kMinHeight = 480;
// Beyond any monitor's logical size.
inline constexpr int kMaxSide = 16384;
// How long after the last resize the size is saved: once per gesture, not per
// frame of a drag (Windows' placement save waits as long).
inline constexpr int kSaveDebounceMillis = 700;

// app_prefs.json keys
inline constexpr const char* kWidthKey = "window_width";
inline constexpr const char* kHeightKey = "window_height";
inline constexpr const char* kMaximizedKey = "window_maximized";

struct Size {
  int width = kDefaultWidth;
  int height = kDefaultHeight;
};

// Whether the window could have had this size.
constexpr bool Plausible(int64_t width, int64_t height) {
  return width >= kMinWidth && height >= kMinHeight && width <= kMaxSide && height <= kMaxSide;
}

// The size to open at: the saved one when the window could have had it, else
// the default (a size never saved reads as 0 by 0).
constexpr Size SizeToOpenAt(int64_t savedWidth, int64_t savedHeight) {
  if (!Plausible(savedWidth, savedHeight)) return Size{};
  return Size{static_cast<int>(savedWidth), static_cast<int>(savedHeight)};
}

}  // namespace urnw::window_geometry
