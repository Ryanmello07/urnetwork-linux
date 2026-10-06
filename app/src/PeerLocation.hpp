// What a network peer row connects to: the location the chooser's pinned peer
// section (LocationsSheet.cpp) and the Network page's peers group
// (NetworkPage.cpp) build when one of the user's own devices is tapped, and the
// name those rows and the Connect page's location row show for a peer.
//
// A peer row is one of the user's own devices (PeerViewController: connected
// and provide-enabled), so its location carries network_peer = true. The SDK
// then reaches the device as a trusted same-network peer and the connection
// egresses under the Network provide mode (sdk device_local.go). Without the
// flag the SDK takes the client id for a public exit and the connection runs
// under the Public provide mode. android
// (NetworkPeersViewModel.connectLocationForPeer) and apple
// (NetworkPeerItem.toConnectLocation) set it the same way. "Stay on this exit"
// (ProviderLocationsSheet.cpp) also connects to a client id, but to a public
// exit, and leaves it false.
//
// Header-only and free of GTK and the SDK so the unit tests need no vendored
// headers (tests/PeerLocationTest.cpp): the functions are templates over the
// shapes of urnet::NetworkPeer and urnet::ConnectLocation (named and typed as
// in the generated urnetwork_sdk.hpp), and the sheets instantiate them with the
// real types.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {

// A network peer's display name: DeviceName, else DeviceSpec, else the client id.
// Shared with the Connect page's location row.
template <class Peer>
std::string PeerDisplayName(const Peer& peer) {
  if (!peer.DeviceName.empty()) return peer.DeviceName;
  if (!peer.DeviceSpec.empty()) return peer.DeviceSpec;
  return peer.ClientId.value_or(std::string());
}

// The location a tap on a peer row connects to: the peer's client id as the
// location id, its display name, and network_peer set.
template <class Location, class Peer>
Location PeerConnectLocation(const Peer& peer) {
  Location location;
  typename decltype(location.connect_location_id)::value_type id;
  id.client_id = peer.ClientId;
  location.connect_location_id = id;
  location.name = PeerDisplayName(peer);
  location.network_peer = true;
  return location;
}

}  // namespace urnw
