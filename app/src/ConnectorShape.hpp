// The URnetwork connector silhouette -- the scalloped 32x32 mark that is the
// brand's own shape: the connect hero's globe clip, the login carousel's image
// mask, and the glyph at the centre of an extender share code
// (connect/EXTENDER.md K7).
//
// ONE definition, because three surfaces draw it and a second transcription of
// forty bezier control points is a drift waiting to happen. The path is added
// to the context's CURRENT path and closed; the caller decides whether to
// fill, stroke or clip with it.
//
// Needs cairomm only -- no GTK widget, no SDK.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <utility>

#include <cairomm/context.h>

namespace urnw {

// Adds the connector outline, scaled from its 32x32 design box into the
// `side`-sized square whose top-left corner is (originX, originY).
inline void AddConnectorPath(const Cairo::RefPtr<Cairo::Context>& cr, double originX,
                             double originY, double side) {
  const double u = side / 32.0;
  auto P = [&](double x, double y) { return std::pair(originX + x * u, originY + y * u); };
  auto M = [&](double x, double y) { auto [px, py] = P(x, y); cr->move_to(px, py); };
  auto C = [&](double x1, double y1, double x2, double y2, double x, double y) {
    auto [ax, ay] = P(x1, y1);
    auto [bx, by] = P(x2, y2);
    auto [cx2, cy2] = P(x, y);
    cr->curve_to(ax, ay, bx, by, cx2, cy2);
  };
  auto L = [&](double x, double y) { auto [px, py] = P(x, y); cr->line_to(px, py); };
  M(30, 8);
  C(28.8955, 8, 28, 7.10453, 28, 6);
  C(28, 4.89547, 27.1045, 4, 26, 4);
  C(24.8955, 4, 24, 3.10453, 24, 2);
  C(24, 0.895469, 23.1045, 0, 22, 0);
  L(10, 0);
  C(8.89547, 0, 8, 0.895469, 8, 2);
  C(8, 3.10453, 7.10453, 4, 6, 4);
  C(4.89547, 4, 4, 4.89547, 4, 6);
  C(4, 7.10453, 3.10453, 8, 2, 8);
  C(0.895469, 8, 0, 8.89547, 0, 10);
  L(0, 22);
  C(0, 23.1045, 0.895469, 24, 2, 24);
  C(3.10453, 24, 4, 24.8955, 4, 26);
  C(4, 27.1045, 4.89547, 28, 6, 28);
  C(7.10453, 28, 8, 28.8955, 8, 30);
  C(8, 31.1045, 8.89547, 32, 10, 32);
  L(22, 32);
  C(23.1045, 32, 24, 31.1045, 24, 30);
  C(24, 28.8955, 24.8955, 28, 26, 28);
  C(27.1045, 28, 28, 27.1045, 28, 26);
  C(28, 24.8955, 28.8955, 24, 30, 24);
  C(31.1045, 24, 32, 23.1045, 32, 22);
  L(32, 10);
  C(32, 8.89547, 31.1045, 8, 30, 8);
  cr->close_path();
}

}  // namespace urnw
