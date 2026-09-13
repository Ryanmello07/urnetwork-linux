// The extender panel's reading of ExtenderStatus: the gossip state to color
// and label mapping, the active ring set and the "N of M" figures
// (EXTENDER.md K4/K5).
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "ExtenderStatusPresentation.hpp"

using urnw::extender::Entry;
using urnw::extender::GossipState;
using urnw::extender::GossipStateFor;
using urnw::extender::kMaxRings;
using urnw::extender::Panel;
using urnw::extender::PanelFor;
using urnw::extender::StatusDot;

namespace {
std::vector<Entry> TwoActiveOneIdle() {
  return {
      Entry{"192.0.2.1", "3cdd67", 2},
      Entry{"198.51.100.7", "a1b2c3", 0},
      Entry{"2001:db8::1", "dd4f3c", 1},
  };
}
}  // namespace

// The three SDK words map to the three dots; anything else -- including the
// empty string an older SDK sends -- is RED. Green would claim a mesh that may
// not exist.
UR_TEST(ExtenderStatus_GossipStateMapsToTheThreeDots) {
  UR_EXPECT_TRUE(GossipStateFor("connected") == GossipState::Connected);
  UR_EXPECT_TRUE(GossipStateFor("connecting") == GossipState::Connecting);
  UR_EXPECT_TRUE(GossipStateFor("disconnected") == GossipState::Disconnected);
  UR_EXPECT_TRUE(GossipStateFor("") == GossipState::Disconnected);
  UR_EXPECT_TRUE(GossipStateFor("degraded") == GossipState::Disconnected);
  UR_EXPECT_TRUE(GossipStateFor("Connected") == GossipState::Disconnected);  // case matters

  UR_EXPECT_TRUE(urnw::extender::DotFor(GossipState::Connected) == StatusDot::Green);
  UR_EXPECT_TRUE(urnw::extender::DotFor(GossipState::Connecting) == StatusDot::Yellow);
  UR_EXPECT_TRUE(urnw::extender::DotFor(GossipState::Disconnected) == StatusDot::Red);
}

// The label keys are the store's, and the yellow state's key is NOT
// "connecting" -- the store carries it as gossip_connecting.
UR_TEST(ExtenderStatus_StateLabelsAreTheStoreKeys) {
  const Panel connected = PanelFor(true, {}, 0, 0, "connected", 0);
  UR_EXPECT_TRUE(std::string(connected.stateLabelKey()) == "connected");
  UR_EXPECT_TRUE(std::string(connected.stateLabelEnglish()) == "Connected");

  const Panel connecting = PanelFor(true, {}, 0, 0, "connecting", 0);
  UR_EXPECT_TRUE(std::string(connecting.stateLabelKey()) == "gossip_connecting");
  UR_EXPECT_TRUE(std::string(connecting.stateLabelEnglish()) == "Connecting");

  const Panel disconnected = PanelFor(true, {}, 0, 0, "disconnected", 0);
  UR_EXPECT_TRUE(std::string(disconnected.stateLabelKey()) == "disconnected");
  UR_EXPECT_TRUE(std::string(disconnected.stateLabelEnglish()) == "Disconnected");
}

// No status at all is NOT "zero extenders": the panel hides instead of
// reporting a number it does not have.
UR_TEST(ExtenderStatus_NoStatusIsUnknownNotZero) {
  const Panel none = PanelFor(false, TwoActiveOneIdle(), 2, 9, "connected", 7);
  UR_EXPECT_FALSE(none.known);
  UR_EXPECT_EQ(0, static_cast<int>(none.ringColorHexes.size()));
  UR_EXPECT_EQ(0, none.active);
  UR_EXPECT_EQ(0, none.reserve);
  UR_EXPECT_EQ(0, none.eventsPerMinute);
  UR_EXPECT_TRUE(none.gossip == GossipState::Disconnected);

  // ...and a status that really is empty reads as a known zero
  const Panel empty = PanelFor(true, {}, 0, 0, "disconnected", 0);
  UR_EXPECT_TRUE(empty.known);
  UR_EXPECT_EQ(0, empty.active);
}

// A ring per extender CARRYING TRAFFIC, in the SDK's order and color. An idle
// directory entry is reserve, not a ring.
UR_TEST(ExtenderStatus_RingsAreTheInUseExtendersInOrder) {
  const Panel panel = PanelFor(true, TwoActiveOneIdle(), 2, 9, "connected", 4);
  UR_EXPECT_EQ(2, static_cast<int>(panel.ringColorHexes.size()));
  UR_EXPECT_TRUE(panel.ringColorHexes[0] == "3cdd67");
  UR_EXPECT_TRUE(panel.ringColorHexes[1] == "dd4f3c");
  UR_EXPECT_EQ(0, panel.hiddenRings);
  UR_EXPECT_EQ(2, panel.active);
  UR_EXPECT_EQ(9, panel.reserve);
  UR_EXPECT_EQ(4, panel.eventsPerMinute);
}

// The FIGURES come from the status counts, not from the entry list: K5 defines
// ActiveCount and ReserveCount as the authority, and a truncated extender list
// must not silently restate the count.
UR_TEST(ExtenderStatus_FiguresComeFromTheCountsNotTheEntries) {
  const Panel panel = PanelFor(true, TwoActiveOneIdle(), 5, 40, "connected", 0);
  UR_EXPECT_EQ(2, static_cast<int>(panel.ringColorHexes.size()));
  UR_EXPECT_EQ(5, panel.active);
  UR_EXPECT_EQ(40, panel.reserve);
}

// A negative figure is a bug elsewhere; "-1 of 3" is not something a user can
// read, so everything clamps at zero.
UR_TEST(ExtenderStatus_NegativeCountsClampToZero) {
  const Panel panel = PanelFor(true, {}, -1, -7, "connected", -3);
  UR_EXPECT_EQ(0, panel.active);
  UR_EXPECT_EQ(0, panel.reserve);
  UR_EXPECT_EQ(0, panel.eventsPerMinute);
}

// A runaway status cannot build an unbounded row of widgets; the overflow is
// counted, and the figures beside the rings still state the real number.
UR_TEST(ExtenderStatus_RingsAreCapped) {
  std::vector<Entry> many;
  for (int i = 0; i < kMaxRings + 5; ++i) many.push_back(Entry{"192.0.2.1", "3cdd67", 1});
  const Panel panel = PanelFor(true, many, kMaxRings + 5, kMaxRings + 5, "connected", 0);
  UR_EXPECT_EQ(kMaxRings, static_cast<int>(panel.ringColorHexes.size()));
  UR_EXPECT_EQ(5, panel.hiddenRings);
  UR_EXPECT_EQ(kMaxRings + 5, panel.active);
}

// An extender with no color still gets its ring: the count of rings must match
// the count of live extenders whatever the SDK filled in.
UR_TEST(ExtenderStatus_ColorlessActiveExtenderStillRings) {
  const Panel panel = PanelFor(true, {Entry{"192.0.2.1", "", 1}}, 1, 1, "connecting", 0);
  UR_EXPECT_EQ(1, static_cast<int>(panel.ringColorHexes.size()));
  UR_EXPECT_TRUE(panel.ringColorHexes[0].empty());
  UR_EXPECT_TRUE(panel.dot() == StatusDot::Yellow);
}
