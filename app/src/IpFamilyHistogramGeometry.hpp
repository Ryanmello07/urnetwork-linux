// The ip-version histogram's pure logic, kept free of GTK and the SDK so it is
// unit-testable anywhere a C++17 compiler runs (tests/IpFamilyHistogramTest.cpp).
//
// The histogram (connect/IPV6.md D2) shows the connect window's ADDED
// providers as dots stacked under the address-family category the platform
// proved for each: "Both" (dualstack), "v4" (v4-only) or "v6" (v6-only). A
// provider still being evaluated, not added, or removed is not a dot here:
// the widget answers "what can my traffic use right now", and only an added
// exit can carry a flow.
//
// The dot is the connect canvas's cell. The canvas writes every metric in
// iOS's 256pt space and scales by side/256, and its provider dots are one grid
// cell wide (cell = side / cols), so 256 / cols is the same dot in that space.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace urnw::ipfamily {

// The SDK's provider grid point, reduced to the two fields this widget reads.
struct Point {
  std::string state;     // urnet::ProviderGridPoint::State ("Added", ...)
  std::string ipFamily;  // urnet::ProviderGridPoint::IpFamily ("dualstack", ...)
};

// The three rows, in display order.
enum class Row { Both = 0, V4 = 1, V6 = 2 };
inline constexpr int kRowCount = 3;

// The SDK's category values (sdk ip_family.go, connect ip_family.go).
inline constexpr const char* kFamilyDualstack = "dualstack";
inline constexpr const char* kFamilyV4Only = "v4-only";
inline constexpr const char* kFamilyV6Only = "v6-only";

// Which row a category lands in. Legacy (empty) and anything this build does
// not know read as v4: that is what such a provider carries (the SDK
// normalizes legacy to v4-only before it reaches an app, and this is the same
// rule applied once more at the edge).
inline Row RowFor(const std::string& ipFamily) {
  if (ipFamily == kFamilyDualstack) return Row::Both;
  if (ipFamily == kFamilyV6Only) return Row::V6;
  return Row::V4;
}

// The short label the SDK publishes for a category, from the row.
inline const char* RowLabelKey(Row row) {
  switch (row) {
    case Row::Both: return "both";
    case Row::V4: return "v4";
    case Row::V6: return "v6";
  }
  return "v4";
}

struct Rows {
  int counts[kRowCount] = {0, 0, 0};
  int total() const { return counts[0] + counts[1] + counts[2]; }
  int at(Row row) const { return counts[static_cast<int>(row)]; }
};

// One dot per ADDED provider, under its category's row.
inline Rows GroupPoints(const std::vector<Point>& points) {
  Rows rows;
  for (const auto& point : points) {
    if (point.state != "Added") continue;
    ++rows.counts[static_cast<int>(RowFor(point.ipFamily))];
  }
  return rows;
}

// The connect canvas's grid is square: cols = max(width, height). With no grid
// (no session yet) the default window's grid width stands in, so the dots are
// the size they will be rather than a size they never are.
inline constexpr int64_t kDefaultGridWidth = 16;
inline constexpr double kCanvasSpace = 256.0;

// The dot diameter in px: the canvas cell in the 256pt space, clamped so a
// pathological grid can neither vanish nor swamp the row.
inline int DotDiameter(int64_t gridWidth, int64_t gridHeight) {
  int64_t cols = std::max(gridWidth, gridHeight);
  if (cols <= 0) cols = kDefaultGridWidth;
  const double cell = kCanvasSpace / static_cast<double>(cols);
  return static_cast<int>(std::clamp(cell, 4.0, 32.0) + 0.5);
}

}  // namespace urnw::ipfamily
