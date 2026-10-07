// When the update checker asks GitHub again, decided pure.
//
// Requests are anonymous: GitHub allows 60 an hour per IP address, and every
// user behind one exit or one NAT shares them, a URnetwork exit included when
// the app's own traffic is tunnelled. A refused request carries when to ask
// again: Retry-After (seconds, for a secondary limit), or X-RateLimit-Reset (a
// Unix time on GitHub's clock) with X-RateLimit-Remaining at 0. The checker
// never asks before then, manual checks included, and never waits more than a
// day whatever a header says, so a hostile or broken header cannot stop checks
// for good.
//
// No GTK or libsoup: UpdateChecker.cpp asks it, and tests/UpdateScheduleTest.cpp
// runs it on any host. Same arrangement as the windows app's UpdateSchedule.h.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>

namespace urnw::update {

// The longest a refused request can push the next one out.
inline constexpr std::int64_t kMaxBackoffSeconds = 24 * 60 * 60;

// What a refused release-list request said about asking again.
struct RateLimit {
  // Retry-After in seconds, 0 when absent.
  std::int64_t retryAfterSeconds = 0;
  // X-RateLimit-Reset in Unix seconds on GitHub's clock, 0 when absent.
  std::int64_t resetUnixSeconds = 0;
  // X-RateLimit-Remaining was 0.
  bool exhausted = false;
  // The response's Date header in Unix seconds, 0 when absent: the reset is
  // measured against it, not against this machine's clock.
  std::int64_t serverUnixSeconds = 0;
};

// How many seconds after a refused request the next one may go: at least
// `cadenceSeconds`, later when the response asked for it, at most
// kMaxBackoffSeconds unless the cadence itself is longer.
inline constexpr std::int64_t NextCheckDelaySeconds(std::int64_t cadenceSeconds,
                                                    const RateLimit& limit) {
  std::int64_t wait = 0;
  if (limit.retryAfterSeconds > 0) wait = limit.retryAfterSeconds;
  if (limit.exhausted && limit.resetUnixSeconds > 0 && limit.serverUnixSeconds > 0) {
    wait = std::max(wait, limit.resetUnixSeconds - limit.serverUnixSeconds);
  }
  wait = std::min(wait, kMaxBackoffSeconds);
  return std::max(cadenceSeconds, wait);
}

// A response header's value as a decimal count, or `fallback` when it is
// absent or is not one: digits only (no sign, no space, no date form of
// Retry-After) and at most 18 of them, so it cannot overflow.
inline constexpr std::int64_t ParseDecimalHeader(const char* value, std::int64_t fallback) {
  if (!value || !*value) return fallback;
  std::int64_t number = 0;
  int digits = 0;
  for (const char* c = value; *c; ++c, ++digits) {
    if (*c < '0' || *c > '9' || digits >= 18) return fallback;
    number = number * 10 + (*c - '0');
  }
  return number;
}

}  // namespace urnw::update
