// EarningsPage's destructor must not touch a widget.
//
// The page is destroyed from inside gtk_window_destroy, after the window has
// already disposed the page's children. The destructor once called
// ClosePointsBoard, which rebuilds the points rows (RemoveAllChildren on the
// disposed rows Gtk::Box) and re-renders the header, footer and indicator:
// every quit segfaulted (urnetwork/linux#13). The destructor now only releases
// the controller (ReleasePointsBoard). EarningsPage needs GTK and the SDK, so
// this reads EarningsPage.cpp and checks both bodies instead.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadEarningsPage() {
  std::ifstream in(std::string(UR_SRC_DIR) + "/EarningsPage.cpp");
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of the function whose definition starts with `signature`, or ""
// (braces balanced from the first '{' after the signature).
std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t at = source.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t open = source.find('{', at);
  if (open == std::string::npos) return std::string();
  int depth = 0;
  for (size_t i = open; i < source.size(); ++i) {
    if (source[i] == '{') ++depth;
    if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
  }
  return std::string();
}

// Calls that reach a widget of the points board.
const char* const kWidgetCalls[] = {
    "ClosePointsBoard(",   "RebuildPointsRows(",     "PrependPointsRows(",
    "RemoveAllChildren(",  "RenderPointsHeader(",    "RenderPointsFooter(",
    "UpdatePointsIndicator(", "pointsRows_->",       "pointsBoardStatus_->",
};

}  // namespace

UR_TEST(earningsPageDestructorTouchesNoWidget) {
  const std::string source = ReadEarningsPage();
  if (source.empty()) {
    UR_FAIL("could not read EarningsPage.cpp to check the destructor");
    return;
  }
  const std::string body = FunctionBody(source, "EarningsPage::~EarningsPage()");
  if (body.empty()) {
    UR_FAIL("could not find EarningsPage::~EarningsPage() in EarningsPage.cpp");
    return;
  }
  for (const char* call : kWidgetCalls) {
    UR_EXPECT_TRUE_MSG(std::string("~EarningsPage calls ") + call,
                       body.find(call) == std::string::npos);
  }
  // ...and still closes the points controller on the device
  UR_EXPECT_TRUE_MSG("~EarningsPage no longer releases the points board",
                     body.find("ReleasePointsBoard(/*deviceAlive=*/true)") != std::string::npos);
}

UR_TEST(earningsPageReleasePointsBoardTouchesNoWidget) {
  const std::string source = ReadEarningsPage();
  if (source.empty()) {
    UR_FAIL("could not read EarningsPage.cpp to check ReleasePointsBoard");
    return;
  }
  const std::string body = FunctionBody(source, "void EarningsPage::ReleasePointsBoard(");
  if (body.empty()) {
    UR_FAIL("could not find EarningsPage::ReleasePointsBoard in EarningsPage.cpp");
    return;
  }
  for (const char* call : kWidgetCalls) {
    UR_EXPECT_TRUE_MSG(std::string("ReleasePointsBoard calls ") + call,
                       body.find(call) == std::string::npos);
  }
  UR_EXPECT_TRUE_MSG("ReleasePointsBoard does not close the controller",
                     body.find("closePointsLeaderboardViewController(") != std::string::npos);
}
