// When the updater asks GitHub again after a refused request
// (UpdateSchedule.hpp), on the windows app's vectors (update-release-tests.cpp
// Schedule), and how a rate-limit header is read.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "UpdateSchedule.hpp"

#include <cstdint>

namespace {

using namespace urnw::update;

constexpr std::int64_t kServerUnix = 1'800'000'000;
constexpr std::int64_t kCadence = 6 * 60 * 60;
constexpr std::int64_t kDay = 24 * 60 * 60;

RateLimit Limit(std::int64_t retryAfter, std::int64_t reset, bool exhausted, std::int64_t server) {
  RateLimit limit;
  limit.retryAfterSeconds = retryAfter;
  limit.resetUnixSeconds = reset;
  limit.exhausted = exhausted;
  limit.serverUnixSeconds = server;
  return limit;
}

}  // namespace

UR_TEST(aRefusalWithoutHeadersHoldsNothingBeyondTheCadence) {
  UR_EXPECT_EQ(kCadence, NextCheckDelaySeconds(kCadence, RateLimit{}));
  UR_EXPECT_EQ(std::int64_t{0}, NextCheckDelaySeconds(0, RateLimit{}));
  // a cadence longer than a day is kept
  UR_EXPECT_EQ(2 * kDay, NextCheckDelaySeconds(2 * kDay, RateLimit{}));
}

UR_TEST(retryAfterHoldsTheNextRequest) {
  UR_EXPECT_EQ(std::int64_t{60}, NextCheckDelaySeconds(0, Limit(60, 0, false, kServerUnix)));
  // a short Retry-After does not bring the cadence forward
  UR_EXPECT_EQ(kCadence, NextCheckDelaySeconds(kCadence, Limit(60, 0, false, kServerUnix)));
  // a negative one holds nothing
  UR_EXPECT_EQ(std::int64_t{0}, NextCheckDelaySeconds(0, Limit(-5, 0, false, kServerUnix)));
}

UR_TEST(aSpentHourHoldsUntilTheResetOnGitHubsClock) {
  UR_EXPECT_EQ(std::int64_t{1800},
               NextCheckDelaySeconds(0, Limit(0, kServerUnix + 1800, true, kServerUnix)));
  // a reset time with requests left holds nothing
  UR_EXPECT_EQ(std::int64_t{0},
               NextCheckDelaySeconds(0, Limit(0, kServerUnix + 1800, false, kServerUnix)));
  // without the server's date the reset is not measured against this clock
  UR_EXPECT_EQ(std::int64_t{0}, NextCheckDelaySeconds(0, Limit(0, kServerUnix + 1800, true, 0)));
  // a reset already past holds nothing
  UR_EXPECT_EQ(std::int64_t{0},
               NextCheckDelaySeconds(0, Limit(0, kServerUnix - 600, true, kServerUnix)));
}

UR_TEST(theLaterOfRetryAfterAndTheResetHolds) {
  UR_EXPECT_EQ(std::int64_t{1800},
               NextCheckDelaySeconds(0, Limit(60, kServerUnix + 1800, true, kServerUnix)));
  UR_EXPECT_EQ(std::int64_t{7200},
               NextCheckDelaySeconds(0, Limit(7200, kServerUnix + 1800, true, kServerUnix)));
}

UR_TEST(noHeaderHoldsChecksForMoreThanADay) {
  UR_EXPECT_EQ(std::int64_t{24 * 60 * 60}, kMaxBackoffSeconds);
  UR_EXPECT_EQ(kDay, NextCheckDelaySeconds(0, Limit(10 * kDay, 0, false, kServerUnix)));
  UR_EXPECT_EQ(kDay,
               NextCheckDelaySeconds(0, Limit(0, kServerUnix + 10 * kDay, true, kServerUnix)));
}

UR_TEST(aRateLimitHeaderIsReadAsDigitsOnly) {
  UR_EXPECT_EQ(std::int64_t{60}, ParseDecimalHeader("60", 0));
  UR_EXPECT_EQ(std::int64_t{0}, ParseDecimalHeader("0", -1));  // X-RateLimit-Remaining: 0
  UR_EXPECT_EQ(std::int64_t{1800000000}, ParseDecimalHeader("1800000000", 0));
  UR_EXPECT_EQ(std::int64_t{999'999'999'999'999'999},
               ParseDecimalHeader("999999999999999999", 0));
  // absent, empty, signed, spaced, the date form of Retry-After, 19 digits
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader(nullptr, -1));
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader("", -1));
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader("-5", -1));
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader("+5", -1));
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader(" 5", -1));
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader("5 ", -1));
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader("Wed, 21 Oct 2026 07:28:00 GMT", -1));
  UR_EXPECT_EQ(std::int64_t{-1}, ParseDecimalHeader("1000000000000000000", -1));
}
