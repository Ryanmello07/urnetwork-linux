// Who the desktop starts for a urnetwork:// link (SchemeHandler.hpp): the
// browser approval is offered only when it is this app's own desktop entry,
// which the packages install and nothing in the app writes. The judgment is
// pure; the read-back needs GIO, so the wiring cases read SchemeHandler.cpp
// with its comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <optional>
#include <set>
#include <sstream>
#include <string>

#include "InstanceHandover.hpp"
#include "SchemeHandler.hpp"
#include "WiringSource.hpp"

using urnw::scheme::HandlerState;
using urnw::scheme::JudgeHandler;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;
using urnw::testing::wiring::ReadRaw;

UR_TEST(SchemeHandler_OnlyTheAppsOwnEntryIsOurs) {
  UR_EXPECT_TRUE(JudgeHandler(std::nullopt) == HandlerState::Absent);
  UR_EXPECT_TRUE(JudgeHandler(std::string()) == HandlerState::Absent);
  UR_EXPECT_TRUE(JudgeHandler(std::string("com.bringyour.network.desktop")) == HandlerState::Ours);
  UR_EXPECT_TRUE(JudgeHandler(std::string("org.mozilla.firefox.desktop")) == HandlerState::Foreign);
  // the bus name without the suffix is not a desktop id, and neither is a near miss
  UR_EXPECT_TRUE(JudgeHandler(std::string("com.bringyour.network")) == HandlerState::Foreign);
  UR_EXPECT_TRUE(JudgeHandler(std::string("com.bringyour.network.desktop ")) ==
                 HandlerState::Foreign);
  UR_EXPECT_TRUE(JudgeHandler(std::string("Com.bringyour.network.desktop")) ==
                 HandlerState::Foreign);
  UR_EXPECT_TRUE(JudgeHandler(std::string("urnetwork.desktop")) == HandlerState::Foreign);
}

// The id is the app id the instance handover owns on the bus, plus ".desktop", and the packages'
// desktop entry claims the scheme.
UR_TEST(SchemeHandler_TheDesktopIdIsThePackagedEntry) {
  UR_EXPECT_TRUE(urnw::scheme::kDesktopId == std::string(urnw::instance::kBusName) + ".desktop");
  UR_EXPECT_TRUE(urnw::scheme::kScheme == "urnetwork");
  const std::string entry = ReadRaw("../packaging/com.bringyour.network.desktop");
  UR_EXPECT_TRUE_MSG("packaging/com.bringyour.network.desktop was read", !entry.empty());
  UR_EXPECT_TRUE(Contains(entry, "\nMimeType=x-scheme-handler/urnetwork;"));
  UR_EXPECT_TRUE(Contains(entry, "\nExec=urnetwork %u\n"));
}

// The read-back asks GIO for the default handler and writes nothing: no default is set, no
// mimeapps.list or desktop file is edited.
UR_TEST(SchemeHandler_TheReadBackWritesNothing) {
  const std::string code = ReadCode("SchemeHandler.cpp");
  const std::string body = FunctionBody(code, "HandlerState HandlerStatus()");
  UR_EXPECT_TRUE_MSG("HandlerStatus", !body.empty());
  UR_EXPECT_TRUE(
      Contains(body, "g_app_info_get_default_for_uri_scheme(std::string(kScheme).c_str())"));
  UR_EXPECT_TRUE(Contains(body, "g_app_info_get_id(app)"));
  UR_EXPECT_TRUE(Contains(body, "g_object_unref(app);"));
  UR_EXPECT_TRUE(Contains(body, "return JudgeHandler(id);"));
  for (const char* write : {"set_as_default", "mimeapps", "g_file_set_contents", "g_key_file_save",
                            "g_file_replace", "xdg-mime", "g_spawn", "update-desktop-database"}) {
    UR_EXPECT_TRUE_MSG(std::string("SchemeHandler.cpp has ") + write, !Contains(code, write));
  }
  // the header is the pure judgment
  const std::string header = ReadCode("SchemeHandler.hpp");
  for (const char* gio : {"<gio", "<glib", "g_app_info"}) {
    UR_EXPECT_TRUE_MSG(std::string("SchemeHandler.hpp has ") + gio, !Contains(header, gio));
  }
}

// The log tokens are closed, distinct, and no key of the store.
UR_TEST(SchemeHandler_TheTokensAreClosed) {
  std::set<std::string> keys;
  std::istringstream lines(ReadRaw("../po/linux-keys.txt"));
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] != '#') keys.insert(line);
  }
  UR_EXPECT_TRUE_MSG("po/linux-keys.txt was read", keys.size() > 500);
  std::set<std::string> seen;
  for (const HandlerState state :
       {HandlerState::Ours, HandlerState::Foreign, HandlerState::Absent}) {
    const std::string token(urnw::scheme::ToString(state));
    seen.insert(token);
    bool lowercase = !token.empty();
    for (const char c : token) lowercase = lowercase && ((c >= 'a' && c <= 'z') || c == '-');
    UR_EXPECT_TRUE_MSG("the token '" + token + "' is lowercase letters and hyphens", lowercase);
    UR_EXPECT_TRUE_MSG("the token '" + token + "' is a key of the store", keys.count(token) == 0);
  }
  UR_EXPECT_EQ(std::size_t{3}, seen.size());
  UR_EXPECT_TRUE(urnw::scheme::ToString(HandlerState::Ours) == "ours");
  UR_EXPECT_TRUE(urnw::scheme::ToString(HandlerState::Foreign) == "foreign");
  UR_EXPECT_TRUE(urnw::scheme::ToString(HandlerState::Absent) == "absent");
}
