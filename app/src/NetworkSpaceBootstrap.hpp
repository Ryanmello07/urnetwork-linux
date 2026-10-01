// The official network space's identity and the launch-time bootstrap that
// puts it in a NetworkSpaceManager's storage: WHICH host/env key the bundled
// space lives under, the values it carries, and the one-time move of a space
// stored under the retired key. Pure and SDK-free on purpose (templated over
// the SDK's NetworkSpaceKey / NetworkSpaceValues / NetworkSpaceManager shapes)
// so the unit test binary, which links no SDK, pins the constants and the
// ORDER of the bootstrap. NetworkSpaceConfig.hpp instantiates this with the
// real SDK types for the GUI and the daemon.
//
// History: the operator's planned move to *.ur.network was cancelled; the
// operator stays bringyour.com. Builds up to v2026.9.19 keyed the bundled space
// "ur.network"/"main" (deriving api.ur.network, a host that was never stood up)
// with bringyour.com as its migration host. A space stored under that key
// holds the device's jwt and local state, so every launch first moves it to
// the current key -- idempotent: the SDK's migrateNetworkSpace is a no-op when
// the source is absent or the destination already exists -- and only then
// builds or refreshes the space, before any Device is constructed or a
// NetworkSpace held.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {

// The operator's network space key. bringyour.com derives api.bringyour.com
// and connect.bringyour.com with no migration host.
inline constexpr const char* kUrHostName = "bringyour.com";
// The retired key from the cancelled *.ur.network migration. Only the
// launch-time move names it; nothing derives a URL from it.
inline constexpr const char* kUrLegacyHostName = "ur.network";
inline constexpr const char* kUrEnvName = "main";
// The web app and link host (sign-in, checkout, referrals): a site, not the
// operator's api/connect host.
inline constexpr const char* kUrLinkHostName = "ur.io";

template <class Key>
inline Key UrNetworkSpaceKey(const std::string& hostName) {
  Key key;
  key.host_name = hostName;
  key.env_name = std::string(kUrEnvName);
  return key;
}

// The value set of the official space (official == true: the bundled space,
// pinned endpoints, ur.io links) or of a self-hosted deployment under
// `hostName` (deriving everything off its own name). Neither carries a
// migration host: the operator stays on its host.
template <class Values>
inline Values UrNetworkSpaceValues(bool official, const std::string& hostName) {
  Values values;
  values.bundled = official;
  values.net_expose_server_ips = true;
  values.net_expose_server_host_names = true;
  values.link_host_name = official ? std::string(kUrLinkHostName) : hostName;
  values.migration_host_name = std::string();
  values.wallet = "circle";
  values.sso_google = false;
  return values;
}

// Moves a space stored under the retired ur.network/main key to
// bringyour.com/main. Returns what the manager returns: true when a space was
// moved, false when there was nothing to move or the destination already
// exists. Call it on every launch, where the manager is created, BEFORE any
// NetworkSpace is taken from the manager.
template <class Key, class Manager>
inline bool MigrateLegacyUrNetworkSpace(Manager& manager) {
  return manager.migrateNetworkSpace(UrNetworkSpaceKey<Key>(kUrLegacyHostName),
                                     UrNetworkSpaceKey<Key>(kUrHostName));
}

// The launch bootstrap of the official space: the legacy move, then the
// build/refresh of the space under the current key. Idempotent
// (updateNetworkSpaceValues persists and returns the space for the fixed key).
template <class Key, class Values, class Manager>
inline auto BootstrapUrNetworkSpace(Manager& manager) {
  MigrateLegacyUrNetworkSpace<Key>(manager);
  return manager.updateNetworkSpaceValues(UrNetworkSpaceKey<Key>(kUrHostName),
                                          UrNetworkSpaceValues<Values>(true, kUrHostName));
}

}  // namespace urnw
