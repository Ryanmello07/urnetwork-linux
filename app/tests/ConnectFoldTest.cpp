// The connect page's fold table (ConnectFold.hpp): Simple Mode is one pane at
// any width, and Advanced Mode opens the statistics pane only once the
// activity pane keeps 330dip beside the 330 and 380 rails and their two rules.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "ConnectFold.hpp"

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
