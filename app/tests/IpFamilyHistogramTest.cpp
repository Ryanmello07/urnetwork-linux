// The ip-version histogram's grouping and dot geometry -- the pure logic
// behind the drawer's dualstack / v4 / v6 rows.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "IpFamilyHistogramGeometry.hpp"

using urnw::ipfamily::DotDiameter;
using urnw::ipfamily::GroupPoints;
using urnw::ipfamily::Point;
using urnw::ipfamily::Row;
using urnw::ipfamily::RowFor;
using urnw::ipfamily::RowLabelKey;

UR_TEST(IpFamilyHistogram_CategoriesLandInTheirRows) {
  UR_EXPECT_TRUE(RowFor("dualstack") == Row::Both);
  UR_EXPECT_TRUE(RowFor("v4-only") == Row::V4);
  UR_EXPECT_TRUE(RowFor("v6-only") == Row::V6);
}

// Legacy (an older server or peer sends no category) carries v4 only, and so
// does anything this build cannot name: a category that read as "nothing"
// would hide a provider that is carrying traffic.
UR_TEST(IpFamilyHistogram_LegacyAndUnknownReadAsV4) {
  UR_EXPECT_TRUE(RowFor("") == Row::V4);
  UR_EXPECT_TRUE(RowFor("v7-only") == Row::V4);
}

UR_TEST(IpFamilyHistogram_OnlyAddedProvidersAreDots) {
  const std::vector<Point> points = {
      {"Added", "dualstack"},        {"Added", "dualstack"},
      {"Added", "v4-only"},          {"Added", "v6-only"},
      {"InEvaluation", "dualstack"}, {"EvaluationFailed", "v4-only"},
      {"NotAdded", "v6-only"},       {"Removed", "dualstack"},
  };
  const auto rows = GroupPoints(points);
  UR_EXPECT_EQ(2, rows.at(Row::Both));
  UR_EXPECT_EQ(1, rows.at(Row::V4));
  UR_EXPECT_EQ(1, rows.at(Row::V6));
  UR_EXPECT_EQ(4, rows.total());
}

UR_TEST(IpFamilyHistogram_EmptyGridIsThreeEmptyRows) {
  const auto rows = GroupPoints({});
  UR_EXPECT_EQ(0, rows.total());
  UR_EXPECT_EQ(0, rows.at(Row::Both));
  UR_EXPECT_EQ(0, rows.at(Row::V4));
  UR_EXPECT_EQ(0, rows.at(Row::V6));
}

UR_TEST(IpFamilyHistogram_RowLabelsAreTheSdkLabels) {
  UR_EXPECT_TRUE(std::string(RowLabelKey(Row::Both)) == "both");
  UR_EXPECT_TRUE(std::string(RowLabelKey(Row::V4)) == "v4");
  UR_EXPECT_TRUE(std::string(RowLabelKey(Row::V6)) == "v6");
}

// The dot is the connect canvas's cell in its 256pt space: side / cols with
// cols the larger grid dimension, which is how the canvas sizes its own dots.
UR_TEST(IpFamilyHistogram_DotIsTheCanvasCell) {
  UR_EXPECT_EQ(16, DotDiameter(16, 16));
  UR_EXPECT_EQ(8, DotDiameter(32, 32));
  // square grid: the larger dimension is the column count
  UR_EXPECT_EQ(8, DotDiameter(16, 32));
  UR_EXPECT_EQ(8, DotDiameter(32, 16));
}

UR_TEST(IpFamilyHistogram_NoGridUsesTheDefaultWindowWidth) {
  UR_EXPECT_EQ(DotDiameter(urnw::ipfamily::kDefaultGridWidth, 0), DotDiameter(0, 0));
  UR_EXPECT_EQ(16, DotDiameter(0, 0));
  UR_EXPECT_EQ(16, DotDiameter(-3, -3));
}

// A degenerate grid can neither vanish the dots nor swamp the row.
UR_TEST(IpFamilyHistogram_DotDiameterIsClamped) {
  UR_EXPECT_EQ(4, DotDiameter(1000, 1000));
  UR_EXPECT_EQ(32, DotDiameter(1, 1));
}
