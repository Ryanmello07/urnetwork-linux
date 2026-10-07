// Text that comes from outside the app, filtered for display: a network's
// name, a location's or provider's name, a peer device's name. A name with a
// Zalgo stack of combining marks renders as overlapping glyphs that spill
// over the lines around it (Pango stacks every mark, and a GTK label does
// not clip), and zero-width and bidi control characters hide in a name or
// reorder the text around them.
//
// SanitizeExternalDisplayText keeps at most one combining mark per base
// character and drops the invisible format characters. It is applied where a
// name is shown, never to text the user types, to a name seeded into an
// editor, to an address, or to anything sent back to the server.
//
// Unlike Windows (kit::SanitizeExternalDisplayText), ZWNJ and ZWJ (U+200C,
// U+200D) are kept: they join the emoji sequences a device name may carry (a
// family, a profession) and shape Persian and Indic text. They do not count
// as a base, so they cannot restart a mark stack.
//
// Header-only and free of GTK so the unit tests can build it
// (tests/DisplayTextTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace urnw {

// The combining diacritical marks block and its three extensions: Unicode's
// Mn and Me by range, to stay dependency-free. Hebrew, Arabic and Indic marks
// are outside them and pass unchanged, so those scripts render as sent.
inline bool IsStackingMark(char32_t cp) {
  return (0x0300 <= cp && cp <= 0x036F) || (0x1AB0 <= cp && cp <= 0x1AFF) ||
         (0x1DC0 <= cp && cp <= 0x1DFF) || (0x20D0 <= cp && cp <= 0x20FF);
}

// The zero-width space, the directional marks, the bidi embeddings,
// overrides and isolates, the word joiner and invisible operators, and the
// byte order mark: invisible ink, and the bidi controls reorder the text
// around them.
inline bool IsHiddenFormat(char32_t cp) {
  return cp == 0x200B || cp == 0x200E || cp == 0x200F || (0x202A <= cp && cp <= 0x202E) ||
         (0x2060 <= cp && cp <= 0x206F) || cp == 0xFEFF;
}

// ZWNJ and ZWJ: kept, and transparent to the mark count.
inline bool IsJoinControl(char32_t cp) { return cp == 0x200C || cp == 0x200D; }

inline std::string SanitizeExternalDisplayText(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool haveBase = false;  // a kept character the next mark may attach to
  bool markKept = false;  // the current base already carries its one mark
  size_t i = 0;
  while (i < text.size()) {
    // One UTF-8 code point. Ill-formed bytes pass through untouched: this is
    // a display filter, not a validator, and mangling bytes it cannot read is
    // worse than showing them.
    const auto lead = static_cast<unsigned char>(text[i]);
    char32_t cp = lead;
    size_t len = 1;
    if (0xC0 <= lead && lead < 0xE0) {
      cp = lead & 0x1F;
      len = 2;
    } else if (0xE0 <= lead && lead < 0xF0) {
      cp = lead & 0x0F;
      len = 3;
    } else if (0xF0 <= lead && lead < 0xF8) {
      cp = lead & 0x07;
      len = 4;
    }
    bool valid = i + len <= text.size();
    for (size_t k = 1; valid && k < len; ++k) {
      const auto cont = static_cast<unsigned char>(text[i + k]);
      valid = (cont & 0xC0) == 0x80;
      cp = (cp << 6) | (cont & 0x3F);
    }
    if (!valid) {
      out.push_back(text[i]);
      ++i;
      continue;
    }
    if (IsStackingMark(cp)) {
      // the second mark on a base is where the overlap starts
      if (haveBase && !markKept) {
        markKept = true;
        out.append(text, i, len);
      }
    } else if (IsJoinControl(cp)) {
      out.append(text, i, len);
    } else if (!IsHiddenFormat(cp)) {
      out.append(text, i, len);
      haveBase = true;
      markKept = false;
    }
    i += len;
  }
  return out;
}

}  // namespace urnw
