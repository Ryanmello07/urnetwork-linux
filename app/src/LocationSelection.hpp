// Which connect location is selected, asked one way everywhere: the location
// chooser's rows (LocationsSheet.cpp), the Network page's rows and selected
// location (NetworkPage.cpp), the Connect page's provider row
// (ConnectPage.cpp), the Connect button's target (MainWindow::StartTunnelUi)
// and the row-click coalescer's re-click (SdkHost::ConnectFromRow). Windows
// exports the same predicates from SdkHost.h so the page, the sheet and the
// coalescer cannot disagree.
//
// A selection is the SDK's ConnectLocation: no selection and a best-available
// one are both "best available", and two locations are the same when any of
// their location, client or location group ids match.
//
// Header-only and free of GTK and the SDK so the unit tests need no vendored
// headers (tests/LocationSelectionTest.cpp): the functions are templates over
// the shapes of urnet::ConnectLocation and urnet::NetworkPeer, named and typed
// as in the generated urnetwork_sdk.hpp.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <optional>
#include <string>

namespace urnw {

// Both ids are set, not empty, and equal.
inline bool SameLocationId(const std::optional<std::string>& a,
                           const std::optional<std::string>& b) {
  return a && b && !a->empty() && *a == *b;
}

// No selection, or one flagged best available.
template <class Location>
bool IsBestAvailableSelected(const std::optional<Location>& selected) {
  return !selected || (selected->connect_location_id &&
                       selected->connect_location_id->best_available.value_or(false));
}

// The selection is this network peer (by its client id).
template <class Location, class Peer>
bool IsPeerSelected(const std::optional<Location>& selected, const Peer& peer) {
  if (!selected || !selected->connect_location_id) return false;
  return SameLocationId(selected->connect_location_id->client_id, peer.ClientId);
}

// The selection is this location (by any of its ids).
template <class Location>
bool IsLocationSelected(const std::optional<Location>& selected, const Location& location) {
  if (!selected || !selected->connect_location_id || !location.connect_location_id) return false;
  const auto& a = *selected->connect_location_id;
  const auto& b = *location.connect_location_id;
  return SameLocationId(a.location_id, b.location_id) ||
         SameLocationId(a.client_id, b.client_id) ||
         SameLocationId(a.location_group_id, b.location_group_id);
}

// The selection is this connect target (a row's location, or none for the
// best-available row): both best available, or the same location.
template <class Location>
bool IsTargetSelected(const std::optional<Location>& selected,
                      const std::optional<Location>& target) {
  if (IsBestAvailableSelected(target)) return IsBestAvailableSelected(selected);
  return IsLocationSelected(selected, *target);
}

}  // namespace urnw
