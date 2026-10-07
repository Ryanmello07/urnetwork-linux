// What the Account page's Plan pane (pane A) shows in its figure rows, decided
// pure so tests/AccountPlanRowsTest.cpp pins it without GTK.
//
// Under the usage bar, which shows the balance's shape and whose legend only
// colours its parts, rows carry the figures: the daily balance, then Used,
// Pending and Available. Before a balance snapshot has landed none of them has
// a figure, so each shows its state instead of an invented "0 B" (spec §0.4).
// Above Redeem, a Balance Codes row counts the redeemed codes for every
// answered fetch, zero included; with no answer the row is hidden rather than
// drawn over nothing.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace urnw::account_plan {

enum class FigureView {
  Figure,     // the snapshot's figure
  Loading,    // no snapshot yet, and the page can ask for one
  NoSession,  // no snapshot, and no session to ask with
};

constexpr FigureView FigureFor(bool snapshotLoaded, bool canCallApi) {
  if (snapshotLoaded) return FigureView::Figure;
  return canCallApi ? FigureView::Loading : FigureView::NoSession;
}

// The Balance Codes row's count, or nothing to hide the row. `answered`: the
// fetch returned a list, empty or not.
inline std::optional<std::string> CodeCountText(bool answered, size_t count) {
  if (!answered) return std::nullopt;
  return std::to_string(count);
}

}  // namespace urnw::account_plan
