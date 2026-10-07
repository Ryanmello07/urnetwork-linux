// The edit that turns the keyed rows on screen into the rows wanted, as a list
// of steps a widget list can apply one by one. It keeps every row whose key is
// still wanted (so its widget keeps its keyboard focus, its hover and its
// screen reader context) instead of rebuilding the list.
//
// The plan follows the Windows activity feed's reconcile (ConnectPage
// ApplyConnectionsList, urnetwork/windows 0d3fa0b): rows whose key left the
// wanted list go first, back to front, so the indices of those still to
// remove stay valid; then the wanted list is walked from the top, each key
// updating the row already in its place, moving its row up from further down,
// or inserting a new row. Rows left past the end (a key wanted fewer times
// than it is on screen) are removed last.
//
// Indices are positions in the list as it stands when the step runs. A Move
// always takes a row from further down (from > index). No GTK: ConnectPage
// applies the steps to its activity rows, and tests/KeyedReconcileTest.cpp
// replays them on a vector.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace urnw::reconcile {

// What a step does to the list.
enum class StepKind {
  Remove,  // erase the row at index
  Update,  // the row at index stays; render it from wanted[wantedIndex]
  Move,    // take the row at from, render it from wanted[wantedIndex], put it at index
  Insert,  // build a row from wanted[wantedIndex] and put it at index
};

// One edit, at the positions the list has when it runs.
struct Step {
  StepKind kind = StepKind::Update;
  size_t index = 0;
  size_t from = 0;         // Move only
  size_t wantedIndex = 0;  // Update, Move and Insert
};

// The steps that turn current into wanted, in the order the file comment
// gives.
inline std::vector<Step> Plan(const std::vector<std::string>& current,
                              const std::vector<std::string>& wanted) {
  std::vector<Step> steps;
  std::unordered_map<std::string, size_t> wantedCount;
  for (const auto& key : wanted) ++wantedCount[key];

  std::vector<std::string> rows = current;
  for (size_t i = rows.size(); 0 < i--;) {
    if (wantedCount.count(rows[i]) != 0) continue;
    steps.push_back(Step{StepKind::Remove, i, 0, 0});
    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(i));
  }

  for (size_t i = 0; i < wanted.size(); ++i) {
    size_t at = i;
    while (at < rows.size() && rows[at] != wanted[i]) ++at;
    if (at == rows.size()) {
      steps.push_back(Step{StepKind::Insert, i, 0, i});
      rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(i), wanted[i]);
    } else if (at == i) {
      steps.push_back(Step{StepKind::Update, i, 0, i});
    } else {
      steps.push_back(Step{StepKind::Move, i, at, i});
      std::string key = std::move(rows[at]);
      rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(at));
      rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(i), std::move(key));
    }
  }

  for (size_t i = rows.size(); wanted.size() < i--;) {
    steps.push_back(Step{StepKind::Remove, i, 0, 0});
  }
  return steps;
}

}  // namespace urnw::reconcile
