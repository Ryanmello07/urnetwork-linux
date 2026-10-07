// Cutting a string to a byte budget without splitting a UTF-8 sequence.
//
// The daemon's log ring caps a line at a byte count (DaemonLog.cpp). A cut
// that lands inside a multibyte character leaves a lead byte with no
// continuation, and every reader then sees an invalid sequence: the control
// frame shows it as U+FFFD (ctl::DumpForWire) and a strict reader refuses the
// line. Backing off to the start of the character keeps the text valid
// whenever it was valid before the cut.
//
// Pure: no glib, no SDK, so the unit tests read it directly.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string>

namespace urnw {

// Truncates `text` to at most `maxBytes`, dropping a character the limit would
// split. Returns whether anything was dropped. Bytes that were already invalid
// UTF-8 are cut as bytes: a continuation byte with no lead stops the back-off
// after the three bytes a sequence can carry beyond its lead.
inline bool TruncateUtf8(std::string& text, std::size_t maxBytes) {
  if (text.size() <= maxBytes) return false;
  std::size_t cut = maxBytes;
  // The byte at `cut` is the first one dropped. While it continues a sequence
  // (0b10xxxxxx), the sequence started at or before the cut, so it goes too.
  for (int backed = 0; cut > 0 && backed < 3; ++backed) {
    const unsigned char next = static_cast<unsigned char>(text[cut]);
    if ((next & 0xC0) != 0x80) break;
    --cut;
  }
  if (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
    // Four continuation bytes in a row are not UTF-8; cut where asked.
    cut = maxBytes;
  }
  text.resize(cut);
  return true;
}

}  // namespace urnw
