// The Settings fold table and pane A's second doors (SettingsFold.hpp): three
// panes from 1400dip, two from 900, one below, the About doors while About is
// folded and the Device door while Device is, so exactly one copy of each
// shows at any width.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "SettingsFold.hpp"

namespace {

using namespace urnw::settings_fold;

}  // namespace

UR_TEST(SettingsFold_PanesFoldAt1400And900) {
  UR_EXPECT_EQ(1400, kThreePaneDip);
  UR_EXPECT_EQ(900, kTwoPaneDip);
  UR_EXPECT_EQ(3, PaneCount(2062));  // the reference window
  UR_EXPECT_EQ(3, PaneCount(1400));
  UR_EXPECT_EQ(2, PaneCount(1399));
  UR_EXPECT_EQ(2, PaneCount(1120));  // the default window
  UR_EXPECT_EQ(2, PaneCount(900));
  UR_EXPECT_EQ(1, PaneCount(899));
  UR_EXPECT_EQ(1, PaneCount(0));
}

UR_TEST(SettingsFold_EachPanesDoorsShowExactlyWhileItIsFolded) {
  // three panes: everything in its own pane, no copies
  UR_EXPECT_FALSE(AboutDoorsShown(3));
  UR_EXPECT_FALSE(DeviceDoorsShown(3));
  // the default window: About folded, so its version rows and Licenses
  // move to pane A, while the Advanced toggle stays in Device
  UR_EXPECT_TRUE(AboutDoorsShown(2));
  UR_EXPECT_FALSE(DeviceDoorsShown(2));
  // one pane: both
  UR_EXPECT_TRUE(AboutDoorsShown(1));
  UR_EXPECT_TRUE(DeviceDoorsShown(1));
  // at every width a pane and its copy are never both on screen, and never
  // both away
  for (int width = 0; width <= 2400; width += 50) {
    const int panes = PaneCount(width);
    UR_EXPECT_TRUE((panes >= 3) != AboutDoorsShown(panes));
    UR_EXPECT_TRUE((panes >= 2) != DeviceDoorsShown(panes));
  }
}
