// Which release stream this install follows, and where its updates come from.
//
// THE CHANNEL IS INFERRED FROM THE VERSION STRING, not from a build flag. Our
// CI appends "-beta" to every version it stamps; upstream's build repo does
// not. So the binary already carries the answer and there is nothing extra to
// keep in sync — a beta build cannot be mistaken for an upstream one, and an
// upstream build cannot accidentally ship pointing at the fork.
//
// The inferred value is only the DEFAULT. Settings persists an explicit
// choice, and the explicit choice wins: someone running a beta build who
// switches to Upstream is asking to be told about upstream releases, and that
// is what they get.
//
// ONE RULE ABOUT WHAT THE UI SAYS: the app reports updates for the SELECTED
// channel and nothing else. It never mentions the other stream, never says
// "a beta exists", and never tells an upstream user they are behind a beta.
// A build that is newer than its own channel's latest release simply has no
// update to report and stays silent.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <unistd.h>

#include <string>

#ifndef UR_APP_VERSION
#define UR_APP_VERSION "0.0.0"
#endif

namespace urnw::channel {

enum class Channel {
  Upstream,  // github.com/urnetwork/build — the shipping stream
  Beta,      // github.com/Ryanmello07/urnetwork-linux — versions ending -beta
};

// The suffix our CI appends and upstream's never does.
inline constexpr const char* kBetaSuffix = "-beta";

inline constexpr const char* kUpstreamRepo = "urnetwork/build";
inline constexpr const char* kBetaRepo     = "Ryanmello07/urnetwork-linux";

// Which stream THIS BINARY came from, read off its own version.
inline Channel BuiltChannel() {
  const std::string v = UR_APP_VERSION;
  const std::string suffix = kBetaSuffix;
  if (v.size() >= suffix.size() &&
      v.compare(v.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return Channel::Beta;
  }
  return Channel::Upstream;
}

inline const char* RepoFor(Channel c) {
  return c == Channel::Beta ? kBetaRepo : kUpstreamRepo;
}

// Stable token for AppPrefs and for the Settings picker.
inline const char* NameFor(Channel c) {
  return c == Channel::Beta ? "beta" : "upstream";
}

inline Channel FromName(const std::string& name, Channel fallback) {
  if (name == "beta") return Channel::Beta;
  if (name == "upstream") return Channel::Upstream;
  return fallback;
}

// A release on the beta channel carries the suffix; upstream's does not. Used
// to reject a tag that belongs to the other stream if a repo ever mixes them.
inline bool TagBelongsTo(const std::string& tag, Channel c) {
  const std::string suffix = kBetaSuffix;
  const bool isBeta = tag.size() >= suffix.size() &&
                      tag.compare(tag.size() - suffix.size(), suffix.size(), suffix) == 0;
  return isBeta == (c == Channel::Beta);
}

// CAN THIS INSTALL REPLACE ITS OWN APP BINARY? Inside a Flatpak the answer is
// no, and the update mechanism is Flatpak itself — Flathub for a store install,
// `flatpak update` for a sideloaded bundle. The sandbox cannot tell those two
// apart (/.flatpak-info carries no origin; only the host's `flatpak info`
// knows), so both are treated the same: no app-update reporting, because
// offering an update the user cannot take from inside the app is noise.
//
// The SERVICE check is unaffected — urnetworkd lives on the host, is installed
// by the user, and genuinely can be out of date whatever ships the GUI.
inline bool AppUpdatesAreOurs() {
#ifdef __linux__
  return ::access("/.flatpak-info", F_OK) != 0;
#else
  return true;
#endif
}

}  // namespace urnw::channel
