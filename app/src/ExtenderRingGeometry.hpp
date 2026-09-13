// The extender rings drawn around a provider dot (connect/EXTENDER.md K2/K3),
// kept free of GTK and the SDK so it is unit-testable anywhere a C++17
// compiler runs (tests/ExtenderRingGeometryTest.cpp).
//
// A provider reached through one or more extenders is the same filled dot the
// connect canvas always drew, with one hollow ring per extender ip in that
// extender's color. The footprint NEVER grows: the outermost ring's outer
// edge sits exactly on the cell edge, so a dot with rings occupies the same
// square as a dot without them and can never bleed into a neighbour. What
// gives way is the fill — the dot shrinks inward by 4 px (2 px stroke + 2 px
// gap) for every ring.
//
//   cell edge  ─┐
//               │  ring 0  stroke 2, outer edge at the cell edge
//               │  gap 2
//               │  ring 1  stroke 2
//               │  gap 2
//               │  ring 2  stroke 2   (dashed when 4+ extenders collapse here)
//               │  gap 2
//               └  the filled dot, radius = cell/2 - 4 * rings
//
// RING ORDER. Ring 0 (the outermost, pinned to the cell edge) is the FIRST ip
// in the SDK's order and every further ip steps inward. That way the primary
// extender's ring keeps its radius as others come and go — an inward-first
// order would slide every existing ring outward on each new extender, which
// reads as motion that means nothing.
//
// COLLAPSE. At most three rings are drawn. Four or more extenders collapse the
// third ring into a DASHED ring in the third extender's color, so "several"
// stays visually distinct from "exactly three" without the dot disappearing
// under stroke.
//
// The color is the SDK's: GetExtenderColorHex(ip) computes it once (FNV-1a of
// the canonical ip, hue = hash % 360, 70% saturation, 55% lightness) and every
// app draws the value as given. Nothing here re-derives a color.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace urnw::extender {

// px, in the same space the caller's cell size is written in
inline constexpr double kRingStroke = 2.0;
inline constexpr double kRingGap = 2.0;
// stroke + gap: what one ring costs the filled dot's radius
inline constexpr double kRingPitch = kRingStroke + kRingGap;
// four or more extenders collapse into the third (dashed) ring
inline constexpr int kMaxRings = 3;
// the dot must survive its rings: below this radius a ring is dropped instead
inline constexpr double kMinDotRadius = 1.0;
// the dash pattern of the collapsed ring, in px along the circumference
inline constexpr double kCollapsedDashOn = 3.0;
inline constexpr double kCollapsedDashOff = 3.0;

struct Ring {
  double radius = 0;      // CENTERLINE radius in px, at full (unanimated) size
  double lineWidth = 0;   // stroke width in px
  bool dashed = false;    // the collapsed "4 or more" ring
  std::string colorHex;   // the SDK's color for this extender; "" when unknown
};

struct Rings {
  // The filled dot's radius after the rings have eaten into it. Always > 0.
  double dotRadius = 0;
  // Outermost first (see RING ORDER above).
  std::vector<Ring> rings;
  // Extenders folded into the dashed ring beyond the one it is drawn for:
  // 0 unless four or more extenders were given AND three rings fit.
  int collapsedCount = 0;

  bool empty() const { return rings.empty(); }
};

// Splits one of the SDK's comma-separated grid-point fields
// (ProviderGridPoint::ExtenderIps / ExtenderColorHexes). Blank entries are
// dropped, so an empty field is an empty list rather than one empty string —
// a direct route must read as "no extenders", never as "one nameless one".
inline std::vector<std::string> SplitList(const std::string& value) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = value.find(',', start);
    const size_t end = comma == std::string::npos ? value.size() : comma;
    // trim ascii whitespace on both sides
    size_t a = start, b = end;
    while (a < b && (value[a] == ' ' || value[a] == '\t')) ++a;
    while (b > a && (value[b - 1] == ' ' || value[b - 1] == '\t')) --b;
    if (a < b) out.emplace_back(value, a, b - a);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

// Pairs the two grid-point fields, which the SDK emits in the SAME order. The
// result is one entry per extender IP — the ips are the truth about how many
// rings there are — carrying that ip's color, or "" when the color list is
// short (an older SDK, or a point built before the colors were filled in).
// A caller draws a "" ring in its own neutral color rather than dropping it:
// a missing color must not hide a live extender.
inline std::vector<std::string> PairColors(const std::string& ipsCsv,
                                           const std::string& colorHexesCsv) {
  const std::vector<std::string> ips = SplitList(ipsCsv);
  const std::vector<std::string> colors = SplitList(colorHexesCsv);
  std::vector<std::string> out;
  out.reserve(ips.size());
  for (size_t i = 0; i < ips.size(); ++i) {
    out.push_back(i < colors.size() ? colors[i] : std::string());
  }
  return out;
}

// How many rings actually fit in `cellSize` while leaving the dot a visible
// core. Never more than kMaxRings and never negative.
inline int RingCapacity(double cellSize) {
  if (!(cellSize > 0)) return 0;
  const double room = cellSize / 2.0 - kMinDotRadius;
  if (room < kRingPitch) return 0;
  const int fits = static_cast<int>(room / kRingPitch);
  return std::min(fits, kMaxRings);
}

// The rings for one provider dot. `cellSize` is the dot's full diameter (the
// grid cell); `colorHexes` is PairColors' output, in the SDK's ip order.
//
// The returned radii are at FULL size: an animating dot multiplies both
// dotRadius and every ring radius by its own grow-in scale, so the rings
// arrive and leave with the dot rather than popping in around it.
inline Rings RingsFor(double cellSize, const std::vector<std::string>& colorHexes) {
  Rings out;
  if (!(cellSize > 0)) return out;
  out.dotRadius = cellSize / 2.0;
  if (colorHexes.empty()) return out;

  const int capacity = RingCapacity(cellSize);
  if (capacity <= 0) return out;  // no room: the bare dot, unshrunk

  const int wanted = static_cast<int>(colorHexes.size());
  const int drawn = std::min(wanted, capacity);
  // Only a FULL three-ring set collapses. With room for one or two rings the
  // third extender is simply not drawn — dashing a second ring would claim a
  // count the geometry never had space to show.
  const bool collapse = wanted > drawn && drawn == kMaxRings;

  out.rings.reserve(static_cast<size_t>(drawn));
  for (int i = 0; i < drawn; ++i) {
    Ring ring;
    // outer edge of ring i = cell/2 - i*pitch; centerline is half a stroke in
    ring.radius = cellSize / 2.0 - static_cast<double>(i) * kRingPitch - kRingStroke / 2.0;
    ring.lineWidth = kRingStroke;
    ring.dashed = collapse && i == kMaxRings - 1;
    ring.colorHex = colorHexes[static_cast<size_t>(i)];
    out.rings.push_back(std::move(ring));
  }
  out.collapsedCount = collapse ? wanted - kMaxRings : 0;
  out.dotRadius = cellSize / 2.0 - static_cast<double>(drawn) * kRingPitch;
  return out;
}

}  // namespace urnw::extender
