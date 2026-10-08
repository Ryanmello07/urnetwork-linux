// The accessible name of a location row, one way for both lists of them: the
// Network page's rows (NetworkPage::MakeRow) and the location chooser's
// (LocationsSheet.cpp). A row is a button whose content is a box, which GTK
// names from nothing, and its dot and state glyphs are hidden from the
// accessibility tree, so the name carries the whole row: the title, the
// figure or caption under it, then a word for each state the glyphs show.
//
// Header-only and free of GTK so the unit tests can build it
// (tests/LocationRowNameTest.cpp); the words are the callers' localized
// strings.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {

// What a location row's glyphs show.
struct LocationRowStates {
  bool unstable = false;
  bool strongPrivacy = false;
  bool providing = false;  // one of the user's own devices, providing
  bool selected = false;
};

// The word read for each state.
struct LocationRowWords {
  std::string unstable;
  std::string strongPrivacy;
  std::string providing;
  std::string selected;
};

// "title, meta, <state words>", the parts joined with ", ", an empty meta
// left out.
inline std::string LocationRowName(const std::string& title, const std::string& meta,
                                   const LocationRowStates& states,
                                   const LocationRowWords& words) {
  std::string name = title;
  auto add = [&name](const std::string& part) {
    if (part.empty()) return;
    if (!name.empty()) name += ", ";
    name += part;
  };
  add(meta);
  if (states.unstable) add(words.unstable);
  if (states.strongPrivacy) add(words.strongPrivacy);
  if (states.providing) add(words.providing);
  if (states.selected) add(words.selected);
  return name;
}

}  // namespace urnw
