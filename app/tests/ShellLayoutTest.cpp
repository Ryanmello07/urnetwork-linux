// The signed-in shell's width (HomeShell): the window can be narrowed to the
// widths the pages fold at, because the shell's page stack is sized by the
// page on screen rather than by the widest of its seven pages. HomeShell needs
// gtkmm, so this reads its source with the comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <sstream>
#include <string>

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

}  // namespace

// A homogeneous stack is as wide as its widest page, whichever is shown: the
// Account page's wide layout held every destination at 1045.
UR_TEST(ShellLayout_TheStackIsSizedByThePageOnScreen) {
  const std::string shell = ReadShellSource("HomeShell.cpp");
  const std::string build = ShellBody(shell, "HomeShell::HomeShell()");
  UR_EXPECT_TRUE(!build.empty());
  UR_EXPECT_TRUE(ShellHas(build, "stack_.set_hhomogeneous(false);"));
}
