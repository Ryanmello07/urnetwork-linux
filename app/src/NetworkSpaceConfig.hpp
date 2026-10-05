// The URnetwork network-space configuration and device identity strings,
// shared by the GUI (urnetwork, SdkHost) and the daemon (urnetworkd,
// TunnelHost). Since the daemon-split (linux/MIGRATION.md) the two binaries
// build their NetworkSpace independently — the GUI for the api/auth surface,
// the daemon for its DeviceLocal — and they MUST agree on these values or the
// DeviceRemote would sync against a device registered in a different space.
// The host/env constants and the bootstrap logic live in the SDK-free
// NetworkSpaceBootstrap.hpp (unit-tested); this header binds them to the SDK
// types. Header-only; needs the SDK (both consumers link it) but no GTK/glib.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <unistd.h>

#include <optional>
#include <string>

#include <urnetwork_sdk.hpp>

#include "NetworkSpaceBootstrap.hpp"
#include "StoredNetworkSpace.hpp"

namespace urnw {

// matches the -Dapp_version meson option's default; the release pipeline
// passes the real version
inline constexpr const char* kUrAppVersionFallback = "0.0.0";

// The initial device name defaults to the machine's hostname (the "device
// name"), e.g. "brien-thinkpad", falling back to a generic label. Users can
// still rename their device (a separate, server-side device_name); this is
// only the default a brand-new device registers with.
inline std::string UrDeviceDescription() {
  char hostname[256];
  if (::gethostname(hostname, sizeof(hostname)) == 0 && hostname[0] != '\0') {
    hostname[sizeof(hostname) - 1] = '\0';
    return std::string(hostname);
  }
  return "linux-desktop";
}

inline std::string UrDeviceSpec() {
#if defined(__aarch64__)
  return "linux arm64";
#else
  return "linux amd64";
#endif
}

// Moves a space stored under the retired ur.network/main key to the current
// key (NetworkSpaceBootstrap.hpp). Every manager owner calls this right after
// newNetworkSpaceManager, before it takes any NetworkSpace from the manager.
inline bool MigrateLegacyUrNetworkSpace(urnet::NetworkSpaceManager& manager) {
  return MigrateLegacyUrNetworkSpace<urnet::NetworkSpaceKey>(manager);
}

// Builds (or refreshes) the app's network space in the given manager's
// storage, after the legacy move. Idempotent: updateNetworkSpaceValues
// persists and returns the space for the fixed host/env key.
inline urnet::NetworkSpace BuildUrNetworkSpace(urnet::NetworkSpaceManager& manager) {
  return BootstrapUrNetworkSpace<urnet::NetworkSpaceKey, urnet::NetworkSpaceValues>(manager);
}

// The value set of a host's space that stores nothing yet
// (NetworkSpaceBootstrap.hpp).
inline urnet::NetworkSpaceValues UrNetworkSpaceValues(bool official, const std::string& hostName) {
  return UrNetworkSpaceValues<urnet::NetworkSpaceValues>(official, hostName);
}

// What a space stores, from its toJson() (StoredNetworkSpace.hpp): the start of
// every write that changes some of its values.
inline std::optional<StoredNetworkSpace<urnet::NetworkSpaceKey, urnet::NetworkSpaceValues>>
ParseStoredNetworkSpace(const std::string& spaceJson) {
  return ParseStoredNetworkSpace<urnet::NetworkSpaceKey, urnet::NetworkSpaceValues>(spaceJson);
}

// What the manager stores for a key; empty when it holds no space for it.
inline urnet::NetworkSpaceValues StoredNetworkSpaceValues(const urnet::NetworkSpaceManager& manager,
                                                          const urnet::NetworkSpaceKey& key) {
  return StoredNetworkSpaceValues<urnet::NetworkSpaceKey, urnet::NetworkSpaceValues>(manager, key);
}

}  // namespace urnw
