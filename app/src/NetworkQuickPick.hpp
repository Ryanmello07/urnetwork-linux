// The Network page's quick-pick (NetworkPage::RenderDetail): while Best
// available provider is selected, the selected provider pane offers the
// first countries of the list as the list's own rows, so a location is one
// click away without scrolling pane A. The countries come in the SDK's
// order, unsorted.
//
// Header-only and free of GTK so the unit tests can build it
// (tests/NetworkQuickPickTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>

namespace urnw {

// At most this many countries ride the pane, as on Windows.
inline constexpr size_t kQuickPickCountries = 5;

// How many of `countries` the quick-pick shows; none hides the group.
inline size_t QuickPickCount(size_t countries) {
  return countries < kQuickPickCountries ? countries : kQuickPickCountries;
}

}  // namespace urnw
