// The signed-in shell's width (HomeShell, ShellLayout.hpp): the window can be
// narrowed to the widths the pages fold at, because the shell's page stack is
// sized by the page on screen rather than by the widest of its seven pages;
// the nav rail is the expanded one from 1008 dip and the compact icon rail at
// every narrower width, as Windows has it, with no mode that hides it; and the
// status strip spans the window under both. HomeShell and MainWindow need
// gtkmm, so the wiring cases read their sources with the comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ShellLayout.hpp"
#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadShellSource(const std::string& relative) {
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

std::string ShellBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool ShellHas(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Each needle occurs after the one before it.
bool ShellInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

}  // namespace

// A homogeneous stack is as wide as its widest page, whichever is shown: the
// Account page's wide layout held every destination at 1045.
UR_TEST(ShellLayout_TheStackIsSizedByThePageOnScreen) {
  const std::string shell = ReadShellSource("HomeShell.cpp");
  const std::string build = ShellBody(shell, "HomeShell::HomeShell()");
  UR_EXPECT_TRUE(!build.empty());
  UR_EXPECT_TRUE(ShellHas(build, "stack_.set_hhomogeneous(false);"));
}

// Expanded from 1008, compact below it down to the 400 dip minimum window:
// the 641 dip boundary of WinUI's minimal mode is gone.
UR_TEST(ShellLayout_TheRailIsCompactBelow1008AtEveryWidth) {
  using urnw::shell::NavRailCompact;
  UR_EXPECT_TRUE(NavRailCompact(400));
  UR_EXPECT_TRUE(NavRailCompact(480));
  UR_EXPECT_TRUE(NavRailCompact(640));
  UR_EXPECT_TRUE(NavRailCompact(641));
  UR_EXPECT_TRUE(NavRailCompact(1007));
  UR_EXPECT_FALSE(NavRailCompact(1008));
  UR_EXPECT_FALSE(NavRailCompact(2000));
}

// The window takes the rail's mode from its width before the pages fold, and
// every item follows the mode, the Developer item inserted later included,
// with its label in the tooltip.
UR_TEST(ShellLayout_TheWindowSetsTheRailModeOnEveryWidth) {
  const std::string window = ReadShellSource("MainWindow.cpp");
  const std::string breakpoint =
      ShellBody(window, "void MainWindow::ApplyPageBreakpoint(int widthDip) {");
  UR_EXPECT_TRUE(ShellInOrder(breakpoint,
                              {"shell_->SetCompactNav(shell::NavRailCompact(widthDip));",
                               "connectPage_->ApplyBreakpoint(widthDip);"}));
  const std::string shell = ReadShellSource("HomeShell.cpp");
  const std::string make = ShellBody(shell, "HomeShell::NavItem* HomeShell::MakeNavItem(");
  UR_EXPECT_TRUE(ShellInOrder(make, {"ApplyCompact(item);", "items_.push_back(item);"}));
  const std::string compact = ShellBody(shell, "void HomeShell::SetCompactNav(bool compact) {");
  UR_EXPECT_TRUE(ShellHas(compact, "navRail_.add_css_class(\"compact\");"));
  UR_EXPECT_TRUE(ShellHas(compact, "for (auto& item : items_) ApplyCompact(item);"));
  const std::string apply = ShellBody(shell, "void HomeShell::ApplyCompact(NavItem& item) {");
  UR_EXPECT_TRUE(ShellHas(apply, "item.label->set_visible(!compact_);"));
  UR_EXPECT_TRUE(ShellHas(apply, "item.button->set_tooltip_text(item.label->get_text());"));
}

// The status strip is the shell's last row and spans the window, under the
// rail and the content, as Windows' root grid has it.
UR_TEST(ShellLayout_TheStatusStripSpansTheWindow) {
  const std::string shell = ReadShellSource("HomeShell.cpp");
  UR_EXPECT_TRUE(
      ShellHas(shell, "HomeShell::HomeShell() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {"));
  const std::string build = ShellBody(shell, "HomeShell::HomeShell()");
  UR_EXPECT_TRUE(ShellInOrder(build, {"body_.append(navRail_);", "body_.append(contentOverlay_);",
                                      "append(body_);", "append(statusStrip_);"}));
  UR_EXPECT_FALSE(ShellHas(build, "contentColumn_.append(statusStrip_);"));
}
