// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "LeaderboardIndicator.hpp"

using namespace urnw::leaderboard;

// The thumb sits at the first loaded row over the total and is as long as the
// loaded window over the total, but never shorter than a finger.
UR_TEST(LeaderboardIndicator_ThumbFollowsTheWindow) {
  // 50 of 12,000 rows loaded from position 1 on a 600px track
  auto thumb = ThumbFor(1, 50, 12000, 600, 2600, 500);
  UR_EXPECT_TRUE(thumb.visible);
  UR_EXPECT_NEAR(0.0, thumb.top, 1e-9);
  UR_EXPECT_NEAR(kThumbMinHeight, thumb.height, 1e-9);  // 2.5px would be unusable

  // the window from position 6,001 sits halfway down
  thumb = ThumbFor(6001, 50, 12000, 600, 2600, 500);
  UR_EXPECT_NEAR(300.0, thumb.top, 1e-9);

  // a window that is half the list is half the track
  thumb = ThumbFor(1, 6000, 12000, 600, 2600, 500);
  UR_EXPECT_NEAR(300.0, thumb.height, 1e-9);

  // the last window never hangs past the track's end
  thumb = ThumbFor(11951, 50, 12000, 600, 2600, 500);
  UR_EXPECT_NEAR(600.0 - kThumbMinHeight, thumb.top, 1e-9);
}

// Hidden while the list is shorter than the viewport or nothing is ranked.
UR_TEST(LeaderboardIndicator_ThumbHiddenForShortLists) {
  UR_EXPECT_FALSE(ThumbFor(1, 8, 8, 600, 416, 500).visible);      // fits the pane
  UR_EXPECT_FALSE(ThumbFor(1, 0, 0, 600, 2600, 500).visible);     // nothing ranked
  UR_EXPECT_FALSE(ThumbFor(1, 50, 12000, 0, 2600, 500).visible);  // no track yet
  UR_EXPECT_TRUE(ThumbFor(1, 50, 12000, 600, 2600, 500).visible);
}

// Dragging the thumb maps its travel onto ranks 1..N and back.
UR_TEST(LeaderboardIndicator_RankRoundTrips) {
  const double track = 600;
  const auto thumb = ThumbFor(1, 50, 12000, track, 2600, 500);
  UR_EXPECT_EQ(1, RankForThumbTop(0, thumb.height, track, 12000));
  UR_EXPECT_EQ(12000, RankForThumbTop(track - thumb.height, thumb.height, track, 12000));
  UR_EXPECT_EQ(12000, RankForThumbTop(track + 50, thumb.height, track, 12000));  // clamped
  UR_EXPECT_EQ(1, RankForThumbTop(-10, thumb.height, track, 12000));
  // halfway down the travel is halfway through the ranks
  const int64_t mid = RankForThumbTop((track - thumb.height) / 2, thumb.height, track, 12000);
  UR_EXPECT_TRUE(5990 <= mid && mid <= 6010);
  // a thumb as long as the track has no travel: rank 1
  UR_EXPECT_EQ(1, RankForThumbTop(100, track, track, 12000));
  UR_EXPECT_EQ(1, RankForThumbTop(100, 44, track, 0));
}

// The first row in view from the scroller, offset by the loaded window's start.
UR_TEST(LeaderboardIndicator_FirstVisiblePosition) {
  // the table header is 28px, rows 52px, the window starts at 1
  UR_EXPECT_EQ(1, FirstVisiblePosition(0, 28, 52, 1, 50));
  UR_EXPECT_EQ(1, FirstVisiblePosition(79, 28, 52, 1, 50));    // still on the first row
  UR_EXPECT_EQ(2, FirstVisiblePosition(80, 28, 52, 1, 50));
  UR_EXPECT_EQ(50, FirstVisiblePosition(10000, 28, 52, 1, 50));  // clamped to the window
  // a window from 6,001 reads its positions
  UR_EXPECT_EQ(6003, FirstVisiblePosition(28 + 104, 28, 52, 6001, 50));
  UR_EXPECT_EQ(6001, FirstVisiblePosition(0, 28, 52, 6001, 0));  // no rows: the window's start
}

// The page before the window is asked for near the window's first row, once.
UR_TEST(LeaderboardIndicator_ShouldLoadMoreBefore) {
  UR_EXPECT_TRUE(ShouldLoadMoreBefore(0, false, true, false));
  UR_EXPECT_TRUE(ShouldLoadMoreBefore(kLoadBeforeThreshold, false, true, false));
  UR_EXPECT_FALSE(ShouldLoadMoreBefore(kLoadBeforeThreshold + 1, false, true, false));
  UR_EXPECT_FALSE(ShouldLoadMoreBefore(0, true, true, false));    // a page is in flight
  UR_EXPECT_FALSE(ShouldLoadMoreBefore(0, false, false, false));  // the window starts at 1
  UR_EXPECT_FALSE(ShouldLoadMoreBefore(0, false, true, true));    // retry, not a loop
  UR_EXPECT_FALSE(ShouldLoadMoreBefore(-1, false, true, false));
}

// Activating a tab always scrolls its list to the top; Points also reloads
// from the top when its window no longer starts at 1.
UR_TEST(LeaderboardIndicator_TabReset) {
  auto reset = TabResetFor(/*pointsBoard=*/false, true, 6001);
  UR_EXPECT_TRUE(reset.scrollToTop);
  UR_EXPECT_FALSE(reset.reloadFromTop);  // the data board has no window

  reset = TabResetFor(true, true, 1);
  UR_EXPECT_TRUE(reset.scrollToTop);
  UR_EXPECT_FALSE(reset.reloadFromTop);  // already at the top: a scroll is enough

  reset = TabResetFor(true, true, 6001);
  UR_EXPECT_TRUE(reset.scrollToTop);
  UR_EXPECT_TRUE(reset.reloadFromTop);

  reset = TabResetFor(true, false, 6001);
  UR_EXPECT_FALSE(reset.reloadFromTop);  // no controller to reload
}

// The tier line's store key, and the rank with thousands grouping.
UR_TEST(LeaderboardIndicator_LabelParts) {
  UR_EXPECT_TRUE(std::string("leaderboard_tier_top") == std::string(TierLabelKey(kTierTop1)));
  UR_EXPECT_TRUE(std::string("leaderboard_tier_top") == std::string(TierLabelKey(kTierTop50)));
  UR_EXPECT_TRUE(std::string("leaderboard_tier_rest") == std::string(TierLabelKey(kTierRest)));
  UR_EXPECT_TRUE(std::string("") == std::string(TierLabelKey(kTierUnknown)));
  UR_EXPECT_TRUE(std::string("") == std::string(TierLabelKey(99)));

  UR_EXPECT_TRUE(std::string("1") == GroupedRank(1, ","));
  UR_EXPECT_TRUE(std::string("999") == GroupedRank(999, ","));
  UR_EXPECT_TRUE(std::string("1,000") == GroupedRank(1000, ","));
  UR_EXPECT_TRUE(std::string("1,240") == GroupedRank(1240, ","));
  UR_EXPECT_TRUE(std::string("1.234.567") == GroupedRank(1234567, "."));
  UR_EXPECT_TRUE(std::string("12 000") == GroupedRank(12000, " "));
}

// Keyboard steps: an arrow moves by a window, a page key by a tenth, Home/End
// to the ends, always inside 1..N.
UR_TEST(LeaderboardIndicator_KeyboardSteps) {
  UR_EXPECT_EQ(51, StepRank(1, Step::Down, 50, 12000));
  UR_EXPECT_EQ(1, StepRank(20, Step::Up, 50, 12000));
  UR_EXPECT_EQ(1201, StepRank(1, Step::PageDown, 50, 12000));
  UR_EXPECT_EQ(1, StepRank(600, Step::PageUp, 50, 12000));
  UR_EXPECT_EQ(1, StepRank(6000, Step::Home, 50, 12000));
  UR_EXPECT_EQ(12000, StepRank(6000, Step::End, 50, 12000));
  UR_EXPECT_EQ(12000, StepRank(11990, Step::Down, 50, 12000));
  UR_EXPECT_EQ(1, StepRank(5, Step::Down, 50, 0));
  // a short list pages by its window, never by zero
  UR_EXPECT_EQ(9, StepRank(1, Step::PageDown, 8, 40));
}
