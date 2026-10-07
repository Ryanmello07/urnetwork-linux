// The accessible name of a location row (LocationRowName.hpp), read the same
// way on the Network page and in the location chooser: the title, the figure
// or caption, then a word for each state the row's hidden glyphs show.
// SPDX-License-Identifier: MPL-2.0
#include <string>

#include "LocationRowName.hpp"
#include "TestHarness.hpp"

namespace {

const urnw::LocationRowWords kWords{"* (may be unstable)", "Strong Anonymization", "Network peers",
                                    "Selected provider"};

bool NameIs(const std::string& expected, const std::string& actual) {
  if (expected == actual) return true;
  UR_FAIL("expected \"" + expected + "\", got \"" + actual + "\"");
  return false;
}

}  // namespace

// A plain row reads its title and its figure.
UR_TEST(LocationRowName_TitleThenFigure) {
  NameIs("Japan, 12 providers", urnw::LocationRowName("Japan", "12 providers", {}, kWords));
  NameIs("Best available provider",
         urnw::LocationRowName("Best available provider", "", {}, kWords));
}

// Each glyph's state is read in a fixed order after the figure, so a screen
// reader hears what the hidden glyphs show.
UR_TEST(LocationRowName_StatesFollowInOrder) {
  urnw::LocationRowStates all;
  all.unstable = true;
  all.strongPrivacy = true;
  all.providing = true;
  all.selected = true;
  NameIs("Japan, 12 providers, * (may be unstable), Strong Anonymization, Network peers, "
         "Selected provider",
         urnw::LocationRowName("Japan", "12 providers", all, kWords));
  urnw::LocationRowStates peer;
  peer.providing = true;
  peer.selected = true;
  NameIs("Office laptop, Ubuntu 24.04, Network peers, Selected provider",
         urnw::LocationRowName("Office laptop", "Ubuntu 24.04", peer, kWords));
  urnw::LocationRowStates selected;
  selected.selected = true;
  NameIs("Best available provider, Selected provider",
         urnw::LocationRowName("Best available provider", "", selected, kWords));
}

// A row with no title (a location the SDK sent unnamed) never opens on a
// separator.
UR_TEST(LocationRowName_NoLeadingSeparator) {
  NameIs("12 providers", urnw::LocationRowName("", "12 providers", {}, kWords));
  urnw::LocationRowStates selected;
  selected.selected = true;
  NameIs("Selected provider", urnw::LocationRowName("", "", selected, kWords));
}
