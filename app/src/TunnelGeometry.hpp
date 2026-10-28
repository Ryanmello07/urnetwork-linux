// The slow-internet tunnel (apple/DELAY.md §2-§3, the same on every
// platform): nested square outlines, one more for each step the transport's
// quality worsens, and the staggered fade that adds or removes them. The
// squares never move: a change only fades squares in or out in place.
//
// Geometry, relative to the box side S:
//   - the outer square's stroke is 0.094 S and the square is inset by half of
//     it, so its outer edge fits the box exactly;
//   - square i is the outer square (its stroke's centre line) scaled by
//     0.70^i about the vanishing point (0.55 S, 0.55 S). The point sits past
//     the centre, so the gaps on the left and top open about 1.65 times wider
//     than on the right and bottom, which is the tunnel;
//   - its stroke is the outer one times 0.60^i, its opacity 1 - 0.08 i;
//   - square corners with mitered joins: rounded ones soften the tunnel.
//
// Depth (squares) by quantized quality: 0 -> 0, .25 -> 1, .5 -> 2, .75 -> 4,
// 1 -> 6. Between the table's steps (a quality step other than 0.25) it is
// interpolated and rounded, and any quality above 0 shows one square at least.
//
// Header-only and free of GTK: TunnelIndicator.cpp draws it and runs the
// fades, tests/SlowInternetTest.cpp checks it, and the tray's
// urnetwork-tray-slow-<level>.png were rendered from it at 32 px.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace urnw::tunnel {

inline constexpr int kMaxDepth = 6;
inline constexpr double kOuterStroke = 0.094;    // of the side
inline constexpr double kStrokeFalloff = 0.60;   // each nested stroke
inline constexpr double kSquareFalloff = 0.70;   // each nested square
inline constexpr double kVanishingPoint = 0.55;  // of the side, on both axes
inline constexpr double kOpacityStep = 0.08;     // per depth index

// The depth at quality 0, .25, .5, .75 and 1.
inline constexpr double kDepthTable[] = {0, 1, 2, 4, 6};
// The SDK quantizes to 0.25 steps; a quality at or below this reads as 0.
inline constexpr double kQualityEpsilon = 1e-6;

// The squares shown for a quality (the worse direction's), 0 to kMaxDepth.
inline int DepthForQuality(double quality) {
  if (!(quality > kQualityEpsilon)) return 0;  // NaN too
  const double steps = std::min(quality, 1.0) * 4.0;
  const int lower = std::min(3, static_cast<int>(std::floor(steps)));
  const double depth =
      kDepthTable[lower] + (kDepthTable[lower + 1] - kDepthTable[lower]) * (steps - lower);
  return std::clamp(static_cast<int>(std::lround(depth)), 1, kMaxDepth);
}

// The quality as one of four levels, for the tray's art
// (urnetwork-tray-slow-<level>): 1 to 4, or 0 for none.
inline int LevelForQuality(double quality) {
  if (!(quality > kQualityEpsilon)) return 0;
  return std::clamp(static_cast<int>(std::lround(std::min(quality, 1.0) * 4.0)), 1, 4);
}

// The level's depth, as the tray's art draws it.
inline int DepthForLevel(int level) {
  return level <= 0 ? 0 : DepthForQuality(std::min(level, 4) / 4.0);
}

// One square: its stroke's centre line in the box's coordinates (the square
// sits on the box's diagonal, so x and y are the same), its stroke width and
// its opacity.
struct Square {
  double x = 0;
  double y = 0;
  double side = 0;
  double stroke = 0;
  double opacity = 1;
};

inline double VanishingPoint(double size) { return kVanishingPoint * size; }

// Square `index` (0 the outer one) in a box of side `size`.
inline Square SquareAt(int index, double size) {
  const double outerStroke = kOuterStroke * size;
  const double inset = outerStroke / 2;
  const double vanish = VanishingPoint(size);
  const double scale = std::pow(kSquareFalloff, index);
  Square square;
  square.x = vanish + (inset - vanish) * scale;
  square.y = square.x;
  square.side = (size - outerStroke) * scale;
  square.stroke = outerStroke * std::pow(kStrokeFalloff, index);
  square.opacity = 1.0 - kOpacityStep * index;
  return square;
}

// ---- motion (apple/DELAY.md §3) ---------------------------------------------
// Deeper: each new square fades in, outer to inner, 420 ms with an ease-out,
// started 110 ms apart, settling from 1.06x about the vanishing point, so it
// seems to recede into the tunnel. Shallower: inner to outer, 320 ms, 90 ms
// apart, easing back toward 1.06x. A jump over levels runs the same stagger
// for every square that changes. The pill around the tunnel appears with the
// first square and, going to 0, waits until the last square starts fading.
inline constexpr int kFadeInMs = 420;
inline constexpr int kFadeInStaggerMs = 110;
inline constexpr int kFadeOutMs = 320;
inline constexpr int kFadeOutStaggerMs = 90;
inline constexpr double kEntryScale = 1.06;

// One square's fade in a change.
struct Step {
  int index = 0;
  bool in = false;  // fades in (deeper) or out (shallower)
  int delayMs = 0;
  int durationMs = 0;
};

// The squares a change from `from` squares to `to` fades, in the order they
// start. Empty when the depth does not change.
inline std::vector<Step> Plan(int from, int to) {
  from = std::clamp(from, 0, kMaxDepth);
  to = std::clamp(to, 0, kMaxDepth);
  std::vector<Step> steps;
  if (from < to) {
    for (int i = from; i < to; ++i) {
      steps.push_back(Step{i, true, (i - from) * kFadeInStaggerMs, kFadeInMs});
    }
  } else {
    for (int i = from - 1; i >= to; --i) {
      steps.push_back(Step{i, false, (from - 1 - i) * kFadeOutStaggerMs, kFadeOutMs});
    }
  }
  return steps;
}

// How long a change runs, its last square's fade included: 0 -> 6 builds the
// tunnel in 970 ms, and 6 -> 0 clears it in 770 ms.
inline int PlanMs(int from, int to) {
  int total = 0;
  for (const Step& step : Plan(from, to)) total = std::max(total, step.delayMs + step.durationMs);
  return total;
}

// Going from `from` squares to none, the pill fades once the last square has
// started fading: (from - 1) staggers.
inline int ContainerFadeOutDelayMs(int from) {
  return std::max(0, std::min(from, kMaxDepth) - 1) * kFadeOutStaggerMs;
}

}  // namespace urnw::tunnel
