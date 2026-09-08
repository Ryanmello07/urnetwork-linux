// The connect page's easter-egg gate: five taps on the connected dot, each
// within five seconds of the previous one, play the Pro celebration; a longer
// gap starts the count over, and so does a completed sequence.
//
// SPDX-License-Identifier: MPL-2.0
#include "TapSequenceGate.hpp"
#include "TestHarness.hpp"

namespace {

using urnw::TapSequenceGate;

UR_TEST(tapSequenceCompletesOnTheFifthTapWithinTheWindow) {
  TapSequenceGate gate(5, 5000);
  // four taps a second apart do nothing; the fifth completes
  UR_EXPECT_FALSE(gate.Tap(1000));
  UR_EXPECT_FALSE(gate.Tap(2000));
  UR_EXPECT_FALSE(gate.Tap(3000));
  UR_EXPECT_FALSE(gate.Tap(4000));
  UR_EXPECT_EQ(4, gate.Taps());
  UR_EXPECT_TRUE(gate.Tap(5000));
  UR_EXPECT_EQ(0, gate.Taps());
  // each tap only has to follow the previous one within the window: a slow
  // sequence spanning more than five seconds in total still completes
  UR_EXPECT_FALSE(gate.Tap(10000));
  UR_EXPECT_FALSE(gate.Tap(14000));
  UR_EXPECT_FALSE(gate.Tap(18000));
  UR_EXPECT_FALSE(gate.Tap(22000));
  UR_EXPECT_TRUE(gate.Tap(26000));
}

UR_TEST(tapSequenceResetsAfterAFiveSecondGap) {
  TapSequenceGate gate(5, 5000);
  UR_EXPECT_FALSE(gate.Tap(1000));
  UR_EXPECT_FALSE(gate.Tap(2000));
  UR_EXPECT_FALSE(gate.Tap(3000));
  UR_EXPECT_FALSE(gate.Tap(4000));
  // a gap longer than the window: this tap is the first of a new sequence
  UR_EXPECT_FALSE(gate.Tap(9001));
  UR_EXPECT_EQ(1, gate.Taps());
  UR_EXPECT_FALSE(gate.Tap(9002));
  UR_EXPECT_FALSE(gate.Tap(9003));
  UR_EXPECT_FALSE(gate.Tap(9004));
  UR_EXPECT_TRUE(gate.Tap(9005));
  // exactly the window is still inside it
  UR_EXPECT_FALSE(gate.Tap(20000));
  UR_EXPECT_FALSE(gate.Tap(25000));
  UR_EXPECT_EQ(2, gate.Taps());
}

UR_TEST(tapSequenceRestartsAfterALaunch) {
  TapSequenceGate gate(5, 5000);
  for (int i = 1; i <= 4; ++i) UR_EXPECT_FALSE(gate.Tap(i * 100));
  UR_EXPECT_TRUE(gate.Tap(500));
  // the taps right after a launch count from zero again: four more do
  // nothing, the fifth launches again
  UR_EXPECT_FALSE(gate.Tap(600));
  UR_EXPECT_FALSE(gate.Tap(700));
  UR_EXPECT_FALSE(gate.Tap(800));
  UR_EXPECT_FALSE(gate.Tap(900));
  UR_EXPECT_TRUE(gate.Tap(1000));
  // Reset drops a partial count
  UR_EXPECT_FALSE(gate.Tap(1100));
  gate.Reset();
  UR_EXPECT_EQ(0, gate.Taps());
}

}  // namespace
