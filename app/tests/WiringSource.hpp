// Reading a source the way the wiring tests check it: the file under src/
// (UR_SRC_DIR, as meson passes it), with every comment blanked so a comment
// that names a call is never mistaken for the call, and string literals kept
// (a T_ key or a CSS class is part of what a test checks). Shared by the
// Sessions wiring tests; the older wiring tests each carry their own copy.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace urnw::testing::wiring {

// The file under src/, or "" when it cannot be read.
inline std::string ReadRaw(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The text with // and /* */ comments replaced by spaces (newlines kept, so
// offsets and lines still match); string and character literals are kept.
inline std::string BlankComments(const std::string& text) {
  std::string out = text;
  const size_t n = out.size();
  size_t i = 0;
  const auto hexDigit = [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; };
  while (i < n) {
    const char c = out[i];
    // a digit separator (21'600LL) is not a character literal
    if (c == '\'' && i > 0 && i + 1 < n && hexDigit(out[i - 1]) && hexDigit(out[i + 1])) {
      ++i;
      continue;
    }
    if (c == '"' || c == '\'') {
      // a raw string R"( ... )" carries no escapes
      if (c == '"' && i > 0 && out[i - 1] == 'R') {
        const size_t open = out.find('(', i);
        if (open == std::string::npos) break;
        const std::string close = ")" + out.substr(i + 1, open - i - 1) + "\"";
        const size_t end = out.find(close, open);
        i = end == std::string::npos ? n : end + close.size();
        continue;
      }
      size_t k = i + 1;
      while (k < n && out[k] != c && out[k] != '\n') {
        if (out[k] == '\\') ++k;
        ++k;
      }
      i = k + 1;
      continue;
    }
    if (c == '/' && i + 1 < n && out[i + 1] == '/') {
      while (i < n && out[i] != '\n') out[i++] = ' ';
      continue;
    }
    if (c == '/' && i + 1 < n && out[i + 1] == '*') {
      const size_t end = out.find("*/", i + 2);
      const size_t stop = end == std::string::npos ? n : end + 2;
      for (; i < stop; ++i) {
        if (out[i] != '\n') out[i] = ' ';
      }
      continue;
    }
    ++i;
  }
  return out;
}

// The file under src/ with its comments blanked.
inline std::string ReadCode(const std::string& relative) { return BlankComments(ReadRaw(relative)); }

// The body of the function whose definition starts with `signature`, braces
// balanced from the first '{' after it; "" when it is not found.
inline std::string FunctionBody(const std::string& code, const std::string& signature) {
  const size_t at = code.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t open = code.find('{', at);
  if (open == std::string::npos) return std::string();
  int depth = 0;
  for (size_t i = open; i < code.size(); ++i) {
    if (code[i] == '{') ++depth;
    if (code[i] == '}' && --depth == 0) return code.substr(open, i - open + 1);
  }
  return std::string();
}

inline bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

inline size_t CountOf(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// True when `first` occurs in `text` and before the first `second`.
inline bool Before(const std::string& text, const std::string& first, const std::string& second) {
  const size_t a = text.find(first);
  const size_t b = text.find(second);
  return a != std::string::npos && b != std::string::npos && a < b;
}

}  // namespace urnw::testing::wiring
