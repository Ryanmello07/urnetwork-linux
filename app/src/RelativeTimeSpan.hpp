// The span a compact "… ago" label names (Formatters.cpp RelativeTime): "now"
// under 5 seconds, then whole seconds, minutes, hours and days ago up to 7
// days, and from 7 days on the event's own date (REVOKE-UI-FINAL.md §3.1, the
// rule every desktop surface shares with Windows' urnw::RelativeTime). A time
// ahead of this machine's clock (a server's clock running ahead) reads "now".
//
// Pure C++17 so tests/RelativeTimeSpanTest.cpp pins every boundary; the
// words and the date are the catalog's and the locale's, in Formatters.cpp.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

namespace urnw::relative_time {

enum class Unit { Now, Seconds, Minutes, Hours, Days, Date };

struct Span {
  Unit unit = Unit::Now;
  int64_t count = 0;  // whole units ago; 0 for Now and Date
};

inline constexpr int64_t kNowBelowSeconds = 5;
inline constexpr int64_t kMinuteSeconds = 60;
inline constexpr int64_t kHourSeconds = 60 * kMinuteSeconds;
inline constexpr int64_t kDaySeconds = 24 * kHourSeconds;
// from here on the label is the date
inline constexpr int64_t kDateFromSeconds = 7 * kDaySeconds;

inline constexpr Span SpanFor(int64_t secondsAgo) {
  if (secondsAgo < kNowBelowSeconds) return Span{Unit::Now, 0};
  if (secondsAgo < kMinuteSeconds) return Span{Unit::Seconds, secondsAgo};
  if (secondsAgo < kHourSeconds) return Span{Unit::Minutes, secondsAgo / kMinuteSeconds};
  if (secondsAgo < kDaySeconds) return Span{Unit::Hours, secondsAgo / kHourSeconds};
  if (secondsAgo < kDateFromSeconds) return Span{Unit::Days, secondsAgo / kDaySeconds};
  return Span{Unit::Date, 0};
}

}  // namespace urnw::relative_time
