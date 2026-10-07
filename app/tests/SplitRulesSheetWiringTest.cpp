// The split rules sheet's activity search and its amber Local chips. The
// sheet needs GTK and the SDK, so this reads its source; the match itself is
// ConnectionFilter.hpp's QueryPasses, which ConnectionFilterTest.cpp covers.
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

std::string ReadSheetSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it.
std::string SheetBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Every needle occurs, in this order.
bool SheetInSequence(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// The explainer leads, the search sits between the Activity header and the
// list, and typing rebuilds the activity alone.
UR_TEST(SplitRulesSheet_TheActivityIsSearchable) {
  const std::string sheet = ReadSheetSource("SplitRulesSheet.cpp");
  const std::string build = SheetBody(sheet, "SplitRulesSheet::SplitRulesSheet(");
  UR_EXPECT_TRUE(SheetInSequence(build, {"T_(\"adv_split_rules_note\"", "T_(\"split_rules_info_note\"",
                                         "content->append(rulesBox_);", "T_(\"activity\", \"Activity\")",
                                         "T_(\"adv_search_activity_placeholder\", \"Search activity\")",
                                         "activitySearch_.signal_search_changed().connect([this] { RebuildActivity(); });",
                                         "content->append(activitySearch_);",
                                         "content->append(activityBox_);"}));
  UR_EXPECT_TRUE(SheetInSequence(SheetBody(sheet, "void SplitRulesSheet::Open()"),
                                 {"activitySearch_.set_text(\"\");", "Refresh();"}));
  const std::string activity = SheetBody(sheet, "void SplitRulesSheet::RebuildActivity()");
  UR_EXPECT_TRUE(SheetInSequence(
      activity, {"if (actions_.empty()) {", "return;",
                 "connection_filter::NormalizeQuery(activitySearch_.get_text());",
                 "if (!connection_filter::QueryPasses(query, decision)) continue;", "++shown;",
                 "if (shown == 0) {",
                 "T_(\"adv_no_activity_matches\", \"No activity matches this search.\")"}));
  // the rules are never filtered
  UR_EXPECT_TRUE(SheetBody(sheet, "void SplitRulesSheet::RebuildRules()").find("activitySearch_") ==
                 std::string::npos);
}

// Local means bypassed the tunnel: amber on both the rule rows and the
// activity rows, as the connect page's verdict dots paint it; matched hosts
// stay green.
UR_TEST(SplitRulesSheet_LocalChipsAreAmber) {
  const std::string sheet = ReadSheetSource("SplitRulesSheet.cpp");
  UR_EXPECT_TRUE(SheetBody(sheet, "void SplitRulesSheet::RebuildRules()")
                     .find("MakeChip(T_(\"local\", \"Local\"), \"amber\", true)") != std::string::npos);
  const std::string activity = SheetBody(sheet, "void SplitRulesSheet::RebuildActivity()");
  UR_EXPECT_TRUE(activity.find("action.local ? \"amber\" : \"muted\"") != std::string::npos);
  UR_EXPECT_TRUE(activity.find("MakeChip(name, \"green\", true)") != std::string::npos);
  UR_EXPECT_TRUE(sheet.find("\"green\", action.hasRouteOverride") == std::string::npos);
  const std::string css = ReadSheetSource("Ui.cpp");
  UR_EXPECT_TRUE(css.find(".ur-chip-amber { color: #F5C242;") != std::string::npos);
  UR_EXPECT_TRUE(css.find(".ur-chip-amber-hi { color: #101010; background-color: #F5C242; }") !=
                 std::string::npos);
}
