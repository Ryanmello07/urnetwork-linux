// Pane A's second doors to the statistics pane's sheets (ConnectFold.hpp
// FoldDoorsShown): the page builds them once, ApplyFold shows them exactly
// while pane C is folded, and each opens the same single-instance sheet as
// pane C's own door, under pane C's rules for the DNS editor and the globe.
// The page needs GTK and the SDK, so this reads its source; the rule is
// ConnectFoldTest.cpp's.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadDoorsSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it.
std::string DoorsBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Every needle occurs, in this order.
bool DoorsInSequence(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// Five doors under pane C's title, ahead of the peers list inside "More
// options", each opening its sheet through the page's one opener.
UR_TEST(ConnectFoldDoors_PaneAHasADoorToEverySheet) {
  const std::string pane =
      DoorsBody(ReadDoorsSource("ConnectPage.cpp"), "void ConnectPage::BuildPaneA()");
  UR_EXPECT_TRUE(DoorsInSequence(
      pane, {"foldDoorsHost_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);",
             "T_(\"client_statistics\", \"Client statistics\")",
             "door(T_(\"client_contracts\", \"Client contracts\"), [this] { OpenContractsSheet(); });",
             "door(T_(\"split_rules\", \"Split rules\"), [this] { OpenSplitRulesSheet(); });",
             "door(T_(\"custom_dns\", \"Custom DNS\"), [this] { OpenDnsSheet(); });",
             "door(T_(\"transports\", \"Transports\"), [this] { OpenTransportSheet(); });",
             "T_(\"provider_locations_title\", \"Provider Locations\")",
             "[this] { OpenProviderLocations(); });",
             "foldDoorGlobe_->set_visible(false);",
             "moreOptionsHost_->append(*foldDoorsHost_);",
             "T_(\"network_peers\", \"Network peers\")"}));
}

// ApplyFold shows exactly one set of doors, on the same reading that shows
// pane C.
UR_TEST(ConnectFoldDoors_TheFoldShowsExactlyOneSet) {
  const std::string fold =
      DoorsBody(ReadDoorsSource("ConnectPage.cpp"), "void ConnectPage::ApplyFold(bool force)");
  UR_EXPECT_TRUE(DoorsInSequence(
      fold, {"const int panes = connect_fold::PaneCount(advanced_, paneWidth);",
             "paneC_.root->set_visible(three);",
             "foldDoorsHost_->set_visible(connect_fold::FoldDoorsShown(panes));"}));
}

// The DNS door greys with pane C's edit action, and the globe door shows and
// presses under the provider count row's rule, through the same opener.
UR_TEST(ConnectFoldDoors_TheDoorsFollowPaneCsRules) {
  const std::string page = ReadDoorsSource("ConnectPage.cpp");
  UR_EXPECT_TRUE(DoorsInSequence(DoorsBody(page, "void ConnectPage::ApplyDnsCard()"),
                                 {"dnsEditButton_->set_sensitive(present);",
                                  "foldDoorDns_->set_sensitive(present);"}));
  UR_EXPECT_TRUE(DoorsInSequence(DoorsBody(page, "void ConnectPage::ApplyLiveStatsGroup()"),
                                 {"const bool show = connected || connecting;",
                                  "foldDoorGlobe_->set_visible(show);",
                                  "foldDoorGlobe_->set_sensitive(show && on_open_provider_locations != nullptr);"}));
  UR_EXPECT_TRUE(DoorsInSequence(DoorsBody(page, "void ConnectPage::OpenProviderLocations()"),
                                 {"if (!ConnectedNow() && !ConnectingNow()) return;",
                                  "if (on_open_provider_locations) on_open_provider_locations();"}));
  UR_EXPECT_TRUE(page.find("providerCountLine_->signal_clicked().connect([this] { OpenProviderLocations(); });") !=
                 std::string::npos);
  // one instance of each sheet, whichever door opened it
  UR_EXPECT_TRUE(DoorsInSequence(DoorsBody(page, "void ConnectPage::OpenSplitRulesSheet()"),
                                 {"if (!splitRulesSheet_) {", "splitRulesSheet_->Open();"}));
}
