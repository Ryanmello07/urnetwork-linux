// The compact relative time (RelativeTimeSpan.hpp, Formatters.cpp
// RelativeTime): "now" under 5 seconds, then seconds, minutes, hours and days
// ago up to 7 days, then the date (REVOKE-UI-FINAL.md §3.1). RelativeTime
// stopped at hours: a session last used two days ago read "48h ago", and a
// month ago "720h ago". Every boundary is pinned here; Formatters.cpp needs
// glib, so its mapping onto the catalog and the locale is read from source.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <string>

#include "RelativeTimeSpan.hpp"
#include "WiringSource.hpp"

using urnw::relative_time::kDateFromSeconds;
using urnw::relative_time::kDaySeconds;
using urnw::relative_time::kHourSeconds;
using urnw::relative_time::Span;
using urnw::relative_time::SpanFor;
using urnw::relative_time::Unit;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

namespace {

bool Is(const Span& span, Unit unit, int64_t count) {
  return span.unit == unit && span.count == count;
}

}  // namespace

UR_TEST(RelativeTime_NowUnderFiveSecondsAndInTheFuture) {
  UR_EXPECT_TRUE(Is(SpanFor(0), Unit::Now, 0));
  UR_EXPECT_TRUE(Is(SpanFor(4), Unit::Now, 0));
  // a clock ahead of the server's: not "-3s ago"
  UR_EXPECT_TRUE(Is(SpanFor(-3), Unit::Now, 0));
  UR_EXPECT_TRUE(Is(SpanFor(-86400), Unit::Now, 0));
}

UR_TEST(RelativeTime_SecondsMinutesAndHours) {
  UR_EXPECT_TRUE(Is(SpanFor(5), Unit::Seconds, 5));
  UR_EXPECT_TRUE(Is(SpanFor(59), Unit::Seconds, 59));
  UR_EXPECT_TRUE(Is(SpanFor(60), Unit::Minutes, 1));
  UR_EXPECT_TRUE(Is(SpanFor(5 * 60 + 59), Unit::Minutes, 5));
  UR_EXPECT_TRUE(Is(SpanFor(kHourSeconds - 1), Unit::Minutes, 59));
  UR_EXPECT_TRUE(Is(SpanFor(kHourSeconds), Unit::Hours, 1));
  UR_EXPECT_TRUE(Is(SpanFor(kDaySeconds - 1), Unit::Hours, 23));
}

UR_TEST(RelativeTime_DaysUpToSevenThenTheDate) {
  UR_EXPECT_TRUE(Is(SpanFor(kDaySeconds), Unit::Days, 1));
  UR_EXPECT_TRUE(Is(SpanFor(2 * kDaySeconds), Unit::Days, 2));  // not "48h ago"
  UR_EXPECT_TRUE(Is(SpanFor(kDateFromSeconds - 1), Unit::Days, 6));
  UR_EXPECT_EQ(7 * kDaySeconds, kDateFromSeconds);
  UR_EXPECT_TRUE(Is(SpanFor(kDateFromSeconds), Unit::Date, 0));
  UR_EXPECT_TRUE(Is(SpanFor(30 * kDaySeconds), Unit::Date, 0));
}

// Formatters.cpp words every unit from the store (days_ago_abbrev is new) and
// names the date in the locale; the date is the event's: now less the age.
UR_TEST(RelativeTime_TheFormatterWordsEveryUnit) {
  const std::string formatters = ReadCode("Formatters.cpp");
  const std::string relative = FunctionBody(formatters, "std::string RelativeTime(int64_t secondsAgo)");
  UR_EXPECT_TRUE_MSG("RelativeTime is defined", !relative.empty());
  UR_EXPECT_TRUE(Contains(relative, "relative_time::SpanFor(secondsAgo)"));
  UR_EXPECT_TRUE(Contains(relative, "T_(\"now\", \"now\")"));
  UR_EXPECT_TRUE(Contains(relative, "Format(T_(\"seconds_ago_abbrev\", \"{}s ago\"), span.count)"));
  UR_EXPECT_TRUE(Contains(relative, "Format(T_(\"minutes_ago_abbrev\", \"{}m ago\"), span.count)"));
  UR_EXPECT_TRUE(Contains(relative, "Format(T_(\"hours_ago_abbrev\", \"{}h ago\"), span.count)"));
  UR_EXPECT_TRUE(Contains(relative, "Format(T_(\"days_ago_abbrev\", \"{}d ago\"), span.count)"));
  UR_EXPECT_TRUE(Contains(relative, "LocalDate(g_get_real_time() / G_USEC_PER_SEC - secondsAgo)"));
  // the locale's date, and the date with the time for a description
  UR_EXPECT_TRUE(Contains(FunctionBody(formatters, "std::string LocalDate(int64_t unixSeconds)"),
                          "FormatLocal(unixSeconds, \"%x\")"));
  UR_EXPECT_TRUE(Contains(FunctionBody(formatters, "std::string LocalDateTime(int64_t unixSeconds)"),
                          "FormatLocal(unixSeconds, \"%x, %R\")"));
  UR_EXPECT_TRUE(Contains(FunctionBody(formatters, "std::string FormatLocal("),
                          "g_date_time_new_from_unix_local(unixSeconds)"));
}
