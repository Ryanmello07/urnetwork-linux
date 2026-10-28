// The slow-internet indicator's pure half (TunnelGeometry.hpp,
// SlowInternet.hpp): the tunnel's depth table, geometry and fade plan, the
// number formats, the copy each state reads, what the Connect page's pill
// says (apple's SlowInternetPillContentTests, with its readings), SdkHost's
// feed gating, and the desktop notification's dwell and episodes, each driven
// through an explicit sequence of readings and times so the expected calls
// are exact. The tray's art is checked to be the 32 px files the tray names.
//
// SPDX-License-Identifier: MPL-2.0
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "SlowInternet.hpp"
#include "TestHarness.hpp"
#include "TunnelGeometry.hpp"

#ifndef UR_ASSETS_DIR
#define UR_ASSETS_DIR ""
#endif

namespace {

namespace si = urnw::slow_internet;
namespace tunnel = urnw::tunnel;

// The harness's EXPECT_EQ is numeric; strings compare here with both values in
// the failure.
void ExpectText(const char* what, const std::string& expected, const std::string& actual) {
  if (expected != actual) {
    UR_FAIL(std::string(what) + ": \"" + actual + "\", expected \"" + expected + "\"");
  }
}

// Records every call, in order.
struct RecordingSink {
  std::vector<si::Notice> posts;
  int withdraws = 0;
  void Post(const si::Notice& notice) { posts.push_back(notice); }
  void Withdraw() { ++withdraws; }
};

si::Delay SlowDelay(double quality, int64_t upMillis = 640, int64_t downMillis = 1234,
                    bool fallback = false) {
  si::Delay delay;
  delay.upQuality = quality;
  delay.downQuality = quality / 2;
  delay.upQueueDelayMillis = upMillis;
  delay.downQueueDelayMillis = downMillis;
  delay.upBitsPerSecond = 48'000;
  delay.fallback = fallback;
  return delay;
}

si::Reading Available(const si::Delay& delay) {
  si::Reading reading;
  reading.available = true;
  reading.delay = delay;
  return reading;
}

si::Reading Slow(double quality = 0.5) { return Available(SlowDelay(quality)); }
si::Reading Normal() { return Available(si::Delay{}); }
si::Reading Unavailable() { return si::Reading{}; }

// The pill tests' slow reading: the download the worse, both rates limited.
si::Delay PillSlow() {
  si::Delay delay;
  delay.upQuality = 0.25;
  delay.downQuality = 0.75;
  delay.upBitsPerSecond = 48'000;
  delay.downBitsPerSecond = 85'000;
  delay.upQueueDelayMillis = 800;
  delay.downQueueDelayMillis = 1900;
  return delay;
}

// No standing delay either way, both directions limited.
si::Delay PillRatesOnly() {
  si::Delay delay;
  delay.downQuality = 0.75;
  delay.upBitsPerSecond = 150'000;
  delay.downBitsPerSecond = 280'000;
  return delay;
}

// The recovered state: the label alone, "Internet back to normal".
si::PillContent Recovered() {
  si::PillContent content;
  content.recovered = true;
  return content;
}

constexpr int64_t kSecond = 1000;

}  // namespace

// ---- the tunnel --------------------------------------------------------------

UR_TEST(slowInternetDepthFollowsTheTable) {
  UR_EXPECT_EQ(0, tunnel::DepthForQuality(0));
  UR_EXPECT_EQ(1, tunnel::DepthForQuality(0.25));
  UR_EXPECT_EQ(2, tunnel::DepthForQuality(0.5));
  UR_EXPECT_EQ(4, tunnel::DepthForQuality(0.75));
  UR_EXPECT_EQ(6, tunnel::DepthForQuality(1.0));
  // between the steps (a quality step other than 0.25): interpolated, rounded
  UR_EXPECT_EQ(2, tunnel::DepthForQuality(0.375));
  UR_EXPECT_EQ(3, tunnel::DepthForQuality(0.625));
  UR_EXPECT_EQ(5, tunnel::DepthForQuality(0.875));
  // anything above 0 shows a square; nothing outside 0..1 breaks it
  UR_EXPECT_EQ(1, tunnel::DepthForQuality(0.1));
  UR_EXPECT_EQ(6, tunnel::DepthForQuality(1.5));
  UR_EXPECT_EQ(0, tunnel::DepthForQuality(1e-9));
  UR_EXPECT_EQ(0, tunnel::DepthForQuality(-1));
  UR_EXPECT_EQ(0, tunnel::DepthForQuality(std::numeric_limits<double>::quiet_NaN()));

  // the tray's four levels, and the depth its art draws for each
  UR_EXPECT_EQ(0, tunnel::LevelForQuality(0));
  UR_EXPECT_EQ(1, tunnel::LevelForQuality(0.25));
  UR_EXPECT_EQ(2, tunnel::LevelForQuality(0.5));
  UR_EXPECT_EQ(3, tunnel::LevelForQuality(0.75));
  UR_EXPECT_EQ(4, tunnel::LevelForQuality(1.0));
  UR_EXPECT_EQ(1, tunnel::LevelForQuality(0.1));
  UR_EXPECT_EQ(4, tunnel::LevelForQuality(2.0));
  UR_EXPECT_EQ(0, tunnel::DepthForLevel(0));
  UR_EXPECT_EQ(1, tunnel::DepthForLevel(1));
  UR_EXPECT_EQ(2, tunnel::DepthForLevel(2));
  UR_EXPECT_EQ(4, tunnel::DepthForLevel(3));
  UR_EXPECT_EQ(6, tunnel::DepthForLevel(4));
  UR_EXPECT_EQ(6, tunnel::DepthForLevel(7));
}

// apple/DELAY.md §2.1 at 24 px: the outer stroke 2.256 inset by half, each
// square 0.70x about (13.2, 13.2), each stroke 0.60x, opacity down 0.08 a step.
UR_TEST(slowInternetSquaresAt24Px) {
  const tunnel::Square outer = tunnel::SquareAt(0, 24);
  UR_EXPECT_NEAR(1.128, outer.x, 1e-9);
  UR_EXPECT_NEAR(1.128, outer.y, 1e-9);
  UR_EXPECT_NEAR(21.744, outer.side, 1e-9);
  UR_EXPECT_NEAR(2.256, outer.stroke, 1e-9);
  UR_EXPECT_NEAR(1.0, outer.opacity, 1e-12);
  // its outer edge is the box
  UR_EXPECT_NEAR(0.0, outer.x - outer.stroke / 2, 1e-9);
  UR_EXPECT_NEAR(24.0, outer.x + outer.side + outer.stroke / 2, 1e-9);

  const tunnel::Square second = tunnel::SquareAt(1, 24);
  UR_EXPECT_NEAR(4.7496, second.x, 1e-9);
  UR_EXPECT_NEAR(15.2208, second.side, 1e-9);
  UR_EXPECT_NEAR(1.3536, second.stroke, 1e-9);
  UR_EXPECT_NEAR(0.92, second.opacity, 1e-12);

  const tunnel::Square third = tunnel::SquareAt(2, 24);
  UR_EXPECT_NEAR(7.28472, third.x, 1e-9);
  UR_EXPECT_NEAR(10.65456, third.side, 1e-9);
  UR_EXPECT_NEAR(0.81216, third.stroke, 1e-9);
  UR_EXPECT_NEAR(0.84, third.opacity, 1e-12);

  const tunnel::Square last = tunnel::SquareAt(5, 24);
  UR_EXPECT_NEAR(11.171059, last.x, 1e-6);
  UR_EXPECT_NEAR(3.654514, last.side, 1e-6);
  UR_EXPECT_NEAR(0.17542656, last.stroke, 1e-9);
  UR_EXPECT_NEAR(0.6, last.opacity, 1e-12);
}

// The vanishing point past the centre opens the left and top gaps about 1.65
// times the right and bottom ones, at any size, and every square nests inside
// the one around it without touching it, at every size the app draws.
UR_TEST(slowInternetSquaresNestTowardTheVanishingPoint) {
  for (const double size : {16.0, 18.0, 24.0, 32.0}) {
    const tunnel::Square outer = tunnel::SquareAt(0, size);
    const tunnel::Square inner = tunnel::SquareAt(1, size);
    const double leftGap = (inner.x - inner.stroke / 2) - (outer.x + outer.stroke / 2);
    const double rightGap = (outer.x + outer.side - outer.stroke / 2) -
                            (inner.x + inner.side + inner.stroke / 2);
    UR_EXPECT_NEAR_MSG("gap ratio at " + urnw::testing::Str(size), 1.65, leftGap / rightGap,
                       0.02);
    for (int i = 0; i + 1 < tunnel::kMaxDepth; ++i) {
      const tunnel::Square a = tunnel::SquareAt(i, size);
      const tunnel::Square b = tunnel::SquareAt(i + 1, size);
      UR_EXPECT_TRUE_MSG("left at " + urnw::testing::Str(size) + ", " + std::to_string(i),
                         a.x + a.stroke / 2 < b.x - b.stroke / 2);
      UR_EXPECT_TRUE_MSG("right at " + urnw::testing::Str(size) + ", " + std::to_string(i),
                         b.x + b.side + b.stroke / 2 < a.x + a.side - a.stroke / 2);
    }
  }
}

UR_TEST(slowInternetFadePlan) {
  // deeper: outer to inner, 110 ms apart, 420 ms each
  const std::vector<tunnel::Step> build = tunnel::Plan(0, 6);
  UR_EXPECT_EQ(6u, build.size());
  for (size_t k = 0; k < build.size(); ++k) {
    UR_EXPECT_EQ(static_cast<int>(k), build[k].index);
    UR_EXPECT_TRUE(build[k].in);
    UR_EXPECT_EQ(110 * static_cast<int>(k), build[k].delayMs);
    UR_EXPECT_EQ(420, build[k].durationMs);
  }
  UR_EXPECT_EQ(970, tunnel::PlanMs(0, 6));
  // shallower: inner to outer, 90 ms apart, 320 ms each
  const std::vector<tunnel::Step> clear = tunnel::Plan(6, 0);
  UR_EXPECT_EQ(6u, clear.size());
  for (size_t k = 0; k < clear.size(); ++k) {
    UR_EXPECT_EQ(5 - static_cast<int>(k), clear[k].index);
    UR_EXPECT_FALSE(clear[k].in);
    UR_EXPECT_EQ(90 * static_cast<int>(k), clear[k].delayMs);
    UR_EXPECT_EQ(320, clear[k].durationMs);
  }
  UR_EXPECT_EQ(770, tunnel::PlanMs(6, 0));
  // a step counts from the first square that changes
  const std::vector<tunnel::Step> deeper = tunnel::Plan(2, 4);
  UR_EXPECT_EQ(2u, deeper.size());
  UR_EXPECT_TRUE(deeper[0].index == 2 && deeper[0].delayMs == 0);
  UR_EXPECT_TRUE(deeper[1].index == 3 && deeper[1].delayMs == 110);
  const std::vector<tunnel::Step> shallower = tunnel::Plan(4, 1);
  UR_EXPECT_EQ(3u, shallower.size());
  UR_EXPECT_TRUE(shallower[0].index == 3 && shallower[0].delayMs == 0);
  UR_EXPECT_TRUE(shallower[2].index == 1 && shallower[2].delayMs == 180);
  // no change, no motion: numbers alone never move the tunnel
  UR_EXPECT_TRUE(tunnel::Plan(4, 4).empty());
  UR_EXPECT_EQ(0, tunnel::PlanMs(4, 4));
  // the pill fades once the last square has started
  UR_EXPECT_EQ(450, tunnel::ContainerFadeOutDelayMs(6));
  UR_EXPECT_EQ(90, tunnel::ContainerFadeOutDelayMs(2));
  UR_EXPECT_EQ(0, tunnel::ContainerFadeOutDelayMs(1));
  UR_EXPECT_EQ(0, tunnel::ContainerFadeOutDelayMs(0));
}

// ---- formats and copy -----------------------------------------------------------

UR_TEST(slowInternetFormats) {
  ExpectText("0", "", si::FormatDelayMillis(0));
  ExpectText("negative", "", si::FormatDelayMillis(-5));
  ExpectText("1", "1 ms", si::FormatDelayMillis(1));
  ExpectText("640", "640 ms", si::FormatDelayMillis(640));
  ExpectText("999", "999 ms", si::FormatDelayMillis(999));
  ExpectText("1000", "1.0 s", si::FormatDelayMillis(1000));
  ExpectText("1049", "1.0 s", si::FormatDelayMillis(1049));
  ExpectText("1050", "1.1 s", si::FormatDelayMillis(1050));
  ExpectText("1234", "1.2 s", si::FormatDelayMillis(1234));
  ExpectText("9949", "9.9 s", si::FormatDelayMillis(9949));
  // rounding up to ten seconds reads as whole seconds, never "10.0 s"
  ExpectText("9950", "10 s", si::FormatDelayMillis(9950));
  ExpectText("12345", "12 s", si::FormatDelayMillis(12345));
  ExpectText("12500", "13 s", si::FormatDelayMillis(12500));

  ExpectText("0 b/s", "", si::FormatBitsPerSecond(0));
  ExpectText("400 b/s", "1 kb/s", si::FormatBitsPerSecond(400));
  ExpectText("48000", "48 kb/s", si::FormatBitsPerSecond(48'000));
  ExpectText("48499", "48 kb/s", si::FormatBitsPerSecond(48'499));
  ExpectText("48500", "49 kb/s", si::FormatBitsPerSecond(48'500));
  ExpectText("999499", "999 kb/s", si::FormatBitsPerSecond(999'499));
  // rounding up to a megabit reads in megabits, never "1000 kb/s"
  ExpectText("999500", "1.0 Mb/s", si::FormatBitsPerSecond(999'500));
  ExpectText("1400000", "1.4 Mb/s", si::FormatBitsPerSecond(1'400'000));
  ExpectText("1449999", "1.4 Mb/s", si::FormatBitsPerSecond(1'449'999));
  ExpectText("12345678", "12.3 Mb/s", si::FormatBitsPerSecond(12'345'678));

  ExpectText("both", "640 ms · 48 kb/s", si::DirectionValue(640, 48'000));
  ExpectText("rate only", "48 kb/s", si::DirectionValue(0, 48'000));
  ExpectText("delay only", "640 ms", si::DirectionValue(640, 0));
  ExpectText("neither", "", si::DirectionValue(0, 0));
}

UR_TEST(slowInternetCopyFollowsFallback) {
  ExpectText("tray", "slow_internet", si::TrayLineCopy(false).key);
  ExpectText("tray fallback", "slow_internet_fallback", si::TrayLineCopy(true).key);
  ExpectText("tray fallback English", "Fallback connection", si::TrayLineCopy(true).english);
  ExpectText("pill", "slow_internet_poor_signal", si::PillLabelCopy(false).key);
  ExpectText("pill English", "Poor signal", si::PillLabelCopy(false).english);
  ExpectText("pill fallback", "slow_internet_fallback_short", si::PillLabelCopy(true).key);
  ExpectText("pill fallback English", "Fallback", si::PillLabelCopy(true).english);
  ExpectText("title", "slow_internet", si::TitleCopy(false).key);
  ExpectText("title fallback", "slow_internet_fallback_title", si::TitleCopy(true).key);
  ExpectText("toggle", "Notify me when my internet is slow", si::NotifyToggleCopy().english);
}

UR_TEST(slowInternetNoticeText) {
  const auto english = [](const si::Copy& copy) { return std::string(copy.english); };
  si::Delay delay;
  delay.upQuality = 0.5;
  delay.upQueueDelayMillis = 640;
  delay.upBitsPerSecond = 48'000;
  delay.downQueueDelayMillis = 1234;
  const si::NoticeText slow = si::ComposeNotice(si::SlowNotice(delay), english);
  ExpectText("slow title", "Slow internet", slow.title);
  ExpectText("slow body",
             "URnetwork is still connected\nUpload delay: 640 ms · 48 kb/s\n"
             "Download delay: 1.2 s",
             slow.body);
  // a direction with nothing measured has no line
  si::Delay fallback;
  fallback.downQuality = 1;
  fallback.fallback = true;
  const si::NoticeText fallbackText = si::ComposeNotice(si::SlowNotice(fallback), english);
  ExpectText("fallback title", "Using a slower fallback connection", fallbackText.title);
  ExpectText("fallback body", "URnetwork is still connected", fallbackText.body);
  const si::NoticeText recovered = si::ComposeNotice(si::RecoveredNotice(), english);
  ExpectText("recovered title", "Internet back to normal", recovered.title);
  ExpectText("recovered body", "Delays cleared", recovered.body);
}

UR_TEST(slowInternetReadingLevels) {
  UR_EXPECT_EQ(0, si::Level(Unavailable()));
  UR_EXPECT_EQ(0, si::Level(Normal()));
  // the worse direction
  si::Delay delay;
  delay.upQuality = 0.25;
  delay.downQuality = 1;
  UR_EXPECT_EQ(4, si::Level(Available(delay)));
  UR_EXPECT_EQ(6, si::Depth(Available(delay)));
  // never shown unavailable, whatever it says
  si::Reading stale = Available(delay);
  stale.available = false;
  UR_EXPECT_EQ(0, si::Level(stale));
  UR_EXPECT_EQ(0, si::Depth(stale));
}

// ---- the Connect page's pill -------------------------------------------------------

UR_TEST(slowInternetPillLabelIsPoorSignalOrFallback) {
  UR_EXPECT_FALSE(si::PillContent::Slow(PillSlow()).fallback);
  si::Delay fallback = PillSlow();
  fallback.fallback = true;
  UR_EXPECT_TRUE(si::PillContent::Slow(fallback).fallback);
}

// each direction shows its standing delay, the larger one the worse
UR_TEST(slowInternetPillShowsEachDirectionsDelay) {
  const si::PillContent content = si::PillContent::Slow(PillSlow());
  ExpectText("up", "800 ms", content.up);
  ExpectText("down", "1.9 s", content.down);
  UR_EXPECT_TRUE(content.worse == si::Direction::Down);
  UR_EXPECT_FALSE(content.recovered);

  si::Delay upWorse = PillSlow();
  upWorse.upQueueDelayMillis = 2900;
  UR_EXPECT_TRUE(si::PillContent::Slow(upWorse).worse == si::Direction::Up);

  // equal delays mark neither
  si::Delay equal = PillSlow();
  equal.upQueueDelayMillis = 1900;
  UR_EXPECT_TRUE(si::PillContent::Slow(equal).worse == si::Direction::None);

  // the rule itself, as apple's TransportDelayDirection.worse
  UR_EXPECT_TRUE(si::WorseDirection(640, 1200) == si::Direction::Down);
  UR_EXPECT_TRUE(si::WorseDirection(2000, 300) == si::Direction::Up);
  UR_EXPECT_TRUE(si::WorseDirection(500, 500) == si::Direction::None);
  UR_EXPECT_TRUE(si::WorseDirection(0, 0) == si::Direction::None);
}

// no standing delay but a limited rate: the rate shows instead, and a rate is
// never the worse, which goes by delay
UR_TEST(slowInternetPillShowsTheRateOfADirectionWithNoDelay) {
  si::Delay mixedDelay;
  mixedDelay.upQuality = 0.5;
  mixedDelay.upBitsPerSecond = 150'000;
  mixedDelay.downBitsPerSecond = 280'000;
  mixedDelay.downQueueDelayMillis = 620;
  const si::PillContent mixed = si::PillContent::Slow(mixedDelay);
  ExpectText("mixed up", "150 kb/s", mixed.up);
  ExpectText("mixed down", "620 ms", mixed.down);
  UR_EXPECT_TRUE(mixed.worse == si::Direction::Down);

  const si::PillContent rates = si::PillContent::Slow(PillRatesOnly());
  ExpectText("rates up", "150 kb/s", rates.up);
  ExpectText("rates down", "280 kb/s", rates.down);
  UR_EXPECT_TRUE(rates.worse == si::Direction::None);
}

// neither a delay nor a rate: the direction hides; with both hidden the pill
// is the tunnel and the label
UR_TEST(slowInternetPillHidesADirectionWithNeither) {
  si::Delay downOnlyDelay;
  downOnlyDelay.downQuality = 0.25;
  downOnlyDelay.downQueueDelayMillis = 640;
  const si::PillContent downOnly = si::PillContent::Slow(downOnlyDelay);
  ExpectText("down only up", "", downOnly.up);
  ExpectText("down only down", "640 ms", downOnly.down);
  UR_EXPECT_TRUE(downOnly.worse == si::Direction::Down);

  si::Delay neitherDelay;
  neitherDelay.upQuality = 0.5;
  const si::PillContent neither = si::PillContent::Slow(neitherDelay);
  ExpectText("neither up", "", neither.up);
  ExpectText("neither down", "", neither.down);
  UR_EXPECT_TRUE(neither.worse == si::Direction::None);
  UR_EXPECT_FALSE(neither.fallback);
}

// with too little room the better direction goes first, then the numbers
UR_TEST(slowInternetPillDropsTheBetterDirectionThenTheNumbers) {
  const si::PillContent content = si::PillContent::Slow(PillSlow());
  const si::PillContent one = content.WorseOnly();
  ExpectText("one up", "", one.up);
  ExpectText("one down", "1.9 s", one.down);
  UR_EXPECT_FALSE(one.fallback);
  const si::PillContent none = content.LabelOnly();
  ExpectText("none up", "", none.up);
  ExpectText("none down", "", none.down);
  UR_EXPECT_FALSE(none.fallback);

  si::Delay upWorse = PillSlow();
  upWorse.upQueueDelayMillis = 2900;
  ExpectText("up worse up", "2.9 s", si::PillContent::Slow(upWorse).WorseOnly().up);
  ExpectText("up worse down", "", si::PillContent::Slow(upWorse).WorseOnly().down);

  // neither worse: the download stays, or the upload when only it shows
  const si::PillContent rates = si::PillContent::Slow(PillRatesOnly()).WorseOnly();
  ExpectText("rates up", "", rates.up);
  ExpectText("rates down", "280 kb/s", rates.down);
  si::Delay upOnlyDelay;
  upOnlyDelay.upQuality = 0.5;
  upOnlyDelay.upBitsPerSecond = 150'000;
  const si::PillContent upOnly = si::PillContent::Slow(upOnlyDelay);
  UR_EXPECT_TRUE(upOnly.WorseOnly() == upOnly);
}

// the reading returns to normal while the pill shows it: "Internet back to
// normal" with no numbers, which then holds
UR_TEST(slowInternetPillShowsTheRecoveredStateOnAReturnToNormal) {
  const std::optional<si::PillContent> slow =
      si::PillContent::Next(std::nullopt, Available(PillSlow()));
  UR_EXPECT_TRUE(slow == si::PillContent::Slow(PillSlow()));

  const std::optional<si::PillContent> recovered = si::PillContent::Next(slow, Normal());
  UR_EXPECT_TRUE(recovered == Recovered());

  // later normal readings, numbers under the thresholds included, hold it
  UR_EXPECT_TRUE(si::PillContent::Next(recovered, Normal()) == Recovered());
  si::Delay quiet;
  quiet.upQueueDelayMillis = 120;
  quiet.downQueueDelayMillis = 90;
  UR_EXPECT_TRUE(si::PillContent::Next(recovered, Available(quiet)) == Recovered());
  UR_EXPECT_TRUE(si::PillContent::Next(slow, Available(quiet)) == Recovered());
}

// a new slow level during the hold ends it: the slow content returns
UR_TEST(slowInternetPillSlowDuringTheHoldReturnsToTheSlowContent) {
  const std::optional<si::PillContent> recovered =
      si::PillContent::Next(si::PillContent::Slow(PillSlow()), Normal());
  UR_EXPECT_TRUE(recovered == Recovered());
  si::Delay again = PillSlow();
  again.downQueueDelayMillis = 2400;
  const std::optional<si::PillContent> next = si::PillContent::Next(recovered, Available(again));
  UR_EXPECT_TRUE(next == si::PillContent::Slow(again));
  UR_EXPECT_FALSE(next.value_or(Recovered()).recovered);
}

// the reading going away (the remote or the session went) is no recovery: the
// pill just leaves, at once from the hold too
UR_TEST(slowInternetPillLeavesWithNoRecoveryWhenTheReadingGoes) {
  const si::PillContent slow = si::PillContent::Slow(PillSlow());
  UR_EXPECT_FALSE(si::PillContent::Next(slow, Unavailable()).has_value());
  si::Reading stale = Available(PillSlow());
  stale.available = false;
  UR_EXPECT_FALSE(si::PillContent::Next(slow, stale).has_value());

  const std::optional<si::PillContent> recovered = si::PillContent::Next(slow, Normal());
  UR_EXPECT_TRUE(recovered == Recovered());
  UR_EXPECT_FALSE(si::PillContent::Next(recovered, Unavailable()).has_value());
}

// nothing shown, a normal reading shows nothing: no recovered state without a
// slow spell on screen (the pill appearing, or one that left)
UR_TEST(slowInternetPillShowsNothingForNormalWithNothingShown) {
  UR_EXPECT_FALSE(si::PillContent::Next(std::nullopt, Normal()).has_value());
  UR_EXPECT_FALSE(si::PillContent::Next(std::nullopt, Unavailable()).has_value());
}

// one accessible element: the label, or the recovered words, then each
// direction that shows
UR_TEST(slowInternetPillAccessibleName) {
  const auto english = [](const si::Copy& copy) { return std::string(copy.english); };
  const si::PillContent slow = si::PillContent::Slow(PillSlow());
  ExpectText("both", "Poor signal, Upload delay 800 ms, Download delay 1.9 s",
             si::PillAccessibleName(slow, english));
  ExpectText("worse only", "Poor signal, Download delay 1.9 s",
             si::PillAccessibleName(slow.WorseOnly(), english));
  si::Delay fallback = PillRatesOnly();
  fallback.fallback = true;
  ExpectText("fallback rates", "Fallback, Upload delay 150 kb/s, Download delay 280 kb/s",
             si::PillAccessibleName(si::PillContent::Slow(fallback), english));
  ExpectText("label only", "Poor signal", si::PillAccessibleName(slow.LabelOnly(), english));
  ExpectText("recovered", "Internet back to normal",
             si::PillAccessibleName(Recovered(), english));
}

// ---- SdkHost's feed ----------------------------------------------------------------

UR_TEST(slowInternetFeedGatesOnTheRemote) {
  si::Feed feed;
  const uint64_t generation = feed.Bind();
  UR_EXPECT_FALSE(feed.Current().available);
  // the reconnect's replay can land before the remote's edge: kept, not shown
  UR_EXPECT_FALSE(feed.NoteDelay(generation, SlowDelay(0.5)));
  UR_EXPECT_FALSE(feed.Current().available);
  UR_EXPECT_TRUE(feed.NoteRemote(generation, true));
  UR_EXPECT_TRUE(feed.Current().available);
  UR_EXPECT_TRUE(feed.Current().delay == SlowDelay(0.5));
  // the same push again changes nothing
  UR_EXPECT_FALSE(feed.NoteDelay(generation, SlowDelay(0.5)));
  UR_EXPECT_TRUE(feed.NoteDelay(generation, SlowDelay(1.0)));
  // disconnected: nothing shows, and the stale level is forgotten, so a
  // reconnect with no replay shows nothing rather than the old level
  UR_EXPECT_TRUE(feed.NoteRemote(generation, false));
  UR_EXPECT_FALSE(feed.Current().available);
  UR_EXPECT_FALSE(feed.NoteRemote(generation, false));
  UR_EXPECT_TRUE(feed.NoteRemote(generation, true));
  UR_EXPECT_TRUE(feed.Current().available);
  UR_EXPECT_TRUE(feed.Current().delay == si::Delay{});
}

UR_TEST(slowInternetFeedDropsATornDownDevice) {
  si::Feed feed;
  const uint64_t first = feed.Bind();
  UR_EXPECT_TRUE(feed.NoteRemote(first, true));
  UR_EXPECT_TRUE(feed.NoteDelay(first, SlowDelay(0.5)));
  const uint64_t second = feed.Bind();
  UR_EXPECT_TRUE(first != second);
  // a new device starts unknown, and the old one's late calls are dropped
  UR_EXPECT_FALSE(feed.Current().available);
  UR_EXPECT_FALSE(feed.NoteRemote(first, true));
  UR_EXPECT_FALSE(feed.NoteDelay(first, SlowDelay(1.0)));
  UR_EXPECT_FALSE(feed.Current().available);
  UR_EXPECT_TRUE(feed.NoteRemote(second, true));
  UR_EXPECT_TRUE(feed.Current().delay == si::Delay{});
  // cleared (the device is gone, or its tunnel): unavailable, and its calls dropped
  feed.Clear();
  UR_EXPECT_FALSE(feed.Current().available);
  UR_EXPECT_FALSE(feed.NoteRemote(second, true));
  UR_EXPECT_FALSE(feed.NoteDelay(second, SlowDelay(0.5)));
  UR_EXPECT_FALSE(feed.Current().available);
}

// The reads after registering lose to any push that landed since: the push
// is the newer value.
UR_TEST(slowInternetFeedSeedsLoseToPushes) {
  si::Feed feed;
  uint64_t generation = feed.Bind();
  uint64_t delayRevision = feed.DelayRevision();
  uint64_t remoteRevision = feed.RemoteRevision();
  // nothing landed: the reads apply
  UR_EXPECT_TRUE(feed.SeedRemote(generation, remoteRevision, true));
  UR_EXPECT_TRUE(feed.SeedDelay(generation, delayRevision, SlowDelay(0.5)));
  UR_EXPECT_TRUE(feed.Current().available);
  UR_EXPECT_TRUE(feed.Current().delay == SlowDelay(0.5));

  generation = feed.Bind();
  delayRevision = feed.DelayRevision();
  remoteRevision = feed.RemoteRevision();
  // a push and an edge land between the registration and the reads
  UR_EXPECT_FALSE(feed.NoteDelay(generation, SlowDelay(1.0)));
  UR_EXPECT_TRUE(feed.NoteRemote(generation, true));
  // the reads, older, change nothing
  UR_EXPECT_FALSE(feed.SeedRemote(generation, remoteRevision, false));
  UR_EXPECT_FALSE(feed.SeedDelay(generation, delayRevision, si::Delay{}));
  UR_EXPECT_TRUE(feed.Current().available);
  UR_EXPECT_TRUE(feed.Current().delay == SlowDelay(1.0));

  // a push that repeats the value still counts as newer than the read
  generation = feed.Bind();
  delayRevision = feed.DelayRevision();
  UR_EXPECT_TRUE(feed.NoteRemote(generation, true));
  UR_EXPECT_FALSE(feed.NoteDelay(generation, si::Delay{}));
  UR_EXPECT_FALSE(feed.SeedDelay(generation, delayRevision, SlowDelay(0.75)));
  UR_EXPECT_TRUE(feed.Current().delay == si::Delay{});
}

// A refresh is a seed with the revision it was read under: new numbers at the
// same level apply, a repeat changes nothing, an answer read before a push
// loses to it, and one from a device torn down since is dropped.
UR_TEST(slowInternetFeedRefreshLosesToPushes) {
  si::Feed feed;
  const uint64_t generation = feed.Bind();
  UR_EXPECT_TRUE(feed.NoteRemote(generation, true));
  UR_EXPECT_TRUE(feed.NoteDelay(generation, SlowDelay(0.5)));
  uint64_t revision = feed.DelayRevision();
  UR_EXPECT_TRUE(feed.SeedDelay(feed.Generation(), revision, SlowDelay(0.5, 900)));
  UR_EXPECT_TRUE(feed.Current().delay == SlowDelay(0.5, 900));
  revision = feed.DelayRevision();
  UR_EXPECT_FALSE(feed.SeedDelay(feed.Generation(), revision, SlowDelay(0.5, 900)));
  const uint64_t stale = feed.DelayRevision();
  UR_EXPECT_TRUE(feed.NoteDelay(generation, SlowDelay(1.0)));
  UR_EXPECT_FALSE(feed.SeedDelay(feed.Generation(), stale, SlowDelay(0.5, 900)));
  UR_EXPECT_TRUE(feed.Current().delay == SlowDelay(1.0));
  const uint64_t oldGeneration = feed.Generation();
  revision = feed.DelayRevision();
  feed.Clear();
  UR_EXPECT_FALSE(feed.SeedDelay(oldGeneration, revision, SlowDelay(0.25)));
  UR_EXPECT_FALSE(feed.Current().available);
}

// ---- the notification's dwell ----------------------------------------------------

UR_TEST(slowInternetHistoryCountsTheLastMinute) {
  si::SlowHistory history;
  UR_EXPECT_EQ(-1, history.ReachesAtMillis(si::kDwellSlowMillis, 0));
  history.Note(true, 0);
  UR_EXPECT_EQ(0, history.SlowMillis(0));
  UR_EXPECT_EQ(10 * kSecond, history.SlowMillis(10 * kSecond));
  UR_EXPECT_EQ(30 * kSecond, history.ReachesAtMillis(si::kDwellSlowMillis, 10 * kSecond));
  history.Note(false, 20 * kSecond);
  UR_EXPECT_EQ(-1, history.ReachesAtMillis(si::kDwellSlowMillis, 20 * kSecond));
  history.Note(true, 70 * kSecond);
  // [10 s, 70 s] holds 10 s of the first stretch
  UR_EXPECT_EQ(10 * kSecond, history.SlowMillis(70 * kSecond));
  // while that stretch slides out the window gains nothing: 100 s, not 90 s
  UR_EXPECT_EQ(100 * kSecond, history.ReachesAtMillis(si::kDwellSlowMillis, 70 * kSecond));
  UR_EXPECT_EQ(29 * kSecond, history.SlowMillis(99 * kSecond));
  UR_EXPECT_EQ(30 * kSecond, history.SlowMillis(100 * kSecond));
  // a minute later only the current stretch is left
  UR_EXPECT_EQ(60 * kSecond, history.SlowMillis(200 * kSecond));
  history.Clear();
  UR_EXPECT_EQ(0, history.SlowMillis(200 * kSecond));
}

UR_TEST(slowInternetNoticeWaitsForTheDwell) {
  si::NoticeTracker tracker;
  RecordingSink sink;
  tracker.Observe(Slow(), true, 0, sink);
  UR_EXPECT_TRUE(sink.posts.empty());
  UR_EXPECT_EQ(30 * kSecond, tracker.NextDeadlineMillis());
  tracker.Observe(Slow(), true, 30 * kSecond - 1, sink);
  UR_EXPECT_TRUE(sink.posts.empty());
  tracker.Observe(Slow(), true, 30 * kSecond, sink);
  UR_EXPECT_EQ(1u, sink.posts.size());
  UR_EXPECT_TRUE(tracker.phase() == si::NoticeTracker::Phase::Slow);
  UR_EXPECT_TRUE(sink.posts.back() == si::SlowNotice(SlowDelay(0.5)));
  // the feed repeating itself posts nothing more
  for (int i = 1; i <= 10; ++i) tracker.Observe(Slow(), true, (30 + i) * kSecond, sink);
  UR_EXPECT_EQ(1u, sink.posts.size());
  UR_EXPECT_EQ(-1, tracker.NextDeadlineMillis());
  UR_EXPECT_EQ(0, sink.withdraws);
}

// Slow on and off counts each slow stretch toward the 30 s of the last 60 s.
UR_TEST(slowInternetNoticeCountsIntermittentSlowness) {
  si::NoticeTracker tracker;
  RecordingSink sink;
  tracker.Observe(Slow(), true, 0, sink);
  tracker.Observe(Normal(), true, 10 * kSecond, sink);
  UR_EXPECT_EQ(-1, tracker.NextDeadlineMillis());
  tracker.Observe(Slow(), true, 20 * kSecond, sink);
  tracker.Observe(Normal(), true, 30 * kSecond, sink);
  tracker.Observe(Slow(), true, 40 * kSecond, sink);
  UR_EXPECT_TRUE(sink.posts.empty());
  UR_EXPECT_EQ(50 * kSecond, tracker.NextDeadlineMillis());
  tracker.Observe(Slow(), true, 50 * kSecond, sink);
  UR_EXPECT_EQ(1u, sink.posts.size());
}

// Shown, it stays its minute; then "back to normal" for 3 s, then withdrawn.
UR_TEST(slowInternetNoticeStaysThenRecovers) {
  si::NoticeTracker tracker;
  RecordingSink sink;
  tracker.Observe(Slow(), true, 0, sink);
  tracker.Observe(Slow(), true, 30 * kSecond, sink);
  UR_EXPECT_EQ(1u, sink.posts.size());
  // back to 0 ten seconds in: it holds what it said until its minute is up
  tracker.Observe(Normal(), true, 40 * kSecond, sink);
  UR_EXPECT_EQ(1u, sink.posts.size());
  UR_EXPECT_TRUE(tracker.phase() == si::NoticeTracker::Phase::Slow);
  UR_EXPECT_EQ(90 * kSecond, tracker.NextDeadlineMillis());
  tracker.Observe(Normal(), true, 90 * kSecond - 1, sink);
  UR_EXPECT_EQ(1u, sink.posts.size());
  tracker.Observe(Normal(), true, 90 * kSecond, sink);
  UR_EXPECT_EQ(2u, sink.posts.size());
  UR_EXPECT_TRUE(sink.posts.back() == si::RecoveredNotice());
  UR_EXPECT_TRUE(tracker.phase() == si::NoticeTracker::Phase::Recovered);
  UR_EXPECT_EQ(93 * kSecond, tracker.NextDeadlineMillis());
  tracker.Observe(Normal(), true, 93 * kSecond, sink);
  UR_EXPECT_EQ(1, sink.withdraws);
  UR_EXPECT_FALSE(tracker.Shown());
  UR_EXPECT_EQ(-1, tracker.NextDeadlineMillis());
  // the next episode is slow for its own 30 s, whatever came before
  tracker.Observe(Slow(), true, 94 * kSecond, sink);
  UR_EXPECT_EQ(2u, sink.posts.size());
  UR_EXPECT_EQ(124 * kSecond, tracker.NextDeadlineMillis());
  tracker.Observe(Slow(), true, 124 * kSecond, sink);
  UR_EXPECT_EQ(3u, sink.posts.size());
  // and posts the same words again: a new episode is a new post
  UR_EXPECT_TRUE(sink.posts.back() == si::SlowNotice(SlowDelay(0.5)));
}

// A level back to 0 after the minute recovers at once.
UR_TEST(slowInternetNoticeRecoversAtOnceAfterItsMinute) {
  si::NoticeTracker tracker;
  RecordingSink sink;
  tracker.Observe(Slow(), true, 0, sink);
  tracker.Observe(Slow(), true, 30 * kSecond, sink);
  tracker.Observe(Normal(), true, 95 * kSecond, sink);
  UR_EXPECT_EQ(2u, sink.posts.size());
  UR_EXPECT_TRUE(sink.posts.back() == si::RecoveredNotice());
}

// Slow again within the recovered 3 s: the same episode, slow again.
UR_TEST(slowInternetNoticeSlowAgainWhileRecovered) {
  si::NoticeTracker tracker;
  RecordingSink sink;
  tracker.Observe(Slow(), true, 0, sink);
  tracker.Observe(Slow(), true, 30 * kSecond, sink);
  tracker.Observe(Normal(), true, 90 * kSecond, sink);
  tracker.Observe(Slow(), true, 91 * kSecond, sink);
  UR_EXPECT_EQ(3u, sink.posts.size());
  UR_EXPECT_TRUE(sink.posts.back() == si::SlowNotice(SlowDelay(0.5)));
  UR_EXPECT_TRUE(tracker.phase() == si::NoticeTracker::Phase::Slow);
  UR_EXPECT_EQ(0, sink.withdraws);
  UR_EXPECT_EQ(-1, tracker.NextDeadlineMillis());
}

// What it says changes with the level, the numbers and fallback; nothing else
// replaces it.
UR_TEST(slowInternetNoticeReplacesOnChange) {
  si::NoticeTracker tracker;
  RecordingSink sink;
  tracker.Observe(Slow(0.5), true, 0, sink);
  tracker.Observe(Slow(0.5), true, 30 * kSecond, sink);
  UR_EXPECT_EQ(1u, sink.posts.size());
  tracker.Observe(Available(SlowDelay(1.0, 2500, 9000)), true, 31 * kSecond, sink);
  UR_EXPECT_EQ(2u, sink.posts.size());
  UR_EXPECT_EQ(2500, sink.posts.back().upDelayMillis);
  tracker.Observe(Available(SlowDelay(1.0, 2500, 9000)), true, 32 * kSecond, sink);
  UR_EXPECT_EQ(2u, sink.posts.size());
  tracker.Observe(Available(SlowDelay(1.0, 2500, 9000, /*fallback=*/true)), true,
                  33 * kSecond, sink);
  UR_EXPECT_EQ(3u, sink.posts.size());
  UR_EXPECT_TRUE(sink.posts.back().fallback);
  // held through its minute with the level at 0: no zeros posted over it
  tracker.Observe(Normal(), true, 40 * kSecond, sink);
  UR_EXPECT_EQ(3u, sink.posts.size());
  UR_EXPECT_EQ(0, sink.withdraws);
}

// Unavailable (the remote or the session went) or switched off: withdrawn at
// once, never "back to normal", since nothing says it recovered.
UR_TEST(slowInternetNoticeWithdrawsWhenUnavailableOrOff) {
  {
    si::NoticeTracker tracker;
    RecordingSink sink;
    tracker.Observe(Slow(), true, 0, sink);
    tracker.Observe(Slow(), true, 30 * kSecond, sink);
    tracker.Observe(Unavailable(), true, 31 * kSecond, sink);
    UR_EXPECT_EQ(1u, sink.posts.size());
    UR_EXPECT_EQ(1, sink.withdraws);
    UR_EXPECT_FALSE(tracker.Shown());
    UR_EXPECT_EQ(-1, tracker.NextDeadlineMillis());
    // and the dwell starts over once it is back
    tracker.Observe(Slow(), true, 32 * kSecond, sink);
    UR_EXPECT_EQ(62 * kSecond, tracker.NextDeadlineMillis());
    // unavailable again while hidden withdraws nothing
    tracker.Observe(Unavailable(), true, 33 * kSecond, sink);
    UR_EXPECT_EQ(1, sink.withdraws);
  }
  {
    si::NoticeTracker tracker;
    RecordingSink sink;
    tracker.Observe(Slow(), true, 0, sink);
    tracker.Observe(Slow(), true, 30 * kSecond, sink);
    tracker.Observe(Slow(), /*enabled=*/false, 31 * kSecond, sink);
    UR_EXPECT_EQ(1u, sink.posts.size());
    UR_EXPECT_EQ(1, sink.withdraws);
    // switched off, a long slow stretch posts nothing
    for (int s = 32; s <= 200; s += 7) tracker.Observe(Slow(), false, s * kSecond, sink);
    UR_EXPECT_EQ(1u, sink.posts.size());
    UR_EXPECT_EQ(-1, tracker.NextDeadlineMillis());
    // switched back on, it is slow for its own 30 s first
    tracker.Observe(Slow(), true, 201 * kSecond, sink);
    UR_EXPECT_EQ(1u, sink.posts.size());
    UR_EXPECT_EQ(231 * kSecond, tracker.NextDeadlineMillis());
  }
}

// ---- the tray's art ------------------------------------------------------------------

// urnetwork-tray-slow-1..4.png: 32x32 RGBA PNGs, as the other tray art.
// ---- the tray's dwell ----------------------------------------------------------

// The tray follows the notification's dwell, whatever the setting: nothing for
// a stretch shorter than the dwell; then the level as it comes, held through
// the minimum stay; then "back to normal" on the tray's own icon for 3 s.
UR_TEST(slowInternetTrayFollowsTheDwell) {
  si::TraySlowTracker tray;
  tray.Observe(Slow(1.0), 0);
  tray.Observe(Normal(), 20 * kSecond);
  UR_EXPECT_EQ(0, tray.view().level);
  UR_EXPECT_FALSE(tray.view().line.has_value());
  UR_EXPECT_EQ(-1, tray.NextDeadlineMillis());
  // [0 s, 20 s) still counts: 30 s of the last minute are slow at 50 s
  tray.Observe(Slow(0.5), 40 * kSecond);
  UR_EXPECT_EQ(0, tray.view().level);
  UR_EXPECT_EQ(50 * kSecond, tray.NextDeadlineMillis());
  tray.Observe(Slow(0.5), 50 * kSecond);
  UR_EXPECT_EQ(si::Level(Slow(0.5)), tray.view().level);
  UR_EXPECT_TRUE(tray.view().line.has_value() &&
                 std::string(tray.view().line->key) == si::TrayLineCopy(false).key);
  // the level as it comes
  tray.Observe(Slow(1.0), 55 * kSecond);
  UR_EXPECT_EQ(si::Level(Slow(1.0)), tray.view().level);
  // back to 0 inside its minute: it holds what it showed
  tray.Observe(Normal(), 60 * kSecond);
  UR_EXPECT_EQ(si::Level(Slow(1.0)), tray.view().level);
  UR_EXPECT_EQ(110 * kSecond, tray.NextDeadlineMillis());
  tray.Observe(Normal(), 110 * kSecond);
  UR_EXPECT_EQ(0, tray.view().level);
  UR_EXPECT_TRUE(tray.view().line.has_value() &&
                 std::string(tray.view().line->key) == si::RecoveredCopy().key);
  UR_EXPECT_EQ(113 * kSecond, tray.NextDeadlineMillis());
  tray.Observe(Normal(), 113 * kSecond);
  UR_EXPECT_EQ(0, tray.view().level);
  UR_EXPECT_FALSE(tray.view().line.has_value());
  UR_EXPECT_EQ(-1, tray.NextDeadlineMillis());
}

// The fallback line while slow; an unavailable reading clears the tray at once,
// with no recovered moment.
UR_TEST(slowInternetTrayFallbackAndUnavailable) {
  si::TraySlowTracker tray;
  const si::Reading fallback = Available(SlowDelay(1.0, 640, 1234, /*fallback=*/true));
  tray.Observe(fallback, 0);
  tray.Observe(fallback, 30 * kSecond);
  UR_EXPECT_EQ(si::Level(fallback), tray.view().level);
  UR_EXPECT_TRUE(tray.view().line.has_value() &&
                 std::string(tray.view().line->key) == si::TrayLineCopy(true).key);
  tray.Observe(Unavailable(), 31 * kSecond);
  UR_EXPECT_EQ(0, tray.view().level);
  UR_EXPECT_FALSE(tray.view().line.has_value());
  UR_EXPECT_EQ(-1, tray.NextDeadlineMillis());
}

UR_TEST(slowInternetTrayArtIs32PxRgba) {
  for (int level = 1; level <= 4; ++level) {
    const std::string path =
        std::string(UR_ASSETS_DIR) + "/urnetwork-tray-slow-" + std::to_string(level) + ".png";
    std::ifstream in(path, std::ios::binary);
    unsigned char header[26] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    UR_EXPECT_TRUE_MSG(path, in.gcount() == static_cast<std::streamsize>(sizeof(header)));
    static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    bool signature = true;
    for (int i = 0; i < 8; ++i) signature = signature && header[i] == kSignature[i];
    UR_EXPECT_TRUE_MSG(path, signature);
    // IHDR: width and height big-endian at 16 and 20, bit depth 24, colour type 25
    const auto u32 = [&](int at) {
      return (uint32_t(header[at]) << 24) | (uint32_t(header[at + 1]) << 16) |
             (uint32_t(header[at + 2]) << 8) | uint32_t(header[at + 3]);
    };
    UR_EXPECT_EQ(32u, u32(16));
    UR_EXPECT_EQ(32u, u32(20));
    UR_EXPECT_EQ(8, header[24]);
    UR_EXPECT_EQ(6, header[25]);  // RGBA
  }
}
