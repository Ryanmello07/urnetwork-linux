// Compact counts and count rates: the decimal base-1000 compact form of a
// count ("996", "1.2k", "12k", "1.2M") and that count with a rate unit ("340
// reads/s", the extender chart's count label, EXTENDER.md O8). Header-only and
// standard library only, so the unit tests reach it directly
// (tests/CountFormatTest.cpp); Formatters.hpp includes it for every other
// caller.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace urnw {

// Decimal base-1000: "996", "1.2k", "340k", "3.4M".
inline std::string FormatCountCompact(int64_t count) {
  const double v = static_cast<double>(count);
  if (count < 1000) return std::to_string(count);
  char buf[64];
  if (v < 1e6) {
    std::snprintf(buf, sizeof(buf), v < 1e4 ? "%.1fk" : "%.0fk", v / 1000);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1fM", v / 1e6);
  }
  return buf;
}

// "340 reads/s": the compact count with a rate unit the caller takes from the
// store (the extender chart's reads_per_second, EXTENDER.md O8).
inline std::string FormatCountRate(int64_t countPerSecond, const std::string& unit) {
  return FormatCountCompact(countPerSecond) + " " + unit;
}

}  // namespace urnw
