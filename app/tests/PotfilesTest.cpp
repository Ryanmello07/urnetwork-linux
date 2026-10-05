// po/POTFILES.in is the list of sources xgettext reads for the maintainer
// targets po/meson.build sets up (urnetwork-pot, and urnetwork-update-po, which
// runs it first). It must name exactly the sources that look strings up with
// T_ or TN_ (src/I18n.hpp). Nothing else keeps it in step: the build and
// install compile the catalogs the localization store generates and never read
// it, so 16 translating sources went unlisted, and a renamed one stayed listed
// (src/IpFamilyHistogram.cpp, which made urnetwork-pot fail outright).
//
// SPDX-License-Identifier: MPL-2.0
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadPotfilesText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool IsIdentifierChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// The identifier that ends right before `at`, or "" when none does.
std::string IdentifierBefore(const std::string& text, size_t at) {
  size_t start = at;
  while (start > 0 && IsIdentifierChar(text[start - 1])) --start;
  return text.substr(start, at - start);
}

bool IsLiteralPrefix(const std::string& prefix) {
  return prefix.empty() || prefix == "u8" || prefix == "u" || prefix == "U" || prefix == "L";
}

// The source with its comments, string and character literals and
// preprocessor lines left out, so only code remains: the documentation comment
// in I18n.hpp that shows T_("connect", "Connect") and its #define of T_ are no
// call sites. Knows raw strings (Ui.cpp's stylesheet) and digit separators
// (21'600LL), which the sources use.
std::string CodeOnly(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  const size_t n = text.size();
  size_t i = 0;
  bool lineStart = true;
  while (i < n) {
    const char c = text[i];
    const char next = i + 1 < n ? text[i + 1] : '\0';
    if (lineStart && c == '#') {
      // a preprocessor line, with its backslash continuations
      while (i < n && text[i] != '\n') {
        if (text[i] == '\\' && i + 1 < n && text[i + 1] == '\n') ++i;
        ++i;
      }
      continue;
    }
    if (c == '/' && next == '/') {
      while (i < n && text[i] != '\n') ++i;
      continue;
    }
    if (c == '/' && next == '*') {
      const size_t end = text.find("*/", i + 2);
      i = end == std::string::npos ? n : end + 2;
      out += ' ';
      continue;
    }
    if (c == 'R' && next == '"' && IsLiteralPrefix(IdentifierBefore(text, i))) {
      // R"delim( ... )delim"
      const size_t open = text.find('(', i + 2);
      if (open == std::string::npos) break;
      const std::string close = ")" + text.substr(i + 2, open - (i + 2)) + "\"";
      const size_t end = text.find(close, open + 1);
      i = end == std::string::npos ? n : end + close.size();
      out += ' ';
      lineStart = false;
      continue;
    }
    if (c == '\'' && i > 0 && IsIdentifierChar(text[i - 1]) &&
        !IsLiteralPrefix(IdentifierBefore(text, i))) {
      // a digit separator, not a character literal
      ++i;
      continue;
    }
    if (c == '"' || c == '\'') {
      ++i;
      while (i < n && text[i] != c && text[i] != '\n') {
        if (text[i] == '\\') ++i;
        ++i;
      }
      ++i;
      out += ' ';
      lineStart = false;
      continue;
    }
    out += c;
    if (c == '\n') {
      lineStart = true;
    } else if (!std::isspace(static_cast<unsigned char>(c))) {
      lineStart = false;
    }
    ++i;
  }
  return out;
}

// The source calls T_ or TN_: the macro's name as a whole token, then "(", in
// code.
bool CallsTranslation(const std::string& text) {
  const std::string code = CodeOnly(text);
  for (const char* name : {"T_", "TN_"}) {
    const std::string macro(name);
    for (size_t at = code.find(macro); at != std::string::npos;
         at = code.find(macro, at + macro.size())) {
      if (at > 0 && IsIdentifierChar(code[at - 1])) continue;
      size_t after = at + macro.size();
      while (after < code.size() && std::isspace(static_cast<unsigned char>(code[after]))) {
        ++after;
      }
      if (after < code.size() && code[after] == '(') return true;
    }
  }
  return false;
}

// The entries of POTFILES.in: every line that is not blank or a comment.
std::multiset<std::string> PotfilesEntries() {
  std::multiset<std::string> entries;
  std::istringstream lines(
      ReadPotfilesText(std::filesystem::path(UR_SRC_DIR) / ".." / "po" / "POTFILES.in"));
  for (std::string line; std::getline(lines, line);) {
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    entries.insert(line);
  }
  return entries;
}

}  // namespace

// The rule itself: a call in code counts, a mention in a comment, a string or
// a #define does not, and neither does an identifier that only ends in T_.
UR_TEST(potfilesCallSitesAreCodeOnly) {
  UR_EXPECT_TRUE(CallsTranslation("label.set_text(T_(\"connect\", \"Connect\"));"));
  UR_EXPECT_TRUE(CallsTranslation("auto s = TN_(\"host_count\", \"{} host\", \"{} hosts\", n);"));
  UR_EXPECT_TRUE(CallsTranslation("return std::string(T_ (key, english));"));
  UR_EXPECT_FALSE(CallsTranslation("// T_(\"connect\", \"Connect\")\nint x = 1;"));
  UR_EXPECT_FALSE(CallsTranslation("/* TN_(\"a\", \"b\", \"c\", n) */ int x = 1;"));
  UR_EXPECT_FALSE(CallsTranslation("#define T_(key_id, english) \\\n  g_dpgettext2(d, key_id, english)\n"));
  UR_EXPECT_FALSE(CallsTranslation("const char* s = \"T_(\\\"connect\\\")\";"));
  UR_EXPECT_FALSE(CallsTranslation("const char* css = R\"css(a { T_(x) })css\";"));
  UR_EXPECT_FALSE(CallsTranslation("GT_(x); MY_T_(y);"));
  // a digit separator does not open a character literal that hides a call
  UR_EXPECT_TRUE(CallsTranslation("int ms = 21'600; label.set_text(T_(\"a\", \"A\"));"));
  // I18n.hpp, deliberately unlisted, defines the macros and calls neither
  UR_EXPECT_FALSE(CallsTranslation(ReadPotfilesText(std::filesystem::path(UR_SRC_DIR) / "I18n.hpp")));
}

// Every source that looks a string up with T_ or TN_ is listed.
UR_TEST(potfilesListsEverySourceThatTranslates) {
  const std::filesystem::path src(UR_SRC_DIR);
  if (!std::filesystem::is_directory(src)) {
    UR_FAIL("UR_SRC_DIR is not a directory");
    return;
  }
  const std::multiset<std::string> entries = PotfilesEntries();
  int translating = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
    if (!entry.is_regular_file()) continue;
    const std::string ext = entry.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
    if (!CallsTranslation(ReadPotfilesText(entry.path()))) continue;
    ++translating;
    const std::string listed =
        "src/" + std::filesystem::relative(entry.path(), src).generic_string();
    UR_EXPECT_TRUE_MSG(listed + " calls T_ or TN_ and is not in po/POTFILES.in",
                       entries.count(listed) > 0);
  }
  UR_EXPECT_TRUE_MSG("the scan saw the translating sources", translating > 30);
}

// ...and every entry is such a source, listed once: a file that was renamed,
// removed or stopped translating does not stay behind (xgettext stops at an
// entry it cannot open).
UR_TEST(potfilesListsOnlySourcesThatTranslate) {
  const std::multiset<std::string> entries = PotfilesEntries();
  UR_EXPECT_TRUE_MSG("po/POTFILES.in lists sources", entries.size() > 30);
  const std::filesystem::path root = std::filesystem::path(UR_SRC_DIR) / "..";
  for (const std::string& entry : std::set<std::string>(entries.begin(), entries.end())) {
    UR_EXPECT_TRUE_MSG(entry + " is listed more than once", entries.count(entry) == 1);
    const std::filesystem::path path = root / entry;
    if (!std::filesystem::is_regular_file(path)) {
      UR_FAIL(entry + " is in po/POTFILES.in but does not exist");
      continue;
    }
    UR_EXPECT_TRUE_MSG(entry + " is in po/POTFILES.in but calls neither T_ nor TN_",
                       CallsTranslation(ReadPotfilesText(path)));
  }
}
