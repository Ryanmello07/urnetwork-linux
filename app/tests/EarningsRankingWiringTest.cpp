// The Earnings page's own ranking: the rank is the Current Ranking header's
// meta, as on Windows, and the Net Provided row under it is one label and
// one figure. The page needs GTK and the SDK, so this reads its source with
// the comments blanked.
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

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadRankingSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  bool inString = false;
  for (size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (inString) {
      if (c == '\\') {
        ++at;
      } else if (c == '"' || c == '\n') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
      while (at < text.size() && text[at] != '\n') text[at++] = ' ';
    }
  }
  return text;
}

std::string RankingBetween(const std::string& text, const std::string& start,
                           const std::string& end) {
  const size_t from = text.find(start);
  if (from == std::string::npos) return std::string();
  const size_t to = text.find(end, from + start.size());
  return text.substr(from, to == std::string::npos ? std::string::npos : to - from);
}

// Every needle occurs, in this order.
bool RankingInOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// The header carries the rank, shown from the start (the faint dash until a
// rank lands), and the row under it only Net Provided.
UR_TEST(EarningsRankingWiring_TheRankIsTheHeadersMeta) {
  const std::string page = ReadRankingSource("EarningsPage.cpp");
  const std::string block = RankingBetween(
      page, "kit::MakePaneGroupHeader(T_(\"current_ranking\", \"Current Ranking\"))",
      "dataRankingWidgets_.push_back(row.root);");
  UR_EXPECT_TRUE(RankingInOrder(
      block, {"rankValue_ = header.meta;", "rankValue_->set_visible(true);",
              "content->append(*header.root);", "T_(\"net_provided\", \"Net Provided\")",
              "key->set_valign(Gtk::Align::CENTER);", "netProvidedValue_ = MakeStrongValue(18);",
              "grid->append(*netProvidedValue_);"}));
  UR_EXPECT_TRUE(block.find("MakeStrongValue(22)") == std::string::npos);
  UR_EXPECT_TRUE(block.find("figures") == std::string::npos);
  // the dash reads faint in the header as it did as a figure
  UR_EXPECT_TRUE(ReadRankingSource("UrTheme.cpp").find(".ur-pane-meta.ur-label-faint") !=
                 std::string::npos);
}
