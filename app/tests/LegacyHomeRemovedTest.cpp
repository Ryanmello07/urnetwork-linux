// The legacy single-column home ("connect-legacy") is gone. The nav shell kept
// it in its stack, but no destination navigated to it, so only the preview
// harness could show it, and Windows has no such surface. Its connect drawer
// is gone with it; the window owns the two sheets the Connect page opened
// through the drawer (the upgrade sheet and the location chooser). Every page
// the home registers must be one the user can reach: a rail item, or a page a
// destination navigates to (Account's Referrals). The sources need GTK, so
// this reads them.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadLegacyHomeSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Every string literal that follows `call` in `source`, e.g. the tags of
// shell_->SetPage(" calls.
std::set<std::string> LiteralsAfter(const std::string& source, const std::string& call) {
  std::set<std::string> literals;
  for (size_t at = source.find(call); at != std::string::npos; at = source.find(call, at + 1)) {
    const size_t start = at + call.size();
    const size_t end = source.find('"', start);
    if (end != std::string::npos) literals.insert(source.substr(start, end - start));
  }
  return literals;
}

}  // namespace

UR_TEST(LegacyHome_TheWindowBuildsNoLegacyColumnOrDrawer) {
  const std::string window = ReadLegacyHomeSource("MainWindow.cpp");
  const std::string header = ReadLegacyHomeSource("MainWindow.hpp");
  UR_EXPECT_TRUE(!window.empty() && !header.empty());
  for (const char* gone : {"connect-legacy", "ConnectDrawer", "drawer_->", "provideStatsLabel_",
                           "RefreshPeersStatus", "daemonStatusLabel_"}) {
    UR_EXPECT_TRUE_MSG(gone, !Contains(window, gone));
    UR_EXPECT_TRUE_MSG(gone, !Contains(header, gone));
  }
  // the Connect page's location row and every upgrade door open the window's
  // own sheets
  UR_EXPECT_TRUE(Contains(window, "connectPage_->on_open_locations = [this] { OpenLocationChooser(); };"));
  UR_EXPECT_TRUE(Contains(window, "locationsSheet_ = std::make_unique<LocationsSheet>(*this, host_);"));
  UR_EXPECT_TRUE(Contains(window, "upgradeSheet_ = std::make_unique<UpgradeSheet>(*this, host_, balance_);"));
}

UR_TEST(LegacyHome_EveryHomePageIsReachable) {
  const std::string window = ReadLegacyHomeSource("MainWindow.cpp");
  const std::set<std::string> pages = LiteralsAfter(window, "shell_->SetPage(\"");
  UR_EXPECT_TRUE(pages.count("connect") == 1);
  const std::string shell = ReadLegacyHomeSource("HomeShell.cpp");
  std::set<std::string> reachable = LiteralsAfter(shell, "MakeNavItem(navPrimary_, \"");
  for (const std::string& tag : LiteralsAfter(shell, "MakeNavItem(navFooter_, \"")) {
    reachable.insert(tag);
  }
  for (const std::string& tag : LiteralsAfter(window, "Navigate(\"")) reachable.insert(tag);
  UR_EXPECT_TRUE(reachable.count("connect") == 1 && reachable.count("developer") == 1);
  for (const std::string& page : pages) {
    UR_EXPECT_TRUE_MSG(page, reachable.count(page) == 1);
  }
}

UR_TEST(LegacyHome_TheDrawerSourcesAreGone) {
  for (const char* file : {"ConnectDrawer.cpp", "ConnectDrawer.hpp", "IpFamilyStatusRow.cpp",
                           "IpFamilyStatusRow.hpp", "IpFamilyStatus.hpp", "ExtenderPanel.cpp",
                           "ExtenderPanel.hpp", "ExtenderStatusPresentation.hpp"}) {
    std::ifstream in(std::string(UR_SRC_DIR) + "/" + file, std::ios::binary);
    UR_EXPECT_TRUE_MSG(file, !in.good());
  }
  const std::string build = ReadLegacyHomeSource("../meson.build");
  const std::string potfiles = ReadLegacyHomeSource("../po/POTFILES.in");
  UR_EXPECT_TRUE(!build.empty() && !potfiles.empty());
  for (const char* gone : {"ConnectDrawer", "IpFamilyStatus", "ExtenderPanel",
                           "ExtenderStatusPresentation"}) {
    UR_EXPECT_TRUE_MSG(gone, !Contains(build, gone));
    UR_EXPECT_TRUE_MSG(gone, !Contains(potfiles, gone));
  }
}
