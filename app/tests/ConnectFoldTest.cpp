// The connect page's fold table (ConnectFold.hpp): Simple Mode is one pane at
// any width, and Advanced Mode opens the statistics pane only once the
// activity pane keeps 330dip beside the 330 and 380 rails and their two rules.
// While the statistics pane is folded, pane A shows the second doors to its
// sheets. ConnectFoldDoorsWiringTest.cpp reads the page that applies both.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "ConnectFold.hpp"

using urnw::connect_fold::FoldDoorsShown;
using urnw::connect_fold::kThreePaneDip;
using urnw::connect_fold::kTwoPaneDip;
using urnw::connect_fold::PaneCount;

UR_TEST(ConnectFold_SimpleIsAlwaysOnePane) {
  for (const int width : {0, 400, 639, 640, 1000, 1041, 1042, 1800, 4000}) {
    UR_EXPECT_EQ(1, PaneCount(false, width));
  }
}

UR_TEST(ConnectFold_AdvancedFoldsOnTheSharedWidth) {
  UR_EXPECT_EQ(1, PaneCount(true, 0));
  UR_EXPECT_EQ(1, PaneCount(true, 639));
  UR_EXPECT_EQ(2, PaneCount(true, 640));
  UR_EXPECT_EQ(2, PaneCount(true, 1000));
  UR_EXPECT_EQ(2, PaneCount(true, 1041));
  UR_EXPECT_EQ(3, PaneCount(true, 1042));
  UR_EXPECT_EQ(3, PaneCount(true, 1800));
}

// At the gate the activity pane is left exactly its floor: the default
// 1120dip window behind the 220dip nav rail (900dip) stays two panes.
UR_TEST(ConnectFold_TheThirdPaneLeavesActivityItsFloor) {
  UR_EXPECT_EQ(640, kTwoPaneDip);
  UR_EXPECT_EQ(1042, kThreePaneDip);
  UR_EXPECT_EQ(330, kThreePaneDip - 330 - 1 - 1 - 380);
  UR_EXPECT_EQ(2, PaneCount(true, 1120 - 220));
}

// Exactly one set of doors: pane A's while pane C is folded, at any width in
// Simple Mode and under the gate in Advanced, and pane C's own above it.
UR_TEST(ConnectFold_TheSecondDoorsShowWhilePaneCIsFolded) {
  UR_EXPECT_TRUE(FoldDoorsShown(1));
  UR_EXPECT_TRUE(FoldDoorsShown(2));
  UR_EXPECT_FALSE(FoldDoorsShown(3));
  UR_EXPECT_TRUE(FoldDoorsShown(PaneCount(false, 1800)));
  UR_EXPECT_TRUE(FoldDoorsShown(PaneCount(true, 1041)));
  UR_EXPECT_TRUE(FoldDoorsShown(PaneCount(true, 1120 - 220)));
  UR_EXPECT_FALSE(FoldDoorsShown(PaneCount(true, 1042)));
}
