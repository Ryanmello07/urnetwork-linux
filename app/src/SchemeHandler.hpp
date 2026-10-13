// Who the desktop starts for a urnetwork:// link. The browser approval sends
// the person to a web page whose answer comes back as urnetwork://auth, so it is
// offered only when that answer would reach this app (contract section 5, rule
// 1): the default handler of x-scheme-handler/urnetwork has to be this app's own
// desktop entry, read back through GIO before the browser is opened. Anything
// else, another program or none at all (a build run from its tree, which
// installs no desktop entry), keeps the paste flow.
//
// Nothing here writes: the packages own the registration
// (packaging/com.bringyour.network.desktop says MimeType=x-scheme-handler/urnetwork;),
// and the app never sets itself as the default or edits mimeapps.list.
//
// The judgment is pure, so tests/SchemeHandlerTest.cpp runs it on any host;
// the read-back is SchemeHandler.cpp's (GIO, GUI only).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace urnw::scheme {

inline constexpr std::string_view kScheme = "urnetwork";
// packaging/com.bringyour.network.desktop (MimeType=x-scheme-handler/urnetwork;), the app id +
// ".desktop"
inline constexpr std::string_view kDesktopId = "com.bringyour.network.desktop";

enum class HandlerState {
  Ours,     // the desktop starts this app's entry for urnetwork:// links
  Foreign,  // it starts another program's
  Absent,   // it starts nothing
};

// The tokens for the log: closed words that are not keys of the store.
constexpr std::string_view ToString(HandlerState state) {
  switch (state) {
    case HandlerState::Ours: return "ours";
    case HandlerState::Foreign: return "foreign";
    case HandlerState::Absent: return "absent";
  }
  return "unknown-handler";
}

// The desktop's default handler for x-scheme-handler/urnetwork, by its desktop id (nullopt: none).
inline HandlerState JudgeHandler(const std::optional<std::string>& defaultId) {
  if (!defaultId || defaultId->empty()) return HandlerState::Absent;
  return *defaultId == kDesktopId ? HandlerState::Ours : HandlerState::Foreign;
}

// The read-back (SchemeHandler.cpp, GIO). Writes nothing: packaging owns the registration.
HandlerState HandlerStatus();

}  // namespace urnw::scheme
