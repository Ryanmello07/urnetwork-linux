// SVG path data (a <path>'s `d`) read into the segments a Cairo context draws:
// move, line, cubic curve and close, all absolute. The glyphs this app fills
// from path data (MdiGlyphs.hpp) reach Cairo through it (BrandIcons.cpp), so a
// path with a typo cannot draw garbage: one that does not read whole is
// refused, and tests/GlyphPathTest.cpp reads every glyph the app ships.
//
// The full path grammar, not only what today's glyphs use: M L H V C S Q T A Z
// in both cases; implicit repetition (pairs after an M are lines); numbers
// run together where the grammar allows (".5.5", "1-2", "1e-2"); arc flags
// written without separators ("0 011,1"). Quadratic curves become cubics, and
// each elliptical arc becomes cubic pieces of at most a quarter turn
// (SVG 1.1 implementation notes F.6.5, F.6.6). An arc with a zero radius is a
// line, and one that ends where it starts is skipped, as the notes say.
//
// Pure C++17, no GTK and no Cairo, so it runs in the unit tests.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace urnw::glyph {

struct Segment {
  enum class Kind { Move, Line, Curve, Close };
  Kind kind = Kind::Move;
  // Move and Line: the point (x, y). Curve: the control points (x1, y1) and
  // (x2, y2), then the end point (x, y). Close: nothing.
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;
};

namespace detail {

constexpr double kPi = 3.14159265358979323846;

// The reader over one path string; every Read* returns false at a malformed
// or missing value.
class PathReader {
 public:
  explicit PathReader(std::string_view text) : text_(text) {}

  void SkipSeparators() {
    while (at_ < text_.size() &&
           (std::isspace(static_cast<unsigned char>(text_[at_])) != 0 || text_[at_] == ',')) {
      ++at_;
    }
  }
  bool AtEnd() {
    SkipSeparators();
    return at_ >= text_.size();
  }
  // A command letter next (after separators)?
  bool AtCommand() {
    SkipSeparators();
    return at_ < text_.size() && std::isalpha(static_cast<unsigned char>(text_[at_])) != 0;
  }
  // A number next (after separators)?
  bool AtNumber() {
    SkipSeparators();
    if (at_ >= text_.size()) return false;
    const char c = text_[at_];
    return c == '+' || c == '-' || c == '.' || std::isdigit(static_cast<unsigned char>(c)) != 0;
  }
  char TakeCommand() { return text_[at_++]; }

  bool ReadNumber(double& out) {
    SkipSeparators();
    const size_t start = at_;
    size_t i = at_;
    if (i < text_.size() && (text_[i] == '+' || text_[i] == '-')) ++i;
    size_t digits = 0;
    while (i < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i])) != 0) {
      ++i;
      ++digits;
    }
    if (i < text_.size() && text_[i] == '.') {
      ++i;
      while (i < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i])) != 0) {
        ++i;
        ++digits;
      }
    }
    if (digits == 0) return false;
    if (i < text_.size() && (text_[i] == 'e' || text_[i] == 'E')) {
      size_t k = i + 1;
      if (k < text_.size() && (text_[k] == '+' || text_[k] == '-')) ++k;
      size_t exponent = 0;
      while (k < text_.size() && std::isdigit(static_cast<unsigned char>(text_[k])) != 0) {
        ++k;
        ++exponent;
      }
      if (exponent > 0) i = k;  // an "e" with no digits is not part of the number
    }
    const std::string number(text_.substr(start, i - start));
    char* end = nullptr;
    out = std::strtod(number.c_str(), &end);
    if (end != number.c_str() + number.size() || !std::isfinite(out)) return false;
    at_ = i;
    return true;
  }

  // An arc flag: one '0' or '1', which may run straight into what follows.
  bool ReadFlag(bool& out) {
    SkipSeparators();
    if (at_ >= text_.size() || (text_[at_] != '0' && text_[at_] != '1')) return false;
    out = text_[at_++] == '1';
    return true;
  }

  bool ReadPair(double& x, double& y) { return ReadNumber(x) && ReadNumber(y); }

 private:
  std::string_view text_;
  size_t at_ = 0;
};

// One arc in endpoint form as cubic pieces from (x0, y0), appended to `out`.
inline void AppendArc(std::vector<Segment>& out, double x0, double y0, double rx, double ry,
                      double rotationDegrees, bool largeArc, bool sweep, double x, double y) {
  if (x0 == x && y0 == y) return;  // F.6.2: an arc to its own start is omitted
  rx = std::fabs(rx);
  ry = std::fabs(ry);
  if (rx == 0 || ry == 0) {
    out.push_back(Segment{Segment::Kind::Line, 0, 0, 0, 0, x, y});
    return;
  }
  const double phi = rotationDegrees * kPi / 180.0;
  const double cosPhi = std::cos(phi);
  const double sinPhi = std::sin(phi);
  // F.6.5.1: the midpoint in the ellipse's frame
  const double dx = (x0 - x) / 2.0;
  const double dy = (y0 - y) / 2.0;
  const double x1p = cosPhi * dx + sinPhi * dy;
  const double y1p = -sinPhi * dx + cosPhi * dy;
  // F.6.6: radii too small for the endpoints grow until they just fit
  const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lambda > 1) {
    const double scale = std::sqrt(lambda);
    rx *= scale;
    ry *= scale;
  }
  // F.6.5.2: the centre in the ellipse's frame
  const double numerator = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
  const double denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  double coefficient = denominator == 0 ? 0 : std::sqrt(std::max(0.0, numerator / denominator));
  if (largeArc == sweep) coefficient = -coefficient;
  const double cxp = coefficient * (rx * y1p / ry);
  const double cyp = coefficient * -(ry * x1p / rx);
  // F.6.5.3: the centre
  const double cx = cosPhi * cxp - sinPhi * cyp + (x0 + x) / 2.0;
  const double cy = sinPhi * cxp + cosPhi * cyp + (y0 + y) / 2.0;
  // F.6.5.5-6: the start angle and the sweep
  const auto angle = [](double ux, double uy, double vx, double vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  const double theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
  double delta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
  if (!sweep && delta > 0) delta -= 2 * kPi;
  if (sweep && delta < 0) delta += 2 * kPi;

  // quarter turns at most: a cubic with handles 4/3 tan(t/4) along the
  // tangents stays within a hair of the circle over that span
  const int pieces = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / (kPi / 2) - 1e-9)));
  const double step = delta / pieces;
  const double handle = 4.0 / 3.0 * std::tan(step / 4.0);
  // a point of the unit circle, mapped onto the ellipse
  const auto map = [&](double ux, double uy, double& px, double& py) {
    px = cx + cosPhi * rx * ux - sinPhi * ry * uy;
    py = cy + sinPhi * rx * ux + cosPhi * ry * uy;
  };
  double theta = theta1;
  for (int i = 0; i < pieces; ++i) {
    const double next = theta + step;
    const double c0 = std::cos(theta), s0 = std::sin(theta);
    const double c1 = std::cos(next), s1 = std::sin(next);
    Segment curve{Segment::Kind::Curve};
    map(c0 - handle * s0, s0 + handle * c0, curve.x1, curve.y1);
    map(c1 + handle * s1, s1 - handle * c1, curve.x2, curve.y2);
    if (i + 1 == pieces) {
      // the last piece lands exactly on the endpoint the path names
      curve.x = x;
      curve.y = y;
    } else {
      map(c1, s1, curve.x, curve.y);
    }
    out.push_back(curve);
    theta = next;
  }
}

}  // namespace detail

// The segments of `pathData`, or nullopt when it does not read whole: an
// unknown command, a command short of its numbers, or a path that does not
// open with a moveto.
inline std::optional<std::vector<Segment>> ParsePath(std::string_view pathData) {
  detail::PathReader reader(pathData);
  std::vector<Segment> out;
  double cx = 0, cy = 0;          // the current point
  double startX = 0, startY = 0;  // the open subpath's first point
  // the reflections S and T mirror: the previous cubic's second control
  // point and the previous quadratic's control point
  double lastCubicX = 0, lastCubicY = 0, lastQuadX = 0, lastQuadY = 0;
  char previous = 0;  // the previous command, upper case
  if (reader.AtEnd()) return std::nullopt;
  while (!reader.AtEnd()) {
    if (!reader.AtCommand()) return std::nullopt;
    const char command = reader.TakeCommand();
    const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
    if (out.empty() && upper != 'M') return std::nullopt;
    if (upper == 'Z') {
      out.push_back(Segment{Segment::Kind::Close});
      cx = startX;
      cy = startY;
      previous = 'Z';
      continue;
    }
    // every other command takes at least one set of numbers, and repeats
    // while more follow
    bool first = true;
    do {
      // a relative command's numbers are offsets from the current point, which
      // moves with each repetition
      const double rx0 = relative ? cx : 0;
      const double ry0 = relative ? cy : 0;
      switch (upper) {
        case 'M': {
          double x, y;
          if (!reader.ReadPair(x, y)) return std::nullopt;
          x += rx0;
          y += ry0;
          // pairs after the first are lines (relative stays relative)
          out.push_back(Segment{first ? Segment::Kind::Move : Segment::Kind::Line, 0, 0, 0, 0, x, y});
          if (first) {
            startX = x;
            startY = y;
          }
          cx = x;
          cy = y;
          previous = first ? 'M' : 'L';
          break;
        }
        case 'L': {
          double x, y;
          if (!reader.ReadPair(x, y)) return std::nullopt;
          cx = x + rx0;
          cy = y + ry0;
          out.push_back(Segment{Segment::Kind::Line, 0, 0, 0, 0, cx, cy});
          previous = 'L';
          break;
        }
        case 'H': {
          double x;
          if (!reader.ReadNumber(x)) return std::nullopt;
          cx = x + rx0;
          out.push_back(Segment{Segment::Kind::Line, 0, 0, 0, 0, cx, cy});
          previous = 'H';
          break;
        }
        case 'V': {
          double y;
          if (!reader.ReadNumber(y)) return std::nullopt;
          cy = y + ry0;
          out.push_back(Segment{Segment::Kind::Line, 0, 0, 0, 0, cx, cy});
          previous = 'V';
          break;
        }
        case 'C': {
          double x1, y1, x2, y2, x, y;
          if (!reader.ReadPair(x1, y1) || !reader.ReadPair(x2, y2) || !reader.ReadPair(x, y)) {
            return std::nullopt;
          }
          Segment curve{Segment::Kind::Curve, x1 + rx0, y1 + ry0, x2 + rx0, y2 + ry0, x + rx0,
                        y + ry0};
          out.push_back(curve);
          lastCubicX = curve.x2;
          lastCubicY = curve.y2;
          cx = curve.x;
          cy = curve.y;
          previous = 'C';
          break;
        }
        case 'S': {
          double x2, y2, x, y;
          if (!reader.ReadPair(x2, y2) || !reader.ReadPair(x, y)) return std::nullopt;
          // the first control point mirrors the previous cubic's second, or
          // is the current point after anything else
          const bool mirror = previous == 'C' || previous == 'S';
          const double x1 = mirror ? 2 * cx - lastCubicX : cx;
          const double y1 = mirror ? 2 * cy - lastCubicY : cy;
          Segment curve{Segment::Kind::Curve, x1, y1, x2 + rx0, y2 + ry0, x + rx0, y + ry0};
          out.push_back(curve);
          lastCubicX = curve.x2;
          lastCubicY = curve.y2;
          cx = curve.x;
          cy = curve.y;
          previous = 'S';
          break;
        }
        case 'Q':
        case 'T': {
          double qx, qy, x, y;
          if (upper == 'Q') {
            if (!reader.ReadPair(qx, qy) || !reader.ReadPair(x, y)) return std::nullopt;
            qx += rx0;
            qy += ry0;
          } else {
            if (!reader.ReadPair(x, y)) return std::nullopt;
            const bool mirror = previous == 'Q' || previous == 'T';
            qx = mirror ? 2 * cx - lastQuadX : cx;
            qy = mirror ? 2 * cy - lastQuadY : cy;
          }
          x += rx0;
          y += ry0;
          // a quadratic is the cubic with its control points two thirds of
          // the way to the quadratic's
          out.push_back(Segment{Segment::Kind::Curve, cx + 2.0 / 3.0 * (qx - cx),
                                cy + 2.0 / 3.0 * (qy - cy), x + 2.0 / 3.0 * (qx - x),
                                y + 2.0 / 3.0 * (qy - y), x, y});
          lastQuadX = qx;
          lastQuadY = qy;
          cx = x;
          cy = y;
          previous = upper;
          break;
        }
        case 'A': {
          double rx, ry, rotation, x, y;
          bool largeArc, sweep;
          if (!reader.ReadNumber(rx) || !reader.ReadNumber(ry) || !reader.ReadNumber(rotation) ||
              !reader.ReadFlag(largeArc) || !reader.ReadFlag(sweep) || !reader.ReadPair(x, y)) {
            return std::nullopt;
          }
          x += rx0;
          y += ry0;
          detail::AppendArc(out, cx, cy, rx, ry, rotation, largeArc, sweep, x, y);
          cx = x;
          cy = y;
          previous = 'A';
          break;
        }
        default:
          return std::nullopt;
      }
      first = false;
    } while (reader.AtNumber());
  }
  return out;
}

// The smallest box holding every end point and control point.
struct Bounds {
  double minX = 0, minY = 0, maxX = 0, maxY = 0;
};

inline Bounds BoundsOf(const std::vector<Segment>& segments) {
  Bounds box;
  bool any = false;
  const auto add = [&](double x, double y) {
    if (!any) {
      box = Bounds{x, y, x, y};
      any = true;
      return;
    }
    box.minX = std::min(box.minX, x);
    box.minY = std::min(box.minY, y);
    box.maxX = std::max(box.maxX, x);
    box.maxY = std::max(box.maxY, y);
  };
  for (const Segment& segment : segments) {
    switch (segment.kind) {
      case Segment::Kind::Curve:
        add(segment.x1, segment.y1);
        add(segment.x2, segment.y2);
        add(segment.x, segment.y);
        break;
      case Segment::Kind::Move:
      case Segment::Kind::Line:
        add(segment.x, segment.y);
        break;
      case Segment::Kind::Close:
        break;
    }
  }
  return box;
}

}  // namespace urnw::glyph
