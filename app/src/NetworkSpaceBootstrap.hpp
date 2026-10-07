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
// The refresh writes the official values OVER what the space already stores
// (StoredNetworkSpace.hpp): updateNetworkSpaceValues replaces the whole value
// set, and a refresh built from nothing wiped, on every launch, what the user
// had saved in the space -- its VLESS server and its private extender. The GUI
// then binds the space the user last chose, which may be another server's
// (LaunchUrNetworkSpace).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

#include "StoredNetworkSpace.hpp"

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

// The values of the official space (official == true: the bundled space,
// pinned endpoints, ur.io links) or of a self-hosted deployment under
// `hostName` (deriving everything off its own name), written over `values` --
// what the space stores already. Every value they do not name is kept as it
// is: the VLESS server, the private extender, the extender overrides, alt_url,
// sn_chain. Neither carries a migration host: the operator stays on its host.
template <class Values>
inline Values UrNetworkSpaceValuesOver(Values values, bool official, const std::string& hostName) {
  values.bundled = official;
  values.net_expose_server_ips = true;
  values.net_expose_server_host_names = true;
  values.link_host_name = official ? std::string(kUrLinkHostName) : hostName;
  values.migration_host_name = std::string();
  values.wallet = "circle";
  values.sso_google = false;
  return values;
}

// The same values for a space that stores nothing yet.
template <class Values>
inline Values UrNetworkSpaceValues(bool official, const std::string& hostName) {
  return UrNetworkSpaceValuesOver(Values{}, official, hostName);
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

// The build/refresh of the official space under the current key: the official
// values over what that space stores. The bundled space has no url overrides,
// so those are the one stored value the refresh clears (as a refresh from
// nothing always did). Idempotent (updateNetworkSpaceValues persists and
// returns the space for the fixed key).
template <class Key, class Values, class Manager>
inline auto RefreshUrNetworkSpace(Manager& manager) {
  const Key key = UrNetworkSpaceKey<Key>(kUrHostName);
  Values values = UrNetworkSpaceValuesOver(StoredNetworkSpaceValues<Key, Values>(manager, key),
                                           true, kUrHostName);
  values.api_url.reset();
  values.platform_url.reset();
  return manager.updateNetworkSpaceValues(key, values);
}

// The launch bootstrap of the official space: the legacy move, then the
// refresh, which reads what the space stores after the move so a moved space
// keeps what it carried.
template <class Key, class Values, class Manager>
inline auto BootstrapUrNetworkSpace(Manager& manager) {
  MigrateLegacyUrNetworkSpace<Key>(manager);
  return RefreshUrNetworkSpace<Key, Values>(manager);
}

// The space the GUI binds to at launch (android installBundleNetworkSpace,
// apple NetworkSpaceStartup.prepareBundledNetworkSpace parity): the bootstrap
// above, then the space the manager persisted as active -- the server the user
// last applied in the network sheet, which ApplyNetworkServer makes active and
// the manager records in its storage. The bundled space is made active only
// when it was just created (a first launch) or nothing is active; any other
// launch keeps what the user chose. Binding the bundled space on every launch
// brought a user on a custom server back on bringyour.com: signed out, since
// the jwt lives in the chosen space's own state, and on the wrong network's
// api and connect urls. The bundled space is the fallback when the manager
// answers no active space at all, so the app is never bound to none.
//
// The GUI's alone: the daemon builds its DeviceLocal in the space start_tunnel
// hands it, and without one in the bundled space (BuildUrNetworkSpace).
template <class Key, class Values, class Manager>
inline auto LaunchUrNetworkSpace(Manager& manager) {
  MigrateLegacyUrNetworkSpace<Key>(manager);
  // sampled before the refresh, which creates the bundled space when it is
  // missing -- asking afterwards would always answer that it existed
  const bool bundledExisted =
      static_cast<bool>(manager.getNetworkSpace(UrNetworkSpaceKey<Key>(kUrHostName)));
  auto bundled = RefreshUrNetworkSpace<Key, Values>(manager);
  if (bundled && (!bundledExisted || !manager.getActiveNetworkSpace())) {
    manager.setActiveNetworkSpace(bundled);
  }
  // returned by name, each: the SDK's handle is move-only
  auto active = manager.getActiveNetworkSpace();
  if (active) return active;
  return bundled;
}

// URNETWORK_NETWORK_HOST (and URNETWORK_NETWORK_ENV, "main" when unset): a
// launch pointed at another backend, a test network a throwaway account can
// run the success paths against. Each value is trimmed of spaces, tabs and
// line ends (a script's value often ends in its newline), and a host that is
// empty once trimmed is no override, as an env that is empty is "main".
struct LaunchOverride {
  std::string host;
  std::string env;
  bool Active() const { return !host.empty(); }
};

inline std::string TrimmedLaunchValue(const char* value) {
  if (value == nullptr) return std::string();
  constexpr const char* kBlank = " \t\r\n";
  const std::string text(value);
  const size_t first = text.find_first_not_of(kBlank);
  if (first == std::string::npos) return std::string();
  return text.substr(first, text.find_last_not_of(kBlank) - first + 1);
}

inline LaunchOverride ResolveLaunchOverride(const char* host, const char* env) {
  LaunchOverride out;
  out.host = TrimmedLaunchValue(host);
  if (!out.Active()) return out;
  out.env = TrimmedLaunchValue(env);
  if (out.env.empty()) out.env = kUrEnvName;
  return out;
}

// The env a space for `host` is keyed under: the override's for the
// override's own host, so "Use default network" is the network the process
// started on, env and all; "main" for every other host.
inline std::string EnvNameFor(const LaunchOverride& launchOverride, const std::string& host) {
  return launchOverride.Active() && host == launchOverride.host ? launchOverride.env
                                                                : std::string(kUrEnvName);
}

// Whether {host, env} is the override's space, which is bound for this
// process only: a write to it must not make it the active space, or a launch
// without the override would stay on the test network.
inline bool IsLaunchOverrideSpace(const LaunchOverride& launchOverride, const std::string& host,
                                  const std::string& env) {
  return launchOverride.Active() && host == launchOverride.host && env == launchOverride.env;
}

// The launch with an override in force: the official space's bootstrap runs
// as ever, then the override's space is built under {host, env}, its urls
// derived from them (no explicit urls, no migration host) over what it
// stores, and bound for this process only. It is not made active, so a launch
// without the override binds the user's own choice again. Without an
// override, LaunchUrNetworkSpace above.
template <class Key, class Values, class Manager>
inline auto LaunchUrNetworkSpace(Manager& manager, const LaunchOverride& launchOverride) {
  if (!launchOverride.Active()) return LaunchUrNetworkSpace<Key, Values>(manager);
  BootstrapUrNetworkSpace<Key, Values>(manager);
  Key key;
  key.host_name = launchOverride.host;
  key.env_name = launchOverride.env;
  const bool official = launchOverride.host == kUrHostName && launchOverride.env == kUrEnvName;
  Values values = UrNetworkSpaceValuesOver(StoredNetworkSpaceValues<Key, Values>(manager, key),
                                           official, launchOverride.host);
  values.api_url.reset();
  values.platform_url.reset();
  return manager.updateNetworkSpaceValues(key, values);
}

}  // namespace urnw
