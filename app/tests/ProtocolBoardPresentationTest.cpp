// Earnings › Leaderboard › Protocol decided from plain data
// (ProtocolBoardPresentation.hpp): the board's numbers, the label chip and
// the rule that an unmeasurable network is never anything but "Not
// measurable", the margin and trend lines, the status banner while binding is
// closed and when the snapshot is stale, what a ranked row says (its name, an
// anonymous claimed network by its UIDs, its badges, dimmed when not
// measurable, tinted inside the line, "You" and the label on the caller's row
// only) and how a screen reader reads it, where the Top 200 line falls, the
// list's screen in every state, the progress chart (up is better, the 200
// line always in range, the nearest day under a press), the claim prompt with
// binding closed and open and the borderline warning, why a network is not
// measurable, the own-status card in every state, the claimed detail sheet,
// and the Earnings page's Top 200 tile. The words are the English sources
// (the catalog with no translation loaded), pinned against po/en.po at the
// end; dates are fixed strings, so nothing here depends on time or locale.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ProtocolBoardPresentation.hpp"

namespace p = urnw::protocol_board;

#define UR_EXPECT_TEXT(expected, actual)                                                    \
  do {                                                                                      \
    const std::string ur_e_ = (expected);                                                   \
    const std::string ur_a_ = (actual);                                                     \
    if (ur_e_ != ur_a_)                                                                     \
      UR_FAIL(std::string(#actual) + ": expected \"" + ur_e_ + "\", got \"" + ur_a_ + "\""); \
  } while (0)

namespace {

// The store's {} substitution (I18n.hpp FormatArgs): "{}" takes the next
// argument, "{0}" "{1}" a given one.
std::string FormatLike(const std::string& pattern, const std::vector<std::string>& args) {
  std::string out;
  size_t next = 0;
  for (size_t i = 0; i < pattern.size();) {
    if (pattern[i] == '{') {
      const size_t close = pattern.find('}', i);
      if (close != std::string::npos) {
        const std::string spec = pattern.substr(i + 1, close - i - 1);
        size_t index = next;
        if (!spec.empty()) index = static_cast<size_t>(std::stoul(spec));
        if (index < args.size()) {
          out += args[index];
          if (spec.empty()) ++next;
          i = close + 1;
          continue;
        }
      }
    }
    out += pattern[i++];
  }
  return out;
}

p::Words EnglishWords() {
  p::Words words;
  words.lookup = [](const p::Text& text) { return std::string(text.english); };
  words.format = &FormatLike;
  words.date = [](int64_t t) { return "D" + std::to_string(t); };
  words.dateTime = [](int64_t t) { return "DT" + std::to_string(t); };
  return words;
}

p::Snapshot SnapshotOf(bool bindingOpen) {
  p::Snapshot snapshot;
  snapshot.id = "s1";
  snapshot.totalRanked = 41230;
  snapshot.projectedCutoff = 41.2;
  snapshot.bindingOpen = bindingOpen;
  snapshot.computedAtUnixSeconds = 1793286245;
  return snapshot;
}

p::Row RowAt(int64_t position, const std::string& name = "kayak-hollow") {
  p::Row row;
  row.position = position;
  row.name = name;
  row.projectedScore = 41.24;
  row.measurable = true;
  row.label = "estimate";
  row.claimPrompt = position <= 200;
  row.margin = position <= 200 ? 0.12 : -0.08;
  return row;
}

p::Fleet FleetOf(int64_t uid) {
  p::Fleet fleet;
  fleet.uid = uid;
  fleet.generation = 2;
  fleet.validToEpoch = 16;
  fleet.replicaScore = 312.4;
  return fleet;
}

p::MeState MeStateOf(const p::Row& row, bool bindingOpen) {
  p::MeState state;
  state.loaded = true;
  state.snapshot = SnapshotOf(bindingOpen);
  p::Me me;
  me.row = row;
  me.row.isMe = true;
  me.soloScore = 21.43;
  me.distinctPrefixes = 18;
  me.sharedPrefixes = 3;
  state.me = me;
  return state;
}

}  // namespace

// ---- the numbers ------------------------------------------------------------------

UR_TEST(ProtocolBoard_TheNumbersAreLocaleIndependent) {
  UR_EXPECT_TEXT("0", p::Grouped(0));
  UR_EXPECT_TEXT("999", p::Grouped(999));
  UR_EXPECT_TEXT("41,230", p::Grouped(41230));
  UR_EXPECT_TEXT("1,234,567", p::Grouped(1234567));
  UR_EXPECT_TEXT("-1,240", p::Grouped(-1240));
  UR_EXPECT_TEXT("#173", p::PositionText(173));
  UR_EXPECT_TEXT("#1,240", p::PositionText(1240));
  UR_EXPECT_TEXT("41.2", p::ScoreText(41.24));
  UR_EXPECT_TEXT("41.3", p::ScoreText(41.25));
  UR_EXPECT_TEXT("1,312.4", p::ScoreText(1312.4));
  UR_EXPECT_TEXT("0.0", p::ScoreText(-0.01));
  UR_EXPECT_TEXT("-2.5", p::ScoreText(-2.5));
  UR_EXPECT_TEXT("12%", p::PercentText(0.124));
  UR_EXPECT_TEXT("8%", p::PercentText(-0.08));
  UR_EXPECT_TEXT("0%", p::PercentText(0.0));
  UR_EXPECT_TEXT("1,500%", p::PercentText(15.0));
  UR_EXPECT_TEXT("1.8%", p::IncentiveText(0.018));
  UR_EXPECT_TEXT("+12%", p::SignedPercentText(0.124));
  UR_EXPECT_TEXT("-8%", p::SignedPercentText(-0.08));
  UR_EXPECT_TEXT("+0%", p::SignedPercentText(0.0));
  UR_EXPECT_TEXT("+0%", p::SignedPercentText(-0.004));  // rounds to the line, never "-0%"
}

// ---- the label chip ---------------------------------------------------------------

UR_TEST(ProtocolBoard_AnUnmeasurableNetworkIsOnlyNotMeasurable) {
  p::Row row = RowAt(10);
  row.label = "likely";
  UR_EXPECT_TRUE(p::LabelOf(row) == p::Label::Likely);
  row.measurable = false;  // Q7: never "likely" when the validator cannot score it
  UR_EXPECT_TRUE(p::LabelOf(row) == p::Label::NotMeasurable);
  row.measurable = true;
  row.label = "not_measurable";
  UR_EXPECT_TRUE(p::LabelOf(row) == p::Label::NotMeasurable);
  row.label = "borderline";
  UR_EXPECT_TRUE(p::LabelOf(row) == p::Label::Borderline);
  row.label = "unlikely";
  UR_EXPECT_TRUE(p::LabelOf(row) == p::Label::Unlikely);
  row.label = "estimate";
  UR_EXPECT_TRUE(p::LabelOf(row) == p::Label::Estimate);
  // a word this app does not know is the most cautious label
  row.label = "certain";
  UR_EXPECT_TRUE(p::LabelOf(row) == p::Label::Estimate);
  UR_EXPECT_TEXT("Estimate", p::LabelText(p::Label::Estimate).english);
  UR_EXPECT_TEXT("Likely", p::LabelText(p::Label::Likely).english);
  UR_EXPECT_TEXT("Borderline", p::LabelText(p::Label::Borderline).english);
  UR_EXPECT_TEXT("Unlikely", p::LabelText(p::Label::Unlikely).english);
  UR_EXPECT_TEXT("Not measurable", p::LabelText(p::Label::NotMeasurable).english);
}

// The "close to the line" warning goes with the Borderline label (Q13); while
// every row is an estimate (Q17), the prompt has its warning without it.
UR_TEST(ProtocolBoard_BorderlineIsTheLabel) {
  p::Row row = RowAt(196);
  row.label = "borderline";
  UR_EXPECT_TRUE(p::IsBorderline(row));
  row.label = "likely";
  row.margin = 0.05;  // the server's label, not the app's arithmetic
  UR_EXPECT_FALSE(p::IsBorderline(row));
  row.label = "estimate";
  row.margin = 0.03;
  UR_EXPECT_FALSE(p::IsBorderline(row));
  row.label = "borderline";
  row.measurable = false;  // an unmeasurable network is only "Not measurable"
  UR_EXPECT_FALSE(p::IsBorderline(row));
}

// ---- the margin and the trend ------------------------------------------------------

UR_TEST(ProtocolBoard_TheMarginSaysWhichSideOfTheLine) {
  const p::Words words = EnglishWords();
  UR_EXPECT_TEXT("12% above the Top 200 line", p::MarginText(0.12, words));
  UR_EXPECT_TEXT("8% below the Top 200 line", p::MarginText(-0.08, words));
  // the 200th network is on the line: inside
  UR_EXPECT_TEXT("0% above the Top 200 line", p::MarginText(0.0, words));
}

UR_TEST(ProtocolBoard_TheTrendSaysHowFarItMoved) {
  const p::Words words = EnglishWords();
  UR_EXPECT_TEXT("up 9 since the last update", p::TrendText(9, words));
  UR_EXPECT_TEXT("down 2 since the last update", p::TrendText(-2, words));
  UR_EXPECT_TEXT("up 1,240 since the last update", p::TrendText(1240, words));
  UR_EXPECT_TEXT("", p::TrendText(0, words));
  UR_EXPECT_TRUE(p::TrendOf(3) == p::Trend::Up);
  UR_EXPECT_TRUE(p::TrendOf(-3) == p::Trend::Down);
  UR_EXPECT_TRUE(p::TrendOf(0) == p::Trend::Flat);
  UR_EXPECT_TEXT("▲", p::TrendArrow(p::Trend::Up));
  UR_EXPECT_TEXT("▼", p::TrendArrow(p::Trend::Down));
  UR_EXPECT_TEXT("", p::TrendArrow(p::Trend::Flat));
}

// ---- the banner ------------------------------------------------------------------------

UR_TEST(ProtocolBoard_TheBannerSaysClaimingIsClosedAndWhenItIsStale) {
  const p::Words words = EnglishWords();
  UR_EXPECT_TRUE(p::BannerLines(std::nullopt, words).empty());
  p::Snapshot snapshot = SnapshotOf(/*bindingOpen=*/false);
  std::vector<std::string> lines = p::BannerLines(snapshot, words);
  UR_EXPECT_EQ(1u, lines.size());
  if (lines.size() == 1u) {
    UR_EXPECT_TEXT("Claiming isn't open yet. You can see where you'd rank now.", lines[0]);
  }
  // binding open and a fresh snapshot: the banner is removed
  snapshot.bindingOpen = true;
  UR_EXPECT_TRUE(p::BannerLines(snapshot, words).empty());
  // stale: when the rankings were last computed
  snapshot.stale = true;
  lines = p::BannerLines(snapshot, words);
  UR_EXPECT_EQ(1u, lines.size());
  if (lines.size() == 1u) UR_EXPECT_TEXT("Rankings last updated DT1793286245.", lines[0]);
  // both
  snapshot.bindingOpen = false;
  lines = p::BannerLines(snapshot, words);
  UR_EXPECT_EQ(2u, lines.size());
  // a stale snapshot whose time did not read says nothing it cannot fill in
  snapshot.computedAtUnixSeconds = 0;
  UR_EXPECT_EQ(1u, p::BannerLines(snapshot, words).size());
}

UR_TEST(ProtocolBoard_TheTabShowsTheListsSnapshotElseMes) {
  p::ListState list;
  p::MeState me;
  UR_EXPECT_FALSE(p::ShownSnapshot(list, me).has_value());
  me.snapshot = SnapshotOf(false);
  me.snapshot->id = "me";
  UR_EXPECT_TEXT("me", p::ShownSnapshot(list, me)->id);
  list.snapshot = SnapshotOf(false);
  list.snapshot->id = "list";
  UR_EXPECT_TEXT("list", p::ShownSnapshot(list, me)->id);
}

// ---- a ranked row ----------------------------------------------------------------------

UR_TEST(ProtocolBoard_ARowIsNamedAnonymousOrByItsUids) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(196, "quiet-harbor");
  UR_EXPECT_TEXT("quiet-harbor", p::RowName(row, words));
  // filtered for display: a bidi override does not reach the label
  row.name = "quiet‮harbor";
  UR_EXPECT_TEXT("quietharbor", p::RowName(row, words));
  row.name = "";
  UR_EXPECT_TEXT("Anonymous", p::RowName(row, words));
  // an anonymous claimed network shows only its UIDs (Q12)
  row.fleets = {FleetOf(77)};
  UR_EXPECT_TEXT("UID 77", p::RowName(row, words));
  row.fleets.push_back(FleetOf(119));
  UR_EXPECT_TEXT("UID 77, UID 119", p::RowName(row, words));
  // a named claimed network keeps its name
  row.name = "cedar-node";
  UR_EXPECT_TEXT("cedar-node", p::RowName(row, words));
}

UR_TEST(ProtocolBoard_ARowSaysItsPlaceNameScoreTrendAndBadges) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(196, "quiet-harbor");
  row.trend = 3;
  row.fleets = {FleetOf(212)};
  const p::RowView view = p::RowViewFor(row, words);
  UR_EXPECT_TEXT("#196", view.positionText);
  UR_EXPECT_TEXT("quiet-harbor", view.name);
  UR_EXPECT_TRUE(view.named);
  UR_EXPECT_TEXT("41.2", view.score);
  UR_EXPECT_TRUE(view.trend == p::Trend::Up);
  UR_EXPECT_TEXT("▲", view.trendArrow);
  UR_EXPECT_TRUE(view.badges == std::vector<std::string>{"UID 212"});
  UR_EXPECT_TRUE(view.claimed);
  UR_EXPECT_FALSE(view.dimmed);
  UR_EXPECT_TRUE(view.aboveLine);
  // not the caller's: no "You", no label chip
  UR_EXPECT_FALSE(view.isMe);
  UR_EXPECT_FALSE(view.label.has_value());
  UR_EXPECT_TEXT("#196, quiet-harbor, 41.2, up 3 since the last update, UID 212",
                 view.accessibleName);
}

UR_TEST(ProtocolBoard_AnUnmeasurableRowIsDimmedAndSaysSo) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(197, "");
  row.measurable = false;
  const p::RowView view = p::RowViewFor(row, words);
  UR_EXPECT_TRUE(view.dimmed);
  UR_EXPECT_TEXT("Not measurable", view.dimmedText);
  UR_EXPECT_FALSE(view.named);
  UR_EXPECT_TEXT("#197, Anonymous, 41.2, Not measurable", view.accessibleName);
}

UR_TEST(ProtocolBoard_OnlyTheCallersRowSaysYouAndCarriesTheLabel) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(173);
  row.isMe = true;
  row.label = "borderline";
  p::RowView view = p::RowViewFor(row, words);
  UR_EXPECT_TRUE(view.isMe);
  UR_EXPECT_TEXT("You", view.youText);
  UR_EXPECT_TRUE(view.label && *view.label == p::Label::Borderline);
  UR_EXPECT_TEXT("Borderline", view.labelText);
  UR_EXPECT_TEXT("#173, You, kayak-hollow, 41.2, Borderline", view.accessibleName);
  // the caller's unmeasurable row says it once
  row.measurable = false;
  view = p::RowViewFor(row, words);
  UR_EXPECT_TEXT("#173, You, kayak-hollow, 41.2, Not measurable", view.accessibleName);
  // an anonymous claimed row reads its UID once, as its name
  p::Row anonymous = RowAt(1, "");
  anonymous.fleets = {FleetOf(81)};
  view = p::RowViewFor(anonymous, words);
  UR_EXPECT_TEXT("#1, UID 81, 41.2", view.accessibleName);
}

UR_TEST(ProtocolBoard_RowsInsideTheHeadSeatsAreTinted) {
  const p::Words words = EnglishWords();
  UR_EXPECT_TRUE(p::RowViewFor(RowAt(1), words).aboveLine);
  UR_EXPECT_TRUE(p::RowViewFor(RowAt(200), words).aboveLine);
  UR_EXPECT_FALSE(p::RowViewFor(RowAt(201), words).aboveLine);
  UR_EXPECT_FALSE(p::RowViewFor(RowAt(400), words).aboveLine);
}

// ---- the Top 200 line -------------------------------------------------------------------

UR_TEST(ProtocolBoard_TheLineFallsBetween200And201) {
  std::vector<p::Row> rows;
  for (int64_t position = 151; position <= 200; ++position) rows.push_back(RowAt(position));
  // only the rows inside are loaded: the line is not reached yet
  UR_EXPECT_FALSE(p::LineIndex(rows).has_value());
  rows.push_back(RowAt(201));
  rows.push_back(RowAt(202));
  const std::optional<size_t> at = p::LineIndex(rows);
  UR_EXPECT_TRUE(at && *at == 50u);
  // from the first row
  std::vector<p::Row> two{RowAt(200), RowAt(201)};
  UR_EXPECT_TRUE(p::LineIndex(two) && *p::LineIndex(two) == 1u);
  // a list that starts past the line has no line in it
  std::vector<p::Row> past{RowAt(201), RowAt(202)};
  UR_EXPECT_FALSE(p::LineIndex(past).has_value());
  UR_EXPECT_FALSE(p::LineIndex({}).has_value());
}

UR_TEST(ProtocolBoard_TheLineReadsTheCutoffScore) {
  const p::Words words = EnglishWords();
  const p::Line line = p::LineFor(SnapshotOf(false), words);
  UR_EXPECT_TEXT("Top 200 line · score 41.2", line.text);
  UR_EXPECT_TEXT("Top 200 line, score 41.2", line.accessibleName);
}

// ---- the list's screen -------------------------------------------------------------------

UR_TEST(ProtocolBoard_TheListsScreenInEveryState) {
  p::ListState list;
  // signed out and never asked: the sign-in line
  p::ListScreen screen = p::ListScreenFor(list, /*signedIn=*/false);
  UR_EXPECT_TRUE(screen.body == p::ListBody::NoSession);
  UR_EXPECT_TEXT("Please login to URnetwork", p::ListBodyText(screen.body)->english);
  // signed in and never loaded: placeholders, with no line of text
  screen = p::ListScreenFor(list, true);
  UR_EXPECT_TRUE(screen.body == p::ListBody::Placeholder);
  UR_EXPECT_FALSE(p::ListBodyText(screen.body).has_value());
  // the first load failed: the retry row
  list.loadFailed = true;
  screen = p::ListScreenFor(list, true);
  UR_EXPECT_TRUE(screen.body == p::ListBody::LoadFailed);
  UR_EXPECT_TEXT("Something went wrong.", p::ListBodyText(screen.body)->english);
  // ...and a retry in flight shows placeholders again
  list.loading = true;
  UR_EXPECT_TRUE(p::ListScreenFor(list, true).body == p::ListBody::Placeholder);
  // loaded and empty
  list = p::ListState{};
  list.loaded = true;
  screen = p::ListScreenFor(list, true);
  UR_EXPECT_TRUE(screen.body == p::ListBody::Empty);
  UR_EXPECT_TEXT("Rankings will appear after the next update.",
                 p::ListBodyText(screen.body)->english);
  // the rows, with more to page in
  for (int64_t position = 1; position <= 50; ++position) list.rows.push_back(RowAt(position));
  list.next = "c50";
  screen = p::ListScreenFor(list, true);
  UR_EXPECT_TRUE(screen.body == p::ListBody::Rows);
  UR_EXPECT_TRUE(screen.canLoadMore);
  UR_EXPECT_FALSE(screen.refreshing || screen.refreshFailed || screen.loadingMore ||
                  screen.moreFailed);
  // a refresh over the rows, then its failure: the rows stay under a notice
  list.loading = true;
  screen = p::ListScreenFor(list, true);
  UR_EXPECT_TRUE(screen.body == p::ListBody::Rows && screen.refreshing && !screen.refreshFailed);
  list.loading = false;
  list.loadFailed = true;
  screen = p::ListScreenFor(list, true);
  UR_EXPECT_TRUE(screen.body == p::ListBody::Rows && screen.refreshFailed);
  // the next page, and its failure
  list.loadFailed = false;
  list.loadingMore = true;
  UR_EXPECT_TRUE(p::ListScreenFor(list, true).loadingMore);
  list.loadingMore = false;
  list.moreFailed = true;
  UR_EXPECT_TRUE(p::ListScreenFor(list, true).moreFailed);
  // the end, and the top 400
  list.next.clear();
  UR_EXPECT_FALSE(p::ListScreenFor(list, true).canLoadMore);
  list.next = "c400";
  list.rows.back().position = 400;
  UR_EXPECT_FALSE(p::ListScreenFor(list, true).canLoadMore);
  // signed out with the public list loaded: the list
  UR_EXPECT_TRUE(p::ListScreenFor(list, false).body == p::ListBody::Rows);
}

// ---- the progress chart -------------------------------------------------------------------

UR_TEST(ProtocolBoard_ADayReadsAsItsNumber) {
  UR_EXPECT_TRUE(p::DayNumber("1970-01-01") && *p::DayNumber("1970-01-01") == 0);
  UR_EXPECT_TRUE(p::DayNumber("2026-10-29") && *p::DayNumber("2026-10-29") == 20755);
  UR_EXPECT_FALSE(p::DayNumber("2026-13-01").has_value());
  UR_EXPECT_FALSE(p::DayNumber("2026-02-29").has_value());
  UR_EXPECT_TRUE(p::DayNumber("2028-02-29").has_value());
  UR_EXPECT_FALSE(p::DayNumber("2026-10-29T00:00:00Z").has_value());
  UR_EXPECT_FALSE(p::DayNumber("").has_value());
}

UR_TEST(ProtocolBoard_TheChartPutsBetterRanksHigher) {
  const p::Words words = EnglishWords();
  std::vector<p::HistoryPoint> history{
      {"2026-08-01", 412, 20.0}, {"2026-09-15", 260, 30.0}, {"2026-10-29", 173, 41.2}};
  const p::Chart chart = p::ChartFor(history, 173, words);
  UR_EXPECT_TRUE(chart.show);
  UR_EXPECT_EQ(3u, chart.points.size());
  if (chart.points.size() == 3u) {
    // oldest first, the last day at the right edge
    UR_EXPECT_NEAR(1.0, chart.points[2].x, 1e-9);
    UR_EXPECT_TRUE(chart.points[0].x < chart.points[1].x && chart.points[1].x < chart.points[2].x);
    // up means better: #173 is above #260, which is above #412
    UR_EXPECT_TRUE(chart.points[2].y < chart.points[1].y && chart.points[1].y < chart.points[0].y);
    // the 200 line is between #173 and #260
    UR_EXPECT_TRUE(chart.points[2].y < chart.lineY && chart.lineY < chart.points[1].y);
    // no point on an edge
    for (const p::ChartPoint& point : chart.points) {
      UR_EXPECT_TRUE(point.y > 0.0 && point.y < 1.0);
    }
    UR_EXPECT_TEXT("#173", chart.points[2].rankText);
    UR_EXPECT_TEXT("D" + std::to_string(int64_t{20755} * 86400 + 12 * 3600),
                   chart.points[2].dayText);
  }
  UR_EXPECT_TEXT("Projected rank, last 90 days", chart.title);
  UR_EXPECT_TEXT("Top 200 line", chart.lineLabel);
  UR_EXPECT_TEXT("Projected rank over 90 days, now 173", chart.accessibleName);
}

// The 200 line is always in range, whichever side of it the network was on;
// a network far outside keeps its climb readable on the log scale.
UR_TEST(ProtocolBoard_TheTop200LineIsAlwaysInRange) {
  const p::Words words = EnglishWords();
  const p::Chart inside = p::ChartFor({{"2026-10-28", 3, 0}, {"2026-10-29", 1, 0}}, 1, words);
  UR_EXPECT_TRUE(inside.lineY > 0.0 && inside.lineY < 1.0);
  UR_EXPECT_TRUE(inside.points.back().y < inside.lineY);
  const p::Chart outside =
      p::ChartFor({{"2026-08-01", 40000, 0}, {"2026-10-29", 4210, 0}}, 4210, words);
  UR_EXPECT_TRUE(outside.lineY > 0.0 && outside.lineY < outside.points.back().y);
  UR_EXPECT_TRUE(outside.points.back().y < outside.points.front().y);
  // on the line every day: still a line with room around it
  const p::Chart flat = p::ChartFor({{"2026-10-28", 200, 0}, {"2026-10-29", 200, 0}}, 200, words);
  UR_EXPECT_TRUE(flat.lineY > 0.0 && flat.lineY < 1.0);
  UR_EXPECT_NEAR(flat.lineY, flat.points.back().y, 1e-9);
}

// The chart spans the last 90 days: older days are left out, a repeated day
// is drawn once with its last rank, and a day or rank that does not read is
// skipped.
UR_TEST(ProtocolBoard_TheChartSpansTheLast90Days) {
  const p::Words words = EnglishWords();
  std::vector<p::HistoryPoint> history{
      {"2026-07-31", 900, 0},  // 90 days before the last day: outside
      {"2026-08-01", 500, 0},  // the 90th day: the left edge
      {"2026-10-29", 210, 0}, {"2026-10-29", 190, 0},
      {"yesterday", 300, 0},   {"2026-10-20", 0, 0},
  };
  const p::Chart chart = p::ChartFor(history, 190, words);
  UR_EXPECT_EQ(2u, chart.points.size());
  if (chart.points.size() == 2u) {
    UR_EXPECT_NEAR(0.0, chart.points[0].x, 1e-9);
    UR_EXPECT_EQ(500, chart.points[0].rank);
    UR_EXPECT_EQ(190, chart.points[1].rank);
  }
  // no history: nothing to draw, and still a summary of where it is now
  const p::Chart none = p::ChartFor({}, 4210, words);
  UR_EXPECT_FALSE(none.show);
  UR_EXPECT_TEXT("Projected rank over 90 days, now 4,210", none.accessibleName);
  // nothing at all: no summary either
  UR_EXPECT_TEXT("", p::ChartFor({}, 0, words).accessibleName);
}

UR_TEST(ProtocolBoard_APressOnTheChartNamesTheNearestDay) {
  const p::Words words = EnglishWords();
  const p::Chart chart = p::ChartFor(
      {{"2026-08-01", 412, 0}, {"2026-09-15", 260, 0}, {"2026-10-29", 173, 0}}, 173, words);
  UR_EXPECT_TRUE(p::NearestPoint(chart, 0.0) && *p::NearestPoint(chart, 0.0) == 0u);
  UR_EXPECT_TRUE(p::NearestPoint(chart, 0.95) && *p::NearestPoint(chart, 0.95) == 2u);
  UR_EXPECT_TRUE(p::NearestPoint(chart, 0.5) && *p::NearestPoint(chart, 0.5) == 1u);
  UR_EXPECT_FALSE(p::NearestPoint(p::Chart{}, 0.5).has_value());
  p::ChartPoint point;
  point.rankText = "#173";
  point.dayText = "Oct 29, 2026";
  UR_EXPECT_TEXT("#173 · Oct 29, 2026", p::PointText(point));
  point.dayText.clear();
  UR_EXPECT_TEXT("#173", p::PointText(point));
}

// ---- the claim prompt ---------------------------------------------------------------------

UR_TEST(ProtocolBoard_WithBindingClosedTheClaimPromptIsANote) {
  const p::Words words = EnglishWords();
  const p::ClaimPrompt prompt = p::ClaimPromptFor(RowAt(173), SnapshotOf(false), words);
  UR_EXPECT_TRUE(prompt.show);
  UR_EXPECT_TEXT("You're projected in the top 200.", prompt.headline);
  UR_EXPECT_FALSE(prompt.button);
  UR_EXPECT_TEXT("Claiming opens soon on the Earnings page.", prompt.soon);
  UR_EXPECT_TEXT("", prompt.borderline);
  UR_EXPECT_TEXT("A claimed fleet that ranks 201 or lower earns nothing.", prompt.warning);
  // no snapshot yet: closed, nothing invites registering early
  UR_EXPECT_FALSE(p::ClaimPromptFor(RowAt(173), std::nullopt, words).button);
}

UR_TEST(ProtocolBoard_WithBindingOpenTheClaimPromptIsTheButton) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(196);
  row.margin = 0.03;
  row.label = "borderline";
  const p::ClaimPrompt prompt = p::ClaimPromptFor(row, SnapshotOf(true), words);
  UR_EXPECT_TRUE(prompt.show);
  UR_EXPECT_TRUE(prompt.button);
  UR_EXPECT_TEXT("Claim on Earnings", prompt.buttonText);
  UR_EXPECT_TEXT("", prompt.soon);
  UR_EXPECT_TEXT("You're close to the line, and rankings change every tempo.", prompt.borderline);
  UR_EXPECT_TEXT("A claimed fleet that ranks 201 or lower earns nothing.", prompt.warning);
}

// The server's claim_prompt alone decides (CONTRACT, amended 2026-10-29): it
// is false for a network that already has an active claimed fleet, and the
// app never puts its own measurable-and-inside arithmetic in its place.
UR_TEST(ProtocolBoard_TheServersFlagAloneDecidesTheClaimPrompt) {
  const p::Words words = EnglishWords();
  // a claimed network inside the top 200, as the server sends it: no prompt,
  // in the card or on the tile, which shows the claim instead
  p::Row claimed = RowAt(10);
  claimed.fleets = {FleetOf(90)};
  claimed.claimPrompt = false;
  UR_EXPECT_FALSE(p::ClaimPromptFor(claimed, SnapshotOf(true), words).show);
  const p::MeState state = MeStateOf(claimed, /*bindingOpen=*/true);
  const p::Card card = p::CardFor(state, std::nullopt, true, words);
  UR_EXPECT_TRUE(card.body == p::CardBody::Ranked);
  UR_EXPECT_FALSE(card.claim.show);
  UR_EXPECT_FALSE(card.claim.button);
  const p::Tile tile = p::TileFor(state, true, words);
  UR_EXPECT_TRUE(tile.kind == p::TileKind::Claimed);
  UR_EXPECT_FALSE(tile.showClaim);
  // an unclaimed network inside the top 200 the server does not prompt
  p::Row unprompted = RowAt(150);
  unprompted.claimPrompt = false;
  const p::ClaimPrompt none = p::ClaimPromptFor(unprompted, SnapshotOf(true), words);
  UR_EXPECT_FALSE(none.show);
  UR_EXPECT_TRUE(none.headline.empty() && none.warning.empty() && !none.button);
  UR_EXPECT_TRUE(p::TileFor(MeStateOf(unprompted, true), true, words).kind == p::TileKind::Hidden);
  // the flag set where the app's own arithmetic would have said no: the
  // server's call stands
  p::Row past = RowAt(201);
  past.claimPrompt = true;
  UR_EXPECT_TRUE(p::ClaimPromptFor(past, SnapshotOf(true), words).show);
  p::Row unmeasured = RowAt(150);
  unmeasured.measurable = false;
  unmeasured.claimPrompt = true;
  UR_EXPECT_TRUE(p::ClaimPromptFor(unmeasured, SnapshotOf(false), words).show);
}

UR_TEST(ProtocolBoard_TheCardSaysWhyANetworkIsNotMeasurable) {
  const p::Words words = EnglishWords();
  p::Me me;
  me.row = RowAt(4210);
  UR_EXPECT_TEXT("", p::NotMeasurableText(me, words));
  me.row.measurable = false;
  me.notMeasurableReason = "not_in_cohort";
  UR_EXPECT_TEXT("Your devices aren't measured by the validator yet.",
                 p::NotMeasurableText(me, words));
  me.notMeasurableReason = "shared_egress";
  UR_EXPECT_TEXT("Your devices share an IP with another network.", p::NotMeasurableText(me, words));
  me.notMeasurableReason = "not_providing";
  UR_EXPECT_TEXT("Turn on providing to be ranked.", p::NotMeasurableText(me, words));
  me.notMeasurableReason = "";
  UR_EXPECT_TEXT("", p::NotMeasurableText(me, words));
}

// ---- the own-status card --------------------------------------------------------------------

UR_TEST(ProtocolBoard_TheCardInEveryState) {
  const p::Words words = EnglishWords();
  p::MeState state;
  p::Card card = p::CardFor(state, std::nullopt, /*signedIn=*/false, words);
  UR_EXPECT_TRUE(card.body == p::CardBody::SignIn);
  UR_EXPECT_TEXT("Please login to URnetwork", card.bodyText);
  card = p::CardFor(state, std::nullopt, true, words);
  UR_EXPECT_TRUE(card.body == p::CardBody::Placeholder);
  state.loadFailed = true;
  card = p::CardFor(state, std::nullopt, true, words);
  UR_EXPECT_TRUE(card.body == p::CardBody::LoadFailed);
  UR_EXPECT_TEXT("Something went wrong.", card.bodyText);
  state.loading = true;  // the retry in flight
  UR_EXPECT_TRUE(p::CardFor(state, std::nullopt, true, words).body == p::CardBody::Placeholder);
  state = p::MeState{};
  state.loaded = true;  // answered, with no place on the board
  card = p::CardFor(state, std::nullopt, true, words);
  UR_EXPECT_TRUE(card.body == p::CardBody::NotRanked);
  UR_EXPECT_TEXT("Turn on providing to be ranked.", card.bodyText);
}

UR_TEST(ProtocolBoard_TheCardShowsThePositionMarginLabelAndWhy) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(173);
  row.trend = 9;
  row.tag = "\xF0\x9F\x90\xB8";  // 🐸
  const p::MeState state = MeStateOf(row, /*bindingOpen=*/false);
  const p::Card card = p::CardFor(state, std::nullopt, true, words);
  UR_EXPECT_TRUE(card.body == p::CardBody::Ranked);
  UR_EXPECT_TEXT("\xF0\x9F\x90\xB8", card.tag);
  UR_EXPECT_TEXT("kayak-hollow", card.name);
  UR_EXPECT_TRUE(card.named && card.publicName);
  UR_EXPECT_TEXT("Show my network name", card.showNameText);
  UR_EXPECT_TEXT("#173", card.position);
  UR_EXPECT_TEXT("projected", card.projected);
  UR_EXPECT_TEXT("of 41,230 ranked networks", card.ofRanked);
  UR_EXPECT_TEXT("12% above the Top 200 line", card.margin);
  UR_EXPECT_TRUE(card.aboveLine);
  UR_EXPECT_TEXT("up 9 since the last update", card.trendText);
  UR_EXPECT_TRUE(card.label == p::Label::Estimate);
  UR_EXPECT_TEXT("Estimate", card.labelText);
  UR_EXPECT_TEXT("Distinct IP prefixes: 18. Shared with other networks: 3.", card.why);
  UR_EXPECT_TEXT("If you claimed alone today: 21.4.", card.solo);
  UR_EXPECT_TEXT("", card.notMeasurable);
  UR_EXPECT_TRUE(card.claim.show && !card.claim.button);
  UR_EXPECT_TEXT("Claiming opens soon on the Earnings page.", card.claim.soon);
}

UR_TEST(ProtocolBoard_AnAnonymousOrUnmeasurableCard) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(4210, "");
  row.measurable = false;
  p::MeState state = MeStateOf(row, true);
  state.me->notMeasurableReason = "shared_egress";
  const p::Card card = p::CardFor(state, std::nullopt, true, words);
  UR_EXPECT_TEXT("Anonymous", card.name);
  UR_EXPECT_FALSE(card.publicName);
  UR_EXPECT_TEXT("#4,210", card.position);
  UR_EXPECT_TEXT("8% below the Top 200 line", card.margin);
  UR_EXPECT_FALSE(card.aboveLine);
  UR_EXPECT_TRUE(card.label == p::Label::NotMeasurable);
  UR_EXPECT_TEXT("Your devices share an IP with another network.", card.notMeasurable);
  // no claim prompt for a network the validator cannot measure
  UR_EXPECT_FALSE(card.claim.show);
  // the list's snapshot, when there is one, is the one the card counts by
  p::Snapshot list = SnapshotOf(true);
  list.totalRanked = 50000;
  UR_EXPECT_TEXT("of 50,000 ranked networks", p::CardFor(state, list, true, words).ofRanked);
}

// ---- the claimed detail sheet ---------------------------------------------------------------

UR_TEST(ProtocolBoard_TheDetailSheetListsEachFleet) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(1, "");
  p::Fleet fleet = FleetOf(81);
  fleet.validatorRank = 1;
  fleet.validatorRankOf = 142;
  fleet.validatorScore = 312.4;
  fleet.selected = true;
  fleet.incentive = 0.018;
  fleet.incentiveStale = true;
  row.fleets = {fleet};
  const p::Detail detail = p::DetailFor(row, words);
  UR_EXPECT_TEXT("UID 81", detail.title);
  UR_EXPECT_TEXT("Anonymous · claimed", detail.subtitle);
  UR_EXPECT_EQ(1u, detail.facts.size());
  if (!detail.facts.empty()) {
    UR_EXPECT_TEXT("Projected rank", detail.facts[0].label);
    UR_EXPECT_TEXT("#1", detail.facts[0].value);
  }
  UR_EXPECT_EQ(1u, detail.fleets.size());
  if (detail.fleets.size() == 1u) {
    const p::FleetDetail& out = detail.fleets[0];
    UR_EXPECT_TEXT("UID 81", out.title);
    UR_EXPECT_TRUE(out.hasSelection && out.selected);
    UR_EXPECT_TEXT("Selected", out.selectionText);
    std::vector<std::pair<std::string, std::string>> facts;
    for (const p::Fact& fact : out.facts) facts.emplace_back(fact.label, fact.value);
    const std::vector<std::pair<std::string, std::string>> expected{
        {"Validator rank", "#1 of 142 claimed"}, {"Validator score", "312.4"},
        {"Board replica score", "312.4"},        {"On-chain incentive", "1.8% · stale"},
        {"Generation", "2"},                     {"Claim valid until", "Epoch 16"}};
    UR_EXPECT_TRUE(facts == expected);
  }
  UR_EXPECT_TEXT("On-chain incentive lags the validator's ranking.", detail.note);
  UR_EXPECT_TEXT("Close", detail.closeText);
}

// What the validator has not said yet is left out, never shown as zero.
UR_TEST(ProtocolBoard_TheDetailSheetLeavesOutWhatIsNotKnown) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(202, "cedar-node");
  p::Fleet fleet = FleetOf(77);
  fleet.validToEpoch = 0;
  fleet.validatorRank = 5;  // a rank without its count
  row.fleets = {fleet, FleetOf(78)};
  const p::Detail detail = p::DetailFor(row, words);
  // titled by the claim, the network under it
  UR_EXPECT_TEXT("UID 77, UID 78", detail.title);
  UR_EXPECT_TEXT("cedar-node · claimed", detail.subtitle);
  UR_EXPECT_EQ(2u, detail.fleets.size());
  if (detail.fleets.size() == 2u) {
    UR_EXPECT_FALSE(detail.fleets[0].hasSelection);
    std::vector<std::string> labels;
    for (const p::Fact& fact : detail.fleets[0].facts) labels.push_back(fact.label);
    UR_EXPECT_TRUE(labels == (std::vector<std::string>{"Validator rank", "Board replica score",
                                                       "Generation"}));
    UR_EXPECT_TEXT("#5", detail.fleets[0].facts[0].value);
    UR_EXPECT_TEXT("UID 78", detail.fleets[1].title);
  }
  // no incentive anywhere: no note about its lag
  UR_EXPECT_TEXT("", detail.note);
}

// ---- the Earnings page's Top 200 tile ----------------------------------------------------------

UR_TEST(ProtocolBoard_TheTileHidesUntilThereIsAPromptOrAClaim) {
  const p::Words words = EnglishWords();
  p::MeState state;
  UR_EXPECT_TRUE(p::TileFor(state, true, words).kind == p::TileKind::Hidden);
  state = MeStateOf(RowAt(173), false);
  UR_EXPECT_TRUE(p::TileFor(state, /*signedIn=*/false, words).kind == p::TileKind::Hidden);
  // outside the top 200 and unclaimed: no tile
  state = MeStateOf(RowAt(250), false);
  UR_EXPECT_TRUE(p::TileFor(state, true, words).kind == p::TileKind::Hidden);
  // inside but not measurable: the server prompts nobody it cannot measure
  p::Row row = RowAt(150);
  row.measurable = false;
  row.claimPrompt = false;
  state = MeStateOf(row, true);
  UR_EXPECT_TRUE(p::TileFor(state, true, words).kind == p::TileKind::Hidden);
  // a failed load that never landed: no tile
  state = p::MeState{};
  state.loadFailed = true;
  UR_EXPECT_TRUE(p::TileFor(state, true, words).kind == p::TileKind::Hidden);
}

UR_TEST(ProtocolBoard_WithBindingClosedTheTileSaysClaimingOpensSoon) {
  const p::Words words = EnglishWords();
  const p::Tile tile = p::TileFor(MeStateOf(RowAt(173), /*bindingOpen=*/false), true, words);
  UR_EXPECT_TRUE(tile.kind == p::TileKind::Unclaimed);
  UR_EXPECT_TEXT("Top 200", tile.title);
  UR_EXPECT_TEXT("Estimate", tile.labelText);
  UR_EXPECT_TEXT("#173", tile.position);
  UR_EXPECT_TEXT("projected · +12%", tile.projectedLine);
  UR_EXPECT_TEXT(
      "Ranked like the SN25 validator's Top 200, by the distinct IP prefixes your devices provide "
      "from. Head miners earn SN25α every tempo.",
      tile.body);
  UR_EXPECT_FALSE(tile.bindingOpen);
  UR_EXPECT_TEXT("Claiming opens soon", tile.soonTitle);
  UR_EXPECT_TEXT(
      "We'll let you know when you can claim your spot. A claimed fleet that ranks 201 or lower "
      "earns nothing.",
      tile.soonBody);
  // nothing invites registering early: no claim button, no Sign fleet binding
  UR_EXPECT_FALSE(tile.showClaim);
  UR_EXPECT_FALSE(tile.showSign);
  UR_EXPECT_TEXT("See the Protocol leaderboard", tile.seeBoard);
}

UR_TEST(ProtocolBoard_WithBindingOpenTheTileKeepsTheClaimFlow) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(196);
  row.margin = 0.03;
  row.label = "borderline";
  const p::Tile tile = p::TileFor(MeStateOf(row, /*bindingOpen=*/true), true, words);
  UR_EXPECT_TRUE(tile.kind == p::TileKind::Unclaimed);
  UR_EXPECT_TRUE(tile.showClaim);
  UR_EXPECT_TEXT("Claim your spot", tile.claimText);
  UR_EXPECT_TRUE(tile.showSign);
  UR_EXPECT_TEXT("Sign fleet binding", tile.signText);
  UR_EXPECT_TEXT("You're close to the line, and rankings change every tempo.", tile.borderline);
  UR_EXPECT_TEXT("A claimed fleet that ranks 201 or lower earns nothing.", tile.warning);
  UR_EXPECT_TEXT("", tile.soonTitle);
}

UR_TEST(ProtocolBoard_AClaimedTileShowsTheFleetsUidRankAndSelection) {
  const p::Words words = EnglishWords();
  p::Row row = RowAt(1, "");
  row.margin = 0.9;
  p::Fleet fleet = FleetOf(81);
  fleet.validatorRank = 1;
  fleet.selected = true;
  row.fleets = {fleet, FleetOf(92)};
  const p::Tile tile = p::TileFor(MeStateOf(row, true), true, words);
  UR_EXPECT_TRUE(tile.kind == p::TileKind::Claimed);
  UR_EXPECT_EQ(2u, tile.fleets.size());
  if (tile.fleets.size() == 2u) {
    UR_EXPECT_TEXT("UID 81 · rank #1", tile.fleets[0].status);
    UR_EXPECT_TEXT("Selected", tile.fleets[0].selection);
    UR_EXPECT_TRUE(tile.fleets[0].selected);
    UR_EXPECT_TEXT("UID 92", tile.fleets[1].status);
    UR_EXPECT_TEXT("", tile.fleets[1].selection);
    UR_EXPECT_FALSE(tile.fleets[1].selected);
  }
  UR_EXPECT_TEXT("Emission is paid to your coldkey directly. Bindings renew per epoch.",
                 tile.boundDetail);
  UR_EXPECT_TEXT("", tile.demotion);
  UR_EXPECT_TRUE(tile.showSign);
  UR_EXPECT_FALSE(tile.showClaim);
  // close to the line: the eviction warning
  row.margin = 0.1;
  UR_EXPECT_TEXT("Your score is close to the eviction floor. Add routable IPs to keep the spot.",
                 p::TileFor(MeStateOf(row, true), true, words).demotion);
  // a claimed network outside the top 200 still has its tile
  row = RowAt(250);
  row.fleets = {FleetOf(7)};
  UR_EXPECT_TRUE(p::TileFor(MeStateOf(row, false), true, words).kind == p::TileKind::Claimed);
  UR_EXPECT_FALSE(p::TileFor(MeStateOf(row, false), true, words).showSign);
}

// A 404 or any error from GET /sn/top/me (before the route is deployed, or a
// bad moment) leaves the tile hidden, or as the last answer left it: the tile
// has no error state, and nothing on it says something went wrong. The card is
// where the failure shows, with its retry.
UR_TEST(ProtocolBoard_AFailedOwnPlaceLeavesTheTileHiddenOrAsItWas) {
  const p::Words words = EnglishWords();
  // the first answer failed (the binding's state after a 404): hidden
  p::MeState failed;
  failed.loadFailed = true;
  UR_EXPECT_TRUE(p::TileFor(failed, true, words).kind == p::TileKind::Hidden);
  // its retry out: still hidden, never a placeholder or an error
  failed.loading = true;
  UR_EXPECT_TRUE(p::TileFor(failed, true, words).kind == p::TileKind::Hidden);
  // answered, with no place on the board: hidden
  p::MeState unranked;
  unranked.loaded = true;
  UR_EXPECT_TRUE(p::TileFor(unranked, true, words).kind == p::TileKind::Hidden);
  // a refresh that failed after an answer: the last answer, as it was
  p::MeState kept = MeStateOf(RowAt(173), /*bindingOpen=*/false);
  kept.loadFailed = true;
  const p::Tile tile = p::TileFor(kept, true, words);
  UR_EXPECT_TRUE(tile.kind == p::TileKind::Unclaimed);
  UR_EXPECT_TEXT("#173", tile.position);
  UR_EXPECT_TEXT("Claiming opens soon", tile.soonTitle);
  const std::string wrong = p::kSomethingWentWrongText.english;
  for (const std::string& text :
       {tile.title, tile.labelText, tile.position, tile.projectedLine, tile.body, tile.soonTitle,
        tile.soonBody, tile.claimText, tile.signText, tile.borderline, tile.warning,
        tile.boundDetail, tile.demotion, tile.seeBoard}) {
    UR_EXPECT_TRUE_MSG(text, text != wrong);
  }
  // the card, by contrast, says the first load failed and offers a retry
  failed.loading = false;
  const p::Card card = p::CardFor(failed, std::nullopt, true, words);
  UR_EXPECT_TRUE(card.body == p::CardBody::LoadFailed);
  UR_EXPECT_TEXT(wrong, card.bodyText);
}

// ---- every word is the catalog's ----------------------------------------------------

UR_TEST(ProtocolBoard_EveryKeyAndEnglishIsTheCatalogs) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../po/en.po", std::ios::binary);
  UR_EXPECT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string catalog = buffer.str();
  const std::vector<p::Text> texts = {
      p::kTabText,
      p::kBannerClosedText,
      p::kBannerStaleText,
      p::kProjectedText,
      p::kOfRankedText,
      p::kAboveLineText,
      p::kBelowLineText,
      p::kTrendUpText,
      p::kTrendDownText,
      p::kWhyText,
      p::kSoloText,
      p::kYouText,
      p::kLabelEstimateText,
      p::kLabelLikelyText,
      p::kLabelBorderlineText,
      p::kLabelUnlikelyText,
      p::kLabelNotMeasurableText,
      p::kChartTitleText,
      p::kChartLineText,
      p::kChartA11yText,
      p::kClaimInsideText,
      p::kClaimButtonText,
      p::kClaimSoonText,
      p::kClaimBorderlineText,
      p::kClaimWarningText,
      p::kNotMeasurableCohortText,
      p::kNotMeasurableSharedText,
      p::kNotMeasurableProvidingText,
      p::kLineText,
      p::kLineA11yText,
      p::kUidText,
      p::kExplainerText,
      p::kEmptyText,
      p::kDetailClaimedText,
      p::kDetailProjectedRankText,
      p::kDetailValidatorRankText,
      p::kDetailValidatorRankValueText,
      p::kDetailValidatorScoreText,
      p::kDetailReplicaScoreText,
      p::kDetailIncentiveText,
      p::kDetailStaleText,
      p::kDetailGenerationText,
      p::kDetailValidUntilText,
      p::kDetailEpochText,
      p::kDetailSelectedText,
      p::kDetailNotSelectedText,
      p::kDetailIncentiveNoteText,
      p::kTileProjectedText,
      p::kTileBodyText,
      p::kTileClaimingSoonText,
      p::kTileClaimingSoonBodyText,
      p::kTileSeeBoardText,
      p::kTop200Text,
      p::kClaimSpotText,
      p::kSignBindingText,
      p::kBoundStatusText,
      p::kBoundDetailText,
      p::kDemotionText,
      p::kShowNameText,
      p::kAnonymousText,
      p::kCloseText,
      p::kTryAgainText,
      p::kLoadingText,
      p::kSignInText,
      p::kSomethingWentWrongText,
  };
  // the po escapes a quote in the English
  const auto poEscape = [](const std::string& english) {
    std::string out;
    for (const char c : english) {
      if (c == '"' || c == '\\') out += '\\';
      out += c;
    }
    return out;
  };
  for (const p::Text& text : texts) {
    const std::string entry = std::string("msgctxt \"") + text.key + "\"\nmsgid \"" +
                              poEscape(text.english) + "\"\n";
    UR_EXPECT_TRUE_MSG(entry, catalog.find(entry) != std::string::npos);
  }
}
