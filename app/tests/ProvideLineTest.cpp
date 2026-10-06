// The connect page's provide line (ProvideLine.hpp, ProvideLineFor), with
// Windows' rule: the client count while one was given, "Providing (paused)"
// while paused, and nothing while the device does not provide or the count is
// unknown, which is the provider-only device as a daemon that predates its
// client count reports it. The page itself needs GTK and the SDK, so the last
// case reads its source: the line sits under the discoverable line, as on
// Windows, and is drawn from this rule and the store's keys.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "ProvideLine.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

using urnw::ProvideLine;
using urnw::ProvideLineFor;

namespace {

std::string ReadProvideLineSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it.
std::string ProvideLineBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// `first` occurs, and before `second` (which must occur too).
bool InOrder(const std::string& text, const std::string& first, const std::string& second) {
  const size_t a = text.find(first);
  const size_t b = text.find(second);
  return a != std::string::npos && b != std::string::npos && a < b;
}

}  // namespace

UR_TEST(ProvideLineCountsTheClientsItWasGiven) {
  UR_EXPECT_TRUE(ProvideLineFor(true, false, false) == ProvideLine::Clients);
}

UR_TEST(ProvideLineIsLeftOutWhileTheCountIsUnknown) {
  // never "Providing to 0 clients", and paused or not, as on Windows
  UR_EXPECT_TRUE(ProvideLineFor(true, false, true) == ProvideLine::None);
  UR_EXPECT_TRUE(ProvideLineFor(true, true, true) == ProvideLine::None);
}

UR_TEST(ProvideLineSaysPausedWhilePaused) {
  UR_EXPECT_TRUE(ProvideLineFor(true, true, false) == ProvideLine::Paused);
}

UR_TEST(ProvideLineIsLeftOutWhileNotProviding) {
  for (const bool paused : {false, true}) {
    for (const bool clientsUnknown : {false, true}) {
      UR_EXPECT_TRUE(ProvideLineFor(false, paused, clientsUnknown) == ProvideLine::None);
    }
  }
}

UR_TEST(ProvideLineIsOnTheConnectPageUnderTheDiscoverableLine) {
  const std::string page = ReadProvideLineSource("ConnectPage.cpp");
  // built hidden, right after the discoverable line and before the extender row
  UR_EXPECT_TRUE(InOrder(page, "moreOptionsHost_->append(*discoverableText_);",
                         "provideStatsText_ = Gtk::make_managed<Gtk::Label>();"));
  UR_EXPECT_TRUE(InOrder(page, "provideStatsText_->set_visible(false);",
                         "moreOptionsHost_->append(*provideStatsText_);"));
  UR_EXPECT_TRUE(InOrder(page, "moreOptionsHost_->append(*provideStatsText_);",
                         "moreOptionsHost_->append(*extenderRow_);"));
  // drawn from the rule and the store's keys on every stats push, and
  // collapsed when there is nothing to say
  const std::string apply = ProvideLineBody(page, "void ConnectPage::ApplyStats(");
  UR_EXPECT_TRUE(InOrder(apply, "ProvideLineFor(stats.provideEnabled, stats.providePaused,",
                         "stats.provideClientsUnknown)"));
  UR_EXPECT_TRUE(InOrder(apply, "case ProvideLine::Paused:",
                         "T_(\"providing_paused\", \"Providing (paused)\")"));
  UR_EXPECT_TRUE(InOrder(apply, "case ProvideLine::Clients:", "TN_(\"providing_client_count\""));
  UR_EXPECT_TRUE(InOrder(apply, "stats.provideClients", "kit::SetTextOrCollapse(*provideStatsText_, provide);"));
}
