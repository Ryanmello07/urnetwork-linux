// One connected provider as rendered by the globe and the provider-locations
// list, plus the pure label/selection helpers over it.
//
// Toolkit- and SDK-independent (plain C++17) so the ordering, the label
// composition, the override-target selection, "Stay on this exit" and the
// extenders' pairing and rings are unit testable standalone -- see
// tests/ProviderLocationRowTest.cpp. The sheet maps urnet::ConnectedProviderLocation
// onto this; nothing here knows about the SDK.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ExtenderRingGeometry.hpp"

namespace urnw {

// One extender on a provider's route: its address, which is the extender's id
// on the provider rows (connect/EXTENDER.md K9), and its color (K3), paired by
// the SDK.
struct ProviderExtender {
  std::string ip;
  std::string colorHex;  // the SDK's GetExtenderColorHex(ip), drawn as given

  bool operator==(const ProviderExtender& other) const {
    return ip == other.ip && colorHex == other.colorHex;
  }
  bool operator!=(const ProviderExtender& other) const { return !(*this == other); }
};

struct ProviderLocationRow {
  std::string clientId;  // the EGRESS provider client id (what is displayed/copied)
  std::string country;
  std::string countryCode;  // lowercase; feeds the SDK color palette
  std::string region;
  std::string city;
  bool hasLocation = false;
  // the coordinates to plot: the city centroid when known, else the region
  // centroid. hasCoordinates is false when the provider has neither.
  bool hasCoordinates = false;
  double lat = 0;
  double lon = 0;
  int64_t connectedSinceMillis = 0;
  // The provider's proven address-family category as the SDK labels it:
  // "both" (dualstack), "v4" or "v6" (connect/IPV6.md D2). Empty from an
  // older peer that predates the field; IpFamilyTag reads that as v4, which
  // is what such a provider carries.
  std::string ipFamilyLabel;
  // The extenders carrying this client's connection to the provider right now
  // (connect/EXTENDER.md K1), in the SDK's order, which is the order of the
  // dot's rings from the outside in: none over a direct or p2p route, one
  // through an extender, briefly two across a transport migration.
  std::vector<ProviderExtender> extenders;

  // Providers with no coordinates are listed but never plotted.
  bool plottable() const { return hasCoordinates; }

  // A transport migration changes only the extenders, so they are part of the
  // value: an equal row is not re-rendered.
  bool operator==(const ProviderLocationRow& other) const {
    return clientId == other.clientId && country == other.country &&
           countryCode == other.countryCode && region == other.region && city == other.city &&
           hasLocation == other.hasLocation && hasCoordinates == other.hasCoordinates &&
           lat == other.lat && lon == other.lon &&
           connectedSinceMillis == other.connectedSinceMillis &&
           ipFamilyLabel == other.ipFamilyLabel && extenders == other.extenders;
  }
  bool operator!=(const ProviderLocationRow& other) const { return !(*this == other); }
};

// "City, Region, Country" -- omitting whichever parts the server does not know.
// Empty when nothing is known; the caller substitutes the localized
// "Location unknown".
std::string PlaceLabel(const ProviderLocationRow& row);

// "37.7749, -122.4194" at 4 decimal places, or an em dash when the provider has
// no coordinates.
std::string CoordinatesLabel(const ProviderLocationRow& row);

// The row's address-family tag: exactly one of "both", "v4", "v6". A label
// this build does not know (or none at all -- an older peer) reads as "v4",
// so a provider that is carrying traffic is never tagged as carrying nothing.
std::string IpFamilyTag(const ProviderLocationRow& row);

// ---- extenders --------------------------------------------------------------
// The SDK carries a provider's extenders on ConnectedProviderLocation as
// ExtenderIps and ExtenderColorHexes, comma separated in the same order
// (EXTENDER.md K1, K3).

// Pairs the two lists by index. The SDK keeps them the same length; if they
// ever differ, the unpaired tail is dropped rather than drawn in a color this
// app made up. (The connect canvas keeps every ip instead, extender::PairColors,
// since a grid ring only counts; a row names each extender by its pair.) Empty
// for a direct or p2p route.
std::vector<ProviderExtender> PairProviderExtenders(const std::string& ips,
                                                    const std::string& colorHexes);

// The row's extender colors in the SDK's order, outermost ring first.
std::vector<std::string> ExtenderColorHexes(const ProviderLocationRow& row);

// The list row's dot, in px: the country-color dot in the sheet's fixed-size
// column, outside which the selection ring sits.
inline constexpr double kRowDotDiameter = 20.0;

// The row dot's extender rings (EXTENDER.md K2) at the dot's own size, which
// holds two; a third extender dashes the second, as a second one dashes the
// globe dot's only ring (globe::DotRings).
extender::Rings RowDotRings(const ProviderLocationRow& row);

// The oldest connected provider that has coordinates -- the location-override
// target. Found by the smallest non-zero connectedSinceMillis, NOT by position:
// these rows come in the view controller's display order (west to east), which
// says nothing about connected duration. Providers with neither city nor region
// coordinates are skipped rather than ending the search, and a provider with an
// unknown stamp (0) only wins when nothing else is plottable. Returns -1 when
// there is none.
int OldestPlottableIndex(const std::vector<ProviderLocationRow>& rows);

// ---- "Stay on this exit" ----------------------------------------------------
// Reconnect to one provider of the current connection, by its client id, so new
// connections keep that provider's IP address. The SDK dials a client id
// location directly (connect's fixed destination: nothing is discovered and
// nothing replaces it), and the location is not marked as a network peer, so
// the provider keeps carrying the traffic as the public exit it already is. The
// rows are the user's own current exits, so this pins one of them; it is not a
// way to browse or pick from all providers.

// What a provider row shows for "Stay on this exit".
enum class StayOnExitState {
  None,
  Offer,    // the selected row offers the action
  Staying,  // the connection already stays on this provider
};

// The selected row offers to stay on its provider; the provider the connection
// already stays on says so instead, selected or not. `stayingClientId` is the
// client id of the current location when it is a client id location (a stayed
// exit or a network peer), else empty. Ids compare ignoring ASCII case.
StayOnExitState StayOnExitStateFor(const ProviderLocationRow& row,
                                   const std::string& selectedClientId,
                                   const std::string& stayingClientId);

// "018f…5c6d": the first and last four characters of a client id (the form the
// android connect drawer shows a client id location in). A short id is
// returned as it is.
std::string ShortClientId(const std::string& clientId);

// "018f…5c6d · Berlin, Germany": the short client id, which is what makes the
// location one provider, then the city (or the region) and the country. The id
// comes first so a narrow drawer trims the place rather than the id. Just the
// short id when the server does not know where the provider is.
std::string StayOnExitName(const ProviderLocationRow& row);

// What "Stay on this exit" connects to, field for field what the sheet copies
// into the SDK's ConnectLocation (this header stays SDK-free): the provider's
// client id as the location id, the name above, and the place. The sheet sets
// network_peer false.
struct StayOnExitTarget {
  std::string clientId;
  std::string name;
  std::string city;
  std::string region;
  std::string country;
  std::string countryCode;

  bool operator==(const StayOnExitTarget& other) const {
    return clientId == other.clientId && name == other.name && city == other.city &&
           region == other.region && country == other.country &&
           countryCode == other.countryCode;
  }
};
// nullopt when the row has no client id
std::optional<StayOnExitTarget> MakeStayOnExitTarget(const ProviderLocationRow& row);

// The list's order (the plottable providers west to east about their centroid,
// then the ones with no coordinates) and the globe's clamped stepping over it
// are NOT here: they live in the SDK's shared ProviderLocationsViewController,
// which every URnetwork app binds -- see SdkHost::ConnectedProviderLocations
// and SdkHost::StepProviderSelection.

}  // namespace urnw
