// The glyphs the app fills from SVG path data (MdiGlyphs.hpp, through
// GlyphPath.hpp to Cairo in BrandIcons.cpp): the path grammar the reader
// accepts and refuses, the arc and curve conversions on known shapes, and
// every glyph the app ships reading whole and inside its 24x24 box. A glyph
// whose path does not read draws nothing, which on screen is the blank icon
// BrandIcons.hpp warns about, so each one is read here.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "GlyphPath.hpp"
#include "MdiGlyphs.hpp"

using urnw::MdiGlyph;
using urnw::glyph::BoundsOf;
using urnw::glyph::ParsePath;
using urnw::glyph::Segment;

namespace {

using Kind = Segment::Kind;

std::vector<Segment> Read(const char* path) {
  const std::optional<std::vector<Segment>> segments = ParsePath(path);
  if (!segments) {
    UR_FAIL(std::string("did not read: ") + path);
    return {};
  }
  return *segments;
}

size_t CountKind(const std::vector<Segment>& segments, Kind kind) {
  size_t count = 0;
  for (const Segment& segment : segments) count += segment.kind == kind ? 1 : 0;
  return count;
}

// The point a cubic passes through at t.
void CubicAt(double x0, double y0, const Segment& c, double t, double& x, double& y) {
  const double u = 1 - t;
  x = u * u * u * x0 + 3 * u * u * t * c.x1 + 3 * u * t * t * c.x2 + t * t * t * c.x;
  y = u * u * u * y0 + 3 * u * u * t * c.y1 + 3 * u * t * t * c.y2 + t * t * t * c.y;
}

}  // namespace

UR_TEST(GlyphPath_AbsoluteCommandsReadAsSegments) {
  const std::vector<Segment> s = Read("M1 2L3 4H5V6C7 8 9 10 11 12Z");
  UR_EXPECT_EQ(6u, s.size());
  if (s.size() != 6) return;
  UR_EXPECT_TRUE(s[0].kind == Kind::Move && s[0].x == 1 && s[0].y == 2);
  UR_EXPECT_TRUE(s[1].kind == Kind::Line && s[1].x == 3 && s[1].y == 4);
  UR_EXPECT_TRUE(s[2].kind == Kind::Line && s[2].x == 5 && s[2].y == 4);  // H keeps y
  UR_EXPECT_TRUE(s[3].kind == Kind::Line && s[3].x == 5 && s[3].y == 6);  // V keeps x
  UR_EXPECT_TRUE(s[4].kind == Kind::Curve && s[4].x1 == 7 && s[4].y2 == 10 && s[4].x == 11);
  UR_EXPECT_TRUE(s[5].kind == Kind::Close);
}

UR_TEST(GlyphPath_RelativeCommandsFollowTheCurrentPoint) {
  // l after m, h, v, c, then a move after a close starts from the subpath's
  // first point
  const std::vector<Segment> s = Read("m10 10l2 0h3v4c1 1 2 2 3 3zm1 1l1 0");
  UR_EXPECT_TRUE(s[1].x == 12 && s[1].y == 10);
  UR_EXPECT_TRUE(s[2].x == 15 && s[2].y == 10);
  UR_EXPECT_TRUE(s[3].x == 15 && s[3].y == 14);
  UR_EXPECT_TRUE(s[4].kind == Kind::Curve && s[4].x1 == 16 && s[4].y1 == 15 && s[4].x == 18 &&
                 s[4].y == 17);
  UR_EXPECT_TRUE(s[5].kind == Kind::Close);
  UR_EXPECT_TRUE(s[6].kind == Kind::Move && s[6].x == 11 && s[6].y == 11);
  UR_EXPECT_TRUE(s[7].kind == Kind::Line && s[7].x == 12 && s[7].y == 11);
}

UR_TEST(GlyphPath_RepeatedNumbersRepeatTheCommand) {
  // pairs after a moveto are linetos (relative after a relative one)
  const std::vector<Segment> moves = Read("M1,1 2,2 3,3m1 1 1 1");
  UR_EXPECT_EQ(5u, moves.size());
  UR_EXPECT_TRUE(moves[0].kind == Kind::Move);
  UR_EXPECT_TRUE(moves[1].kind == Kind::Line && moves[2].kind == Kind::Line);
  UR_EXPECT_TRUE(moves[3].kind == Kind::Move && moves[3].x == 4 && moves[3].y == 4);
  UR_EXPECT_TRUE(moves[4].kind == Kind::Line && moves[4].x == 5 && moves[4].y == 5);
  const std::vector<Segment> curves = Read("M0 0C1 1 2 2 3 3 4 4 5 5 6 6");
  UR_EXPECT_EQ(2u, CountKind(curves, Kind::Curve));
  UR_EXPECT_TRUE(curves[2].x == 6);
}

UR_TEST(GlyphPath_NumbersRunTogetherWhereTheGrammarAllows) {
  // ".5.5" is two numbers, "-" starts the next, exponents are numbers
  const std::vector<Segment> s = Read("M.5.5L-.5-1.5L1e1,2E-1");
  UR_EXPECT_NEAR(0.5, s[0].x, 1e-12);
  UR_EXPECT_NEAR(0.5, s[0].y, 1e-12);
  UR_EXPECT_NEAR(-0.5, s[1].x, 1e-12);
  UR_EXPECT_NEAR(-1.5, s[1].y, 1e-12);
  UR_EXPECT_NEAR(10, s[2].x, 1e-12);
  UR_EXPECT_NEAR(0.2, s[2].y, 1e-12);
}

UR_TEST(GlyphPath_SmoothCurvesMirrorThePreviousControlPoint) {
  // after a C the S's first control point mirrors the C's second about the
  // current point; after anything else it is the current point
  const std::vector<Segment> s = Read("M0 0C0 1 2 1 2 0S4 -1 4 0");
  UR_EXPECT_TRUE(s[2].kind == Kind::Curve);
  UR_EXPECT_NEAR(2, s[2].x1, 1e-12);
  UR_EXPECT_NEAR(-1, s[2].y1, 1e-12);
  const std::vector<Segment> plain = Read("M3 3S4 -1 4 0");
  UR_EXPECT_NEAR(3, plain[1].x1, 1e-12);
  UR_EXPECT_NEAR(3, plain[1].y1, 1e-12);
  // the android glyph's "...14.32S16.15 13.5..." is such a mirror
  const std::vector<Segment> android = Read(urnw::MdiGlyphPath(MdiGlyph::DeviceAndroid));
  UR_EXPECT_TRUE(!android.empty());
}

UR_TEST(GlyphPath_QuadraticsBecomeCubics) {
  // Q (0,0) -> (3,0) with control (1.5,3): the cubic's controls sit 2/3 of
  // the way to the quadratic's, and T mirrors it
  const std::vector<Segment> s = Read("M0 0Q1.5 3 3 0T6 0");
  UR_EXPECT_TRUE(s[1].kind == Kind::Curve);
  UR_EXPECT_NEAR(1, s[1].x1, 1e-12);
  UR_EXPECT_NEAR(2, s[1].y1, 1e-12);
  UR_EXPECT_NEAR(2, s[1].x2, 1e-12);
  UR_EXPECT_NEAR(2, s[1].y2, 1e-12);
  // the mirrored control is (4.5, -3)
  UR_EXPECT_NEAR(4, s[2].x1, 1e-12);
  UR_EXPECT_NEAR(-2, s[2].y1, 1e-12);
  UR_EXPECT_NEAR(6, s[2].x, 1e-12);
}

UR_TEST(GlyphPath_ArcsBecomeQuarterTurnCubicsOnTheCircle) {
  // a half circle of radius 10 around (12,12), top to bottom through the
  // right (account-circle-outline's first half: A10,10 0 0,0 ... reversed)
  const std::vector<Segment> s = Read("M12 2A10 10 0 0 1 12 22");
  UR_EXPECT_EQ(2u, CountKind(s, Kind::Curve));  // two quarter turns
  UR_EXPECT_NEAR(12, s.back().x, 1e-9);
  UR_EXPECT_NEAR(22, s.back().y, 1e-9);
  // every piece stays on the circle within a few thousandths of the radius
  double x0 = 12, y0 = 2, worst = 0;
  for (size_t i = 1; i < s.size(); ++i) {
    for (double t = 0; t <= 1.0001; t += 0.125) {
      double x, y;
      CubicAt(x0, y0, s[i], t, x, y);
      worst = std::fmax(worst, std::fabs(std::hypot(x - 12, y - 12) - 10));
    }
    x0 = s[i].x;
    y0 = s[i].y;
  }
  UR_EXPECT_TRUE_MSG("off the circle by " + std::to_string(worst), worst < 0.003);
  // the sweep flag picks the side: sweep 1 passes through the right (x 22)
  UR_EXPECT_TRUE(BoundsOf(s).maxX > 21.9 && BoundsOf(s).minX > 11.9);
  const std::vector<Segment> left = Read("M12 2A10 10 0 0 0 12 22");
  UR_EXPECT_TRUE(BoundsOf(left).minX < 2.1 && BoundsOf(left).maxX < 12.1);
}

UR_TEST(GlyphPath_ArcFlagsMayRunIntoTheNextNumber) {
  // "0 011,1": rotation 0, large-arc 0, sweep 1, then x 1, y 1
  const std::vector<Segment> s = Read("M0 0a1 1 0 011,1");
  UR_EXPECT_TRUE(!s.empty() && s.back().kind == Kind::Curve);
  UR_EXPECT_NEAR(1, s.back().x, 1e-9);
  UR_EXPECT_NEAR(1, s.back().y, 1e-9);
}

UR_TEST(GlyphPath_DegenerateArcsAreLinesOrNothing) {
  // a zero radius is a straight line; an arc to its own start is omitted
  const std::vector<Segment> line = Read("M0 0A0 5 0 0 1 4 0");
  UR_EXPECT_TRUE(line.size() == 2 && line[1].kind == Kind::Line && line[1].x == 4);
  const std::vector<Segment> none = Read("M3 3A5 5 0 0 1 3 3");
  UR_EXPECT_EQ(1u, none.size());
  // radii too small for the endpoints grow to fit: a 1-unit radius between
  // points 10 apart is a half circle of radius 5
  const std::vector<Segment> grown = Read("M0 0A1 1 0 0 1 10 0");
  UR_EXPECT_NEAR(10, grown.back().x, 1e-9);
  UR_EXPECT_NEAR(-5, BoundsOf(grown).minY, 0.01);
}

UR_TEST(GlyphPath_MalformedPathsAreRefused) {
  // empty, no opening moveto, short of numbers, an unknown command, an arc
  // flag that is not 0 or 1, an exponent letter read as a command
  for (const char* bad : {"", "   ", "L1 1", "M1", "M1,,", "M1 1L", "M1 1X2 2", "M1 1C1 1 2 2",
                          "M0 0A1 1 0 2 0 1 1", "M1 1e"}) {
    UR_EXPECT_TRUE_MSG(std::string("refused: \"") + bad + "\"", !ParsePath(bad).has_value());
  }
  // "1." is a whole number in the grammar
  UR_EXPECT_TRUE(ParsePath("M1 1.").has_value());
}

// The table names every glyph once, in declaration order (BrandIcons.cpp caches
// a glyph's segments by its value), and every glyph reads whole.
UR_TEST(GlyphPath_EveryAppGlyphReadsInsideItsBox) {
  UR_EXPECT_EQ(static_cast<size_t>(MdiGlyph::Copy) + 1, urnw::kMdiGlyphCount);
  std::set<std::string> names;
  for (size_t i = 0; i < urnw::kMdiGlyphCount; ++i) {
    const urnw::MdiGlyphData& data = urnw::kMdiGlyphs[i];
    UR_EXPECT_TRUE_MSG(data.mdiName, static_cast<size_t>(data.glyph) == i);
    UR_EXPECT_TRUE_MSG(data.mdiName, names.insert(data.mdiName).second);
    UR_EXPECT_TRUE_MSG(data.mdiName, std::string(urnw::MdiGlyphPath(data.glyph)) == data.path);
    const std::optional<std::vector<Segment>> segments = ParsePath(data.path);
    UR_EXPECT_TRUE_MSG(std::string(data.mdiName) + " reads", segments.has_value());
    if (!segments) continue;
    UR_EXPECT_TRUE_MSG(data.mdiName, CountKind(*segments, Kind::Move) >= 1);
    const urnw::glyph::Bounds box = BoundsOf(*segments);
    UR_EXPECT_TRUE_MSG(std::string(data.mdiName) + " inside 24x24",
                       box.minX >= -0.01 && box.minY >= -0.01 && box.maxX <= 24.01 &&
                           box.maxY <= 24.01);
    // ...and fills a real part of it, not a speck
    UR_EXPECT_TRUE_MSG(std::string(data.mdiName) + " spans the box",
                       box.maxX - box.minX > 8 && box.maxY - box.minY > 8);
  }
}

// REVOKE-UI-FINAL.md §8 names nine glyphs by their MDI names.
UR_TEST(GlyphPath_TheSessionsGlyphsAreTheDesignsMdiGlyphs) {
  const std::pair<MdiGlyph, const char*> design[] = {
      {MdiGlyph::SessionFace, "head-outline"},  {MdiGlyph::DeviceAndroid, "android"},
      {MdiGlyph::DeviceApple, "apple"},         {MdiGlyph::DeviceWindows, "microsoft-windows"},
      {MdiGlyph::DeviceLinux, "linux"},         {MdiGlyph::DeviceWeb, "web"},
      {MdiGlyph::DeviceCli, "console"},         {MdiGlyph::DeviceServer, "server"},
      {MdiGlyph::DeviceUnknown, "help"},
  };
  for (const auto& [glyph, name] : design) {
    UR_EXPECT_TRUE_MSG(name, std::string(urnw::kMdiGlyphs[static_cast<size_t>(glyph)].mdiName) ==
                                 name);
  }
  // the face profile's path data, as the design embeds it
  UR_EXPECT_TRUE(std::string(urnw::MdiGlyphPath(MdiGlyph::SessionFace)) ==
                 "M13 1C8.4 1 4.6 4.4 4.1 8.9L2.5 11C2 11.8 1.9 12.8 2.3 13.6C2.7 14.3 3.3 14.8 "
                 "4 14.9V16C4 17.8 5.3 19.4 7 19.9V23H18V17.5C20.5 15.8 22 13.1 22 10C22 5 18 1 "
                 "13 1M16 16.3V21H9V18H8C6.9 18 6 17.1 6 16V13H4.5C4.1 13 3.8 12.5 4.1 12.2L6 "
                 "9.7C6.2 6 9.2 3 13 3C16.9 3 20 6.1 20 10C20 12.8 18.4 15.2 16 16.3Z");
}
