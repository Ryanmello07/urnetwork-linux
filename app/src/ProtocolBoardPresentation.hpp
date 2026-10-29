// Earnings › Leaderboard › Protocol, decided without GTK or the SDK: the board
// the SN25 validator would pick its Top 200 from (GET /sn/top) and the
// caller's own place on it (GET /sn/top/me), as apple/LEADERTOP.md presents
// them. What the status banner says, what the own-status card shows (its
// position, margin, trend, label chip, progress chart, why line and claim
// prompt), what each ranked row says and where the Top 200 line falls, what
// a claimed row's detail sheet lists, what the Earnings page's Top 200 tile
// shows, and which state each surface is in. ProtocolBoard.cpp copies the
// sdk's answers into the plain data below on the GTK main loop and renders
// what these functions return; it holds no rule of its own
// (tests/ProtocolBoardPresentationTest.cpp).
//
// Every string is a store key and its English (the gettext msgid), which the
// page looks up; tests/CatalogLookupTest.cpp reads each {key, English} pair
// here. A network's name and tag are text from outside the app, so they are
// shown filtered, as plain text, never markup.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ApiKeysPresentation.hpp"  // the RFC 3339 and civil-date readers
#include "DisplayText.hpp"

namespace urnw::protocol_board {

// A store key and its English.
struct Text {
  const char* key = "";
  const char* english = "";
};

// ---- the board, as plain data (sdk SnTopSnapshot, SnTopRow, SnTopFleet, SnTopMe) ----

// The snapshot every row and `me` were computed in.
struct Snapshot {
  std::string id;
  int64_t tempoIndex = 0;
  int64_t computedAtUnixSeconds = 0;  // 0 when the time did not read
  int64_t settlementEpoch = 0;
  int64_t totalRanked = 0;
  int64_t headSeats = 200;
  int64_t claimedFleets = 0;
  int64_t openSeats = 0;
  double projectedCutoff = 0;  // the 200th unit's projected score: the Top 200 line
  std::optional<double> claimedCutoff;
  bool bindingOpen = false;
  std::string bindingNote;  // the server's words, for the log; the banner is the store's
  std::optional<double> precisionAt200;
  bool stale = false;  // computed more than 3 tempos ago
};

// One fleet a claimed network bound, with the validator's view of it.
struct Fleet {
  int64_t uid = 0;
  int64_t generation = 0;
  int64_t validToEpoch = 0;
  double replicaScore = 0;
  std::optional<double> validatorScore;
  std::optional<int64_t> validatorRank;
  std::optional<int64_t> validatorRankOf;
  std::optional<bool> selected;
  std::optional<double> incentive;  // the UID's on-chain Incentive, a fraction
  bool incentiveStale = false;
};

// One ranked network.
struct Row {
  int64_t position = 0;
  std::string name;  // "" when anonymous
  std::string tag;   // the emoji tag, may be ""
  double projectedScore = 0;
  double margin = 0;  // a fraction of the projected cutoff: + above the line, - below
  int64_t trend = 0;  // positions moved since the previous tempo, + up
  int64_t prefixes = 0;
  int64_t sharedPrefixes = 0;
  std::string label;  // the server's word: estimate | likely | borderline | unlikely | not_measurable
  bool measurable = false;
  bool claimPrompt = false;  // measurable and projected inside the top 200
  bool isMe = false;
  std::vector<Fleet> fleets;  // empty when unclaimed
};

// One day of the caller's progress.
struct HistoryPoint {
  std::string day;  // YYYY-MM-DD
  int64_t rank = 0;
  double score = 0;
};

// The caller's network in detail (GET /sn/top/me).
struct Me {
  Row row;
  double soloScore = 0;  // the score if nobody sharing its prefixes claimed
  int64_t distinctPrefixes = 0;
  int64_t sharedPrefixes = 0;
  int64_t sharedWithUnits = 0;
  std::string notMeasurableReason;  // "" | not_in_cohort | shared_egress | not_providing
  std::vector<HistoryPoint> history;
};

// The ranked list and the requests about it (ProtocolBoardBinding.hpp).
struct ListState {
  std::optional<Snapshot> snapshot;
  std::vector<Row> rows;  // in position order, the top 400 at most
  std::string next;       // the next page's cursor; "" at the end
  bool loaded = false;       // a first page has landed at least once
  bool loading = false;      // a first page (a load or a refresh) is out
  bool loadFailed = false;   // the latest first page failed or was given up on
  bool loadingMore = false;  // a next page is out
  bool moreFailed = false;   // the latest next page failed or was given up on
};

// The caller's own place, and the request about it.
struct MeState {
  std::optional<Snapshot> snapshot;
  std::optional<Me> me;  // nullopt when the network is not ranked
  bool loaded = false;
  bool loading = false;
  bool loadFailed = false;
};

// ---- the words ------------------------------------------------------------------

inline constexpr Text kTabText{"leaderboard_tab_protocol", "Protocol"};
inline constexpr Text kBannerClosedText{
    "leaderboard_protocol_banner_closed",
    "Claiming isn't open yet. You can see where you'd rank now."};
inline constexpr Text kBannerStaleText{"leaderboard_protocol_banner_stale",
                                       "Rankings last updated {}."};
// the card's position: "#173", then "projected", then "of 41,230 ranked networks"
inline constexpr Text kProjectedText{"leaderboard_protocol_projected", "projected"};
inline constexpr Text kOfRankedText{"leaderboard_protocol_of_ranked", "of {} ranked networks"};
inline constexpr Text kAboveLineText{"leaderboard_protocol_above_line",
                                     "{} above the Top 200 line"};
inline constexpr Text kBelowLineText{"leaderboard_protocol_below_line",
                                     "{} below the Top 200 line"};
inline constexpr Text kTrendUpText{"leaderboard_protocol_trend_up",
                                   "up {} since the last update"};
inline constexpr Text kTrendDownText{"leaderboard_protocol_trend_down",
                                     "down {} since the last update"};
inline constexpr Text kWhyText{"leaderboard_protocol_why",
                               "Distinct IP prefixes: {0}. Shared with other networks: {1}."};
inline constexpr Text kSoloText{"leaderboard_protocol_solo", "If you claimed alone today: {}."};
inline constexpr Text kYouText{"leaderboard_protocol_you", "You"};
// the label chips
inline constexpr Text kLabelEstimateText{"leaderboard_protocol_label_estimate", "Estimate"};
inline constexpr Text kLabelLikelyText{"leaderboard_protocol_label_likely", "Likely"};
inline constexpr Text kLabelBorderlineText{"leaderboard_protocol_label_borderline", "Borderline"};
inline constexpr Text kLabelUnlikelyText{"leaderboard_protocol_label_unlikely", "Unlikely"};
inline constexpr Text kLabelNotMeasurableText{"leaderboard_protocol_label_not_measurable",
                                              "Not measurable"};
// the progress chart
inline constexpr Text kChartTitleText{"leaderboard_protocol_chart_title",
                                      "Projected rank, last 90 days"};
inline constexpr Text kChartLineText{"leaderboard_protocol_chart_line", "Top 200 line"};
inline constexpr Text kChartA11yText{"leaderboard_protocol_chart_a11y",
                                     "Projected rank over 90 days, now {}"};
// the claim prompt
inline constexpr Text kClaimInsideText{"leaderboard_protocol_claim_inside",
                                       "You're projected in the top 200."};
inline constexpr Text kClaimButtonText{"leaderboard_protocol_claim_button", "Claim on Earnings"};
inline constexpr Text kClaimSoonText{"leaderboard_protocol_claim_soon",
                                     "Claiming opens soon on the Earnings page."};
inline constexpr Text kClaimBorderlineText{
    "leaderboard_protocol_claim_borderline",
    "You're close to the line, and rankings change every tempo."};
inline constexpr Text kClaimWarningText{"leaderboard_protocol_claim_warning",
                                        "A claimed fleet that ranks 201 or lower earns nothing."};
// why a network is not measurable
inline constexpr Text kNotMeasurableCohortText{"leaderboard_protocol_not_measurable_cohort",
                                               "Your devices aren't measured by the validator yet."};
inline constexpr Text kNotMeasurableSharedText{"leaderboard_protocol_not_measurable_shared",
                                               "Your devices share an IP with another network."};
inline constexpr Text kNotMeasurableProvidingText{"leaderboard_protocol_not_measurable_providing",
                                                  "Turn on providing to be ranked."};
// the list
inline constexpr Text kLineText{"leaderboard_protocol_line", "Top 200 line · score {}"};
inline constexpr Text kLineA11yText{"leaderboard_protocol_line_a11y", "Top 200 line, score {}"};
inline constexpr Text kUidText{"leaderboard_protocol_uid", "UID {}"};
inline constexpr Text kExplainerText{
    "leaderboard_protocol_explainer",
    "Ranked the way the SN25 validator picks its Top 200: by how many distinct IP prefixes your "
    "devices provide from, with shared prefixes split between the networks on them. Updated about "
    "every 72 minutes. These are projections until you claim."};
inline constexpr Text kEmptyText{"leaderboard_protocol_empty",
                                 "Rankings will appear after the next update."};
// the claimed detail sheet
inline constexpr Text kDetailClaimedText{"leaderboard_protocol_detail_claimed", "claimed"};
inline constexpr Text kDetailProjectedRankText{"leaderboard_protocol_detail_projected_rank",
                                               "Projected rank"};
inline constexpr Text kDetailValidatorRankText{"leaderboard_protocol_detail_validator_rank",
                                               "Validator rank"};
inline constexpr Text kDetailValidatorRankValueText{
    "leaderboard_protocol_detail_validator_rank_value", "#{0} of {1} claimed"};
inline constexpr Text kDetailValidatorScoreText{"leaderboard_protocol_detail_validator_score",
                                                "Validator score"};
inline constexpr Text kDetailReplicaScoreText{"leaderboard_protocol_detail_replica_score",
                                              "Board replica score"};
inline constexpr Text kDetailIncentiveText{"leaderboard_protocol_detail_incentive",
                                           "On-chain incentive"};
inline constexpr Text kDetailStaleText{"leaderboard_protocol_detail_stale", "stale"};
inline constexpr Text kDetailGenerationText{"leaderboard_protocol_detail_generation",
                                            "Generation"};
inline constexpr Text kDetailValidUntilText{"leaderboard_protocol_detail_valid_until",
                                            "Claim valid until"};
inline constexpr Text kDetailEpochText{"leaderboard_protocol_detail_epoch", "Epoch {}"};
inline constexpr Text kDetailSelectedText{"leaderboard_protocol_detail_selected", "Selected"};
inline constexpr Text kDetailNotSelectedText{"leaderboard_protocol_detail_not_selected",
                                             "Not selected"};
inline constexpr Text kDetailIncentiveNoteText{"leaderboard_protocol_detail_incentive_note",
                                               "On-chain incentive lags the validator's ranking."};
// the Earnings page's Top 200 tile
inline constexpr Text kTileProjectedText{"earnings_top200_projected", "projected · {}"};
inline constexpr Text kTileBodyText{
    "earnings_top200_body",
    "Ranked like the SN25 validator's Top 200, by the distinct IP prefixes your devices provide "
    "from. Head miners earn SN25α every tempo."};
inline constexpr Text kTileClaimingSoonText{"earnings_top200_claiming_soon",
                                            "Claiming opens soon"};
inline constexpr Text kTileClaimingSoonBodyText{
    "earnings_top200_claiming_soon_body",
    "We'll let you know when you can claim your spot. A claimed fleet that ranks 201 or lower "
    "earns nothing."};
inline constexpr Text kTileSeeBoardText{"earnings_top200_see_board",
                                        "See the Protocol leaderboard"};
// the existing words the board and the tile reuse
inline constexpr Text kTop200Text{"top200", "Top 200"};
inline constexpr Text kClaimSpotText{"claim_your_spot", "Claim your spot"};
inline constexpr Text kSignBindingText{"top200_sign_binding", "Sign fleet binding"};
inline constexpr Text kBoundStatusText{"top200_bound_status", "UID {0} · rank #{1}"};
inline constexpr Text kBoundDetailText{
    "top200_bound_detail", "Emission is paid to your coldkey directly. Bindings renew per epoch."};
inline constexpr Text kDemotionText{
    "top200_demotion_warning",
    "Your score is close to the eviction floor. Add routable IPs to keep the spot."};
inline constexpr Text kShowNameText{"show_on_points_leaderboard", "Show my network name"};
inline constexpr Text kAnonymousText{"anonymous", "Anonymous"};
inline constexpr Text kCloseText{"close", "Close"};
inline constexpr Text kTryAgainText{"try_again", "Try again"};
inline constexpr Text kLoadingText{"loading", "Loading..."};
inline constexpr Text kSignInText{"please_login_to_urnetwork", "Please login to URnetwork"};
inline constexpr Text kSomethingWentWrongText{"something_went_wrong", "Something went wrong."};

// What the platform lends the texts: the catalog (T_), the store's {}
// substitution (I18n.hpp FormatArgs), and the renderings of a time
// (Formatters.hpp LocalDate, LocalDateTime).
struct Words {
  std::function<std::string(const Text&)> lookup;
  std::function<std::string(const std::string& pattern, const std::vector<std::string>& args)>
      format;
  std::function<std::string(int64_t unixSeconds)> date;
  std::function<std::string(int64_t unixSeconds)> dateTime;
};

// ---- the board's rules -----------------------------------------------------------

// The validator's head seats, which the store's words name ("Top 200").
inline constexpr int64_t kHeadSeats = 200;
// The list shows the top 400 (Q5), paged 50 at a time.
inline constexpr int64_t kListPositions = 400;
inline constexpr int kPageSize = 50;
// Q13's M: within 20% of the line is borderline.
inline constexpr double kBorderlineMargin = 0.20;
// The progress chart spans the last 90 days (Q11).
inline constexpr int64_t kHistoryDays = 90;

// ---- the numbers, locale-independent (the store owns the words, not the digits) ---

// "41,230"
inline std::string Grouped(int64_t value) {
  std::string digits = std::to_string(value < 0 ? -value : value);
  std::string out;
  const size_t lead = digits.size() % 3;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i != 0 && (i + 3 - lead) % 3 == 0) out += ',';
    out += digits[i];
  }
  return value < 0 ? "-" + out : out;
}

// "#173"
inline std::string PositionText(int64_t position) { return "#" + Grouped(position); }

// A score with one decimal: "41.2", "1,312.4".
inline std::string ScoreText(double score) {
  if (!std::isfinite(score)) return "-";
  const double tenths = std::round(std::fabs(score) * 10.0);
  const int64_t whole = static_cast<int64_t>(tenths / 10.0);
  const int64_t decimal = static_cast<int64_t>(tenths - static_cast<double>(whole) * 10.0);
  const bool negative = score < 0 && tenths > 0;
  return (negative ? "-" : "") + Grouped(whole) + "." + std::to_string(decimal);
}

// A fraction as a whole percent of its size: 0.124 -> "12%", -0.08 -> "8%".
inline std::string PercentText(double fraction) {
  if (!std::isfinite(fraction)) return "-";
  return Grouped(static_cast<int64_t>(std::llround(std::fabs(fraction) * 100.0))) + "%";
}

// A margin with its side: 0.124 -> "+12%", -0.08 -> "-8%"; the 200th, on the
// line, "+0%" (the Earnings tile's "projected · +12%").
inline std::string SignedPercentText(double fraction) {
  if (!std::isfinite(fraction)) return "-";
  const int64_t percent = static_cast<int64_t>(std::llround(fraction * 100.0));
  return (percent < 0 ? "-" : "+") + Grouped(percent < 0 ? -percent : percent) + "%";
}

// An incentive fraction with one decimal of a percent: 0.018 -> "1.8%".
inline std::string IncentiveText(double fraction) {
  if (!std::isfinite(fraction)) return "-";
  return ScoreText(fraction * 100.0) + "%";
}

// ---- the label chip ---------------------------------------------------------------

enum class Label { Estimate, Likely, Borderline, Unlikely, NotMeasurable };

// The row's label: a network the validator cannot measure is never anything but
// "Not measurable" (Q7), whatever word came with it; an unknown word reads as
// the most cautious label, an estimate.
inline Label LabelOf(const Row& row) {
  if (!row.measurable || row.label == "not_measurable") return Label::NotMeasurable;
  if (row.label == "likely") return Label::Likely;
  if (row.label == "borderline") return Label::Borderline;
  if (row.label == "unlikely") return Label::Unlikely;
  return Label::Estimate;
}

inline Text LabelText(Label label) {
  switch (label) {
    case Label::Estimate:
      return kLabelEstimateText;
    case Label::Likely:
      return kLabelLikelyText;
    case Label::Borderline:
      return kLabelBorderlineText;
    case Label::Unlikely:
      return kLabelUnlikelyText;
    case Label::NotMeasurable:
      return kLabelNotMeasurableText;
  }
  return kLabelEstimateText;
}

// The claim prompt's extra "close to the line" warning goes with the
// Borderline label (Q13; the server labels |margin| < 20% borderline once the
// board's precision allows labels). Until then every row inside is an
// estimate and gets the prompt and its warning without it (Q17).
inline bool IsBorderline(const Row& row) { return LabelOf(row) == Label::Borderline; }

// ---- the margin and the trend ------------------------------------------------------

// "12% above the Top 200 line" at or above it; "8% below the Top 200 line" under it.
inline std::string MarginText(double margin, const Words& words) {
  const Text& pattern = margin >= 0 ? kAboveLineText : kBelowLineText;
  return words.format(words.lookup(pattern), {PercentText(margin)});
}

enum class Trend { Flat, Up, Down };

inline Trend TrendOf(int64_t trend) {
  return trend > 0 ? Trend::Up : trend < 0 ? Trend::Down : Trend::Flat;
}

// "up 9 since the last update"; nothing for a network that did not move.
inline std::string TrendText(int64_t trend, const Words& words) {
  if (trend == 0) return std::string();
  const Text& pattern = trend > 0 ? kTrendUpText : kTrendDownText;
  return words.format(words.lookup(pattern), {Grouped(trend < 0 ? -trend : trend)});
}

// The small arrow beside a score; "" for no move.
inline const char* TrendArrow(Trend trend) {
  switch (trend) {
    case Trend::Up:
      return "▲";  // ▲
    case Trend::Down:
      return "▼";  // ▼
    case Trend::Flat:
      return "";
  }
  return "";
}

// ---- the status banner ----------------------------------------------------------------

// The banner's lines over the tab: "Claiming isn't open yet..." while binding
// is closed (the neutral style, never the error style), and "Rankings last
// updated {time}." for a snapshot computed more than 3 tempos ago. No snapshot
// yet, no banner; binding open and fresh, no banner.
inline std::vector<std::string> BannerLines(const std::optional<Snapshot>& snapshot,
                                            const Words& words) {
  std::vector<std::string> lines;
  if (!snapshot) return lines;
  if (!snapshot->bindingOpen) lines.push_back(words.lookup(kBannerClosedText));
  if (snapshot->stale && snapshot->computedAtUnixSeconds > 0) {
    lines.push_back(words.format(words.lookup(kBannerStaleText),
                                 {words.dateTime(snapshot->computedAtUnixSeconds)}));
  }
  return lines;
}

// The snapshot the tab shows: the list's once it has one, else the one `me`
// came with.
inline std::optional<Snapshot> ShownSnapshot(const ListState& list, const MeState& me) {
  if (list.snapshot) return list.snapshot;
  return me.snapshot;
}

// ---- a ranked row ------------------------------------------------------------------------

// "UID 81": a claimed fleet's badge.
inline std::string UidText(int64_t uid, const Words& words) {
  return words.format(words.lookup(kUidText), {std::to_string(uid)});
}

// What a row is called: its name, filtered for display; an anonymous claimed
// network by its fleets' UIDs ("UID 81", Q12); any other anonymous network
// "Anonymous".
inline std::string RowName(const Row& row, const Words& words) {
  const std::string name = api_keys::Trimmed(SanitizeExternalDisplayText(row.name));
  if (!name.empty()) return name;
  if (!row.fleets.empty()) {
    std::string uids;
    for (const Fleet& fleet : row.fleets) {
      if (!uids.empty()) uids += ", ";
      uids += UidText(fleet.uid, words);
    }
    return uids;
  }
  return words.lookup(kAnonymousText);
}

struct RowView {
  int64_t position = 0;
  std::string positionText;  // "#173"
  std::string name;          // the name, "UID 81" or "Anonymous"
  bool named = false;        // the network shows its own name (else a stand-in, muted)
  std::string tag;           // the emoji tag, filtered; "" for none
  std::string score;         // "41.2"
  Trend trend = Trend::Flat;
  std::string trendArrow;    // "▲", "▼" or ""
  std::string trendText;     // "up 9 since the last update"; ""
  std::vector<std::string> badges;  // "UID 81", one per fleet
  bool claimed = false;      // opens the claimed detail sheet
  bool dimmed = false;       // not measurable: dimmed, with a small icon
  std::string dimmedText;    // "Not measurable", the icon's accessible name
  bool aboveLine = false;    // inside the top 200: the faint accent tint
  bool isMe = false;         // the caller's row: highlighted, with "You"
  std::string youText;       // "You" on the caller's row
  std::optional<Label> label;  // the label chip, on the caller's row only
  std::string labelText;
  std::string accessibleName;  // everything above, for a screen reader
};

inline RowView RowViewFor(const Row& row, const Words& words) {
  RowView view;
  view.position = row.position;
  view.positionText = PositionText(row.position);
  view.name = RowName(row, words);
  view.named = !api_keys::Trimmed(SanitizeExternalDisplayText(row.name)).empty();
  view.tag = api_keys::Trimmed(SanitizeExternalDisplayText(row.tag));
  view.score = ScoreText(row.projectedScore);
  view.trend = TrendOf(row.trend);
  view.trendArrow = TrendArrow(view.trend);
  view.trendText = TrendText(row.trend, words);
  for (const Fleet& fleet : row.fleets) view.badges.push_back(UidText(fleet.uid, words));
  view.claimed = !row.fleets.empty();
  view.dimmed = !row.measurable;
  if (view.dimmed) view.dimmedText = words.lookup(kLabelNotMeasurableText);
  view.aboveLine = row.position >= 1 && row.position <= kHeadSeats;
  view.isMe = row.isMe;
  if (view.isMe) {
    view.youText = words.lookup(kYouText);
    view.label = LabelOf(row);
    view.labelText = words.lookup(LabelText(*view.label));
  }
  // the order a sighted reader takes the row in
  std::vector<std::string> parts{view.positionText};
  if (view.isMe) parts.push_back(view.youText);
  parts.push_back(view.name);
  parts.push_back(view.score);
  if (!view.trendText.empty()) parts.push_back(view.trendText);
  if (view.claimed && view.named) {
    for (const std::string& badge : view.badges) parts.push_back(badge);
  }
  if (view.dimmed) parts.push_back(view.dimmedText);
  if (view.label && *view.label != Label::NotMeasurable) parts.push_back(view.labelText);
  for (const std::string& part : parts) {
    if (part.empty()) continue;
    if (!view.accessibleName.empty()) view.accessibleName += ", ";
    view.accessibleName += part;
  }
  return view;
}

// ---- the Top 200 line ----------------------------------------------------------------------

// Where the line falls in the loaded rows: before the index of the first row
// past the head seats, when a row at or inside them is loaded right before it.
// nullopt while the loaded rows have not reached it.
inline std::optional<size_t> LineIndex(const std::vector<Row>& rows) {
  for (size_t i = 1; i < rows.size(); ++i) {
    if (rows[i - 1].position <= kHeadSeats && rows[i].position > kHeadSeats) return i;
  }
  return std::nullopt;
}

struct Line {
  std::string text;            // "Top 200 line · score 41.2"
  std::string accessibleName;  // "Top 200 line, score 41.2"
};

inline Line LineFor(const Snapshot& snapshot, const Words& words) {
  const std::string score = ScoreText(snapshot.projectedCutoff);
  Line line;
  line.text = words.format(words.lookup(kLineText), {score});
  line.accessibleName = words.format(words.lookup(kLineA11yText), {score});
  return line;
}

// ---- the list's screen -------------------------------------------------------------------

enum class ListBody {
  NoSession,    // signed out, and the public list never landed: the sign-in line
  Placeholder,  // never loaded: placeholder rows of the real rows' size
  Rows,         // the list
  Empty,        // loaded, and the snapshot ranks nobody yet
  LoadFailed,   // the first load failed: the retry row
};

struct ListScreen {
  ListBody body = ListBody::Placeholder;
  bool refreshing = false;     // a refresh is out over the rows shown
  bool refreshFailed = false;  // the rows kept, under a notice that the refresh failed
  bool loadingMore = false;    // the next page is out: the footer's progress
  bool moreFailed = false;     // the next page failed: the footer's retry
  bool canLoadMore = false;    // more of the top 400 is there to page in
};

inline ListScreen ListScreenFor(const ListState& list, bool signedIn) {
  ListScreen screen;
  if (!list.loaded) {
    if (!signedIn && !list.loading) {
      screen.body = ListBody::NoSession;
    } else {
      // a load in flight shows placeholders, even over the last attempt's failure
      screen.body = !list.loading && list.loadFailed ? ListBody::LoadFailed : ListBody::Placeholder;
    }
    return screen;
  }
  screen.body = list.rows.empty() ? ListBody::Empty : ListBody::Rows;
  screen.refreshing = list.loading;
  screen.refreshFailed = list.loadFailed && !list.loading;
  if (screen.body == ListBody::Rows) {
    screen.loadingMore = list.loadingMore;
    screen.moreFailed = list.moreFailed && !list.loadingMore;
    screen.canLoadMore = !list.next.empty() && list.rows.back().position < kListPositions;
  }
  return screen;
}

// The line a list without rows shows; nothing for the rows or the placeholders.
inline std::optional<Text> ListBodyText(ListBody body) {
  switch (body) {
    case ListBody::NoSession:
      return kSignInText;
    case ListBody::Placeholder:
      return std::nullopt;
    case ListBody::Rows:
      return std::nullopt;
    case ListBody::Empty:
      return kEmptyText;
    case ListBody::LoadFailed:
      return kSomethingWentWrongText;
  }
  return std::nullopt;
}

// ---- the progress chart ------------------------------------------------------------------

// Days since 1970-01-01 for "YYYY-MM-DD"; nullopt when it does not read.
inline std::optional<int64_t> DayNumber(const std::string& day) {
  using api_keys::detail::Digits;
  if (day.size() != 10 || day[4] != '-' || day[7] != '-') return std::nullopt;
  const std::optional<int> year = Digits(day, 0, 4);
  const std::optional<int> month = Digits(day, 5, 2);
  const std::optional<int> date = Digits(day, 8, 2);
  if (!year || !month || !date || *month < 1 || *month > 12 || *date < 1 ||
      *date > api_keys::detail::DaysInMonth(*year, *month)) {
    return std::nullopt;
  }
  return api_keys::detail::DaysFromCivil(*year, *month, *date);
}

struct ChartPoint {
  double x = 0;  // 0 = 90 days before the last day, 1 = the last day
  double y = 0;  // 0 = the best rank shown (the top), 1 = the worst (the bottom)
  int64_t rank = 0;
  std::string rankText;  // "#173"
  std::string dayText;   // the day, as the locale writes a date
};

struct Chart {
  bool show = false;  // at least one day to draw
  std::vector<ChartPoint> points;  // oldest first
  double lineY = 0;   // the Top 200 line, on the same scale
  std::string title;           // "Projected rank, last 90 days"
  std::string lineLabel;       // "Top 200 line"
  std::string accessibleName;  // "Projected rank over 90 days, now 173"
};

// The caller's daily projected rank over the last 90 days, with the Top 200
// line across. Up means better: the y axis runs from the best rank shown at
// the top to the worst at the bottom, on a log scale so a climb from #40,000
// and the last steps around #200 both read; the 200 line is always inside the
// range. `currentRank` is the row's position (0 when unranked), which the
// accessible summary names.
inline Chart ChartFor(const std::vector<HistoryPoint>& history, int64_t currentRank,
                      const Words& words) {
  Chart chart;
  chart.title = words.lookup(kChartTitleText);
  chart.lineLabel = words.lookup(kChartLineText);
  struct Day {
    int64_t number;
    int64_t rank;
  };
  std::vector<Day> days;
  for (const HistoryPoint& point : history) {
    const std::optional<int64_t> number = DayNumber(point.day);
    if (!number || point.rank < 1) continue;
    days.push_back(Day{*number, point.rank});
  }
  std::stable_sort(days.begin(), days.end(),
                   [](const Day& a, const Day& b) { return a.number < b.number; });
  // one point per day: a repeated day keeps its last rank
  std::vector<Day> unique;
  for (const Day& day : days) {
    if (!unique.empty() && unique.back().number == day.number) {
      unique.back() = day;
    } else {
      unique.push_back(day);
    }
  }
  const int64_t now = currentRank > 0 ? currentRank : (unique.empty() ? 0 : unique.back().rank);
  if (now > 0) {
    chart.accessibleName = words.format(words.lookup(kChartA11yText), {Grouped(now)});
  }
  if (unique.empty()) return chart;
  const int64_t last = unique.back().number;
  const int64_t first = last - (kHistoryDays - 1);
  int64_t best = kHeadSeats;
  int64_t worst = kHeadSeats;
  for (const Day& day : unique) {
    if (day.number < first) continue;
    best = std::min(best, day.rank);
    worst = std::max(worst, day.rank);
  }
  // a little room above and below, so no point sits on an edge
  const double top = std::log(std::max(1.0, static_cast<double>(best) / 1.25));
  const double bottom = std::log(static_cast<double>(worst) * 1.25);
  const double span = bottom - top > 1e-9 ? bottom - top : 1.0;
  const auto yOf = [&](int64_t rank) {
    return std::clamp((std::log(static_cast<double>(rank)) - top) / span, 0.0, 1.0);
  };
  for (const Day& day : unique) {
    if (day.number < first) continue;
    ChartPoint point;
    point.x = static_cast<double>(day.number - first) / static_cast<double>(kHistoryDays - 1);
    point.y = yOf(day.rank);
    point.rank = day.rank;
    point.rankText = PositionText(day.rank);
    // noon UTC is the same date in every zone the locale's date is written in
    point.dayText = words.date(day.number * 86400 + 12 * 3600);
    chart.points.push_back(std::move(point));
  }
  chart.lineY = yOf(kHeadSeats);
  chart.show = !chart.points.empty();
  return chart;
}

// The point a press at `x` (0..1 across the chart) names: the nearest day.
// nullopt for a chart with nothing drawn.
inline std::optional<size_t> NearestPoint(const Chart& chart, double x) {
  if (chart.points.empty()) return std::nullopt;
  size_t nearest = 0;
  for (size_t i = 1; i < chart.points.size(); ++i) {
    if (std::fabs(chart.points[i].x - x) < std::fabs(chart.points[nearest].x - x)) nearest = i;
  }
  return nearest;
}

// What a press on the chart shows: "#173 · Oct 12, 2026".
inline std::string PointText(const ChartPoint& point) {
  return point.dayText.empty() ? point.rankText : point.rankText + " · " + point.dayText;
}

// ---- the own-status card -------------------------------------------------------------------

enum class CardBody {
  SignIn,       // signed out: a sign-in prompt in the card's place
  Placeholder,  // never loaded: placeholders of the card's size
  LoadFailed,   // the first load failed: the retry row
  NotRanked,    // loaded, and the network is not on the board
  Ranked,       // the card
};

// The claim prompt: only for a measurable network projected inside the top 200.
struct ClaimPrompt {
  bool show = false;
  std::string headline;    // "You're projected in the top 200."
  bool button = false;     // binding open: "Claim on Earnings", the screen's one primary button
  std::string buttonText;
  std::string soon;        // binding closed: "Claiming opens soon on the Earnings page."
  std::string borderline;  // "" unless within 20% of the line
  std::string warning;     // every prompt: "A claimed fleet that ranks 201 or lower earns nothing."
};

inline ClaimPrompt ClaimPromptFor(const Row& row, const std::optional<Snapshot>& snapshot,
                                  const Words& words) {
  ClaimPrompt prompt;
  // the server's flag, and never for a network the validator cannot measure or
  // one outside the head seats, whatever the flag says
  if (!row.claimPrompt || !row.measurable || row.position < 1 || row.position > kHeadSeats) {
    return prompt;
  }
  prompt.show = true;
  prompt.headline = words.lookup(kClaimInsideText);
  // binding is closed until the snapshot says it is open: nothing invites
  // registering early
  const bool open = snapshot && snapshot->bindingOpen;
  if (open) {
    prompt.button = true;
    prompt.buttonText = words.lookup(kClaimButtonText);
  } else {
    prompt.soon = words.lookup(kClaimSoonText);
  }
  if (IsBorderline(row)) prompt.borderline = words.lookup(kClaimBorderlineText);
  prompt.warning = words.lookup(kClaimWarningText);
  return prompt;
}

// Why the validator cannot score the network yet; "" when it can, or when the
// server gave no reason.
inline std::string NotMeasurableText(const Me& me, const Words& words) {
  if (me.row.measurable) return std::string();
  if (me.notMeasurableReason == "not_in_cohort") return words.lookup(kNotMeasurableCohortText);
  if (me.notMeasurableReason == "shared_egress") return words.lookup(kNotMeasurableSharedText);
  if (me.notMeasurableReason == "not_providing") {
    return words.lookup(kNotMeasurableProvidingText);
  }
  return std::string();
}

struct Card {
  CardBody body = CardBody::Placeholder;
  // the header: the tag and the name others see (or "Anonymous"), and the
  // "Show my network name" switch shared with Points
  std::string tag;
  std::string name;
  bool named = false;
  std::string showNameText;
  bool publicName = false;  // what the switch shows: the network's name is on the boards
  // the position
  std::string position;  // "#173"
  std::string projected;  // "projected"
  std::string ofRanked;   // "of 41,230 ranked networks"; "" before the snapshot says
  // the margin to the line, and the trend
  std::string margin;  // "12% above the Top 200 line"
  bool aboveLine = false;
  Trend trend = Trend::Flat;
  std::string trendText;
  Label label = Label::Estimate;
  std::string labelText;
  Chart chart;
  // why this score: counts only, never IPs
  std::string why;   // "Distinct IP prefixes: 18. Shared with other networks: 3."
  std::string solo;  // "If you claimed alone today: 21.4."
  std::string notMeasurable;  // why the validator cannot score it; ""
  ClaimPrompt claim;
  std::string bodyText;  // the line the sign-in, failed and not ranked bodies show
};

inline Card CardFor(const MeState& state, const std::optional<Snapshot>& snapshot, bool signedIn,
                    const Words& words) {
  Card card;
  card.showNameText = words.lookup(kShowNameText);
  if (!signedIn) {
    card.body = CardBody::SignIn;
    card.bodyText = words.lookup(kSignInText);
    return card;
  }
  if (!state.loaded) {
    card.body = !state.loading && state.loadFailed ? CardBody::LoadFailed : CardBody::Placeholder;
    if (card.body == CardBody::LoadFailed) card.bodyText = words.lookup(kSomethingWentWrongText);
    return card;
  }
  if (!state.me) {
    // on no board: nothing the network provides is ranked
    card.body = CardBody::NotRanked;
    card.bodyText = words.lookup(kNotMeasurableProvidingText);
    return card;
  }
  const Me& me = *state.me;
  const Row& row = me.row;
  card.body = CardBody::Ranked;
  card.tag = api_keys::Trimmed(SanitizeExternalDisplayText(row.tag));
  const std::string name = api_keys::Trimmed(SanitizeExternalDisplayText(row.name));
  card.named = !name.empty();
  card.name = card.named ? name : words.lookup(kAnonymousText);
  card.publicName = card.named;
  card.position = row.position > 0 ? PositionText(row.position) : std::string("-");
  card.projected = words.lookup(kProjectedText);
  const std::optional<Snapshot>& shown = snapshot ? snapshot : state.snapshot;
  if (shown && shown->totalRanked > 0) {
    card.ofRanked = words.format(words.lookup(kOfRankedText), {Grouped(shown->totalRanked)});
  }
  card.aboveLine = row.margin >= 0;
  card.margin = MarginText(row.margin, words);
  card.trend = TrendOf(row.trend);
  card.trendText = TrendText(row.trend, words);
  card.label = LabelOf(row);
  card.labelText = words.lookup(LabelText(card.label));
  card.chart = ChartFor(me.history, row.position, words);
  card.why = words.format(words.lookup(kWhyText),
                          {Grouped(me.distinctPrefixes), Grouped(me.sharedPrefixes)});
  card.solo = words.format(words.lookup(kSoloText), {ScoreText(me.soloScore)});
  card.notMeasurable = NotMeasurableText(me, words);
  card.claim = ClaimPromptFor(row, shown, words);
  return card;
}

// ---- the claimed row's detail sheet ---------------------------------------------------------

struct Fact {
  std::string label;
  std::string value;
};

struct FleetDetail {
  std::string title;  // "UID 81"
  bool hasSelection = false;
  bool selected = false;
  std::string selectionText;  // "Selected" / "Not selected": the chip
  std::vector<Fact> facts;
};

struct Detail {
  std::string tag;
  std::string title;     // the claim: "UID 81", or "UID 81, UID 92"
  std::string subtitle;  // the network: "kayak-hollow · claimed", "Anonymous · claimed"
  std::vector<Fact> facts;  // the network's: its projected rank
  std::vector<FleetDetail> fleets;
  std::string note;  // "On-chain incentive lags the validator's ranking." with an incentive
  std::string closeText;
};

inline Detail DetailFor(const Row& row, const Words& words) {
  Detail detail;
  detail.tag = api_keys::Trimmed(SanitizeExternalDisplayText(row.tag));
  // the sheet is about the claim: its UIDs over the network that holds it
  for (const Fleet& fleet : row.fleets) {
    if (!detail.title.empty()) detail.title += ", ";
    detail.title += UidText(fleet.uid, words);
  }
  const std::string name = api_keys::Trimmed(SanitizeExternalDisplayText(row.name));
  detail.subtitle =
      (name.empty() ? words.lookup(kAnonymousText) : name) + " · " + words.lookup(kDetailClaimedText);
  detail.facts.push_back(
      Fact{words.lookup(kDetailProjectedRankText), PositionText(row.position)});
  bool anyIncentive = false;
  for (const Fleet& fleet : row.fleets) {
    FleetDetail out;
    out.title = UidText(fleet.uid, words);
    if (fleet.selected) {
      out.hasSelection = true;
      out.selected = *fleet.selected;
      out.selectionText =
          words.lookup(*fleet.selected ? kDetailSelectedText : kDetailNotSelectedText);
    }
    if (fleet.validatorRank) {
      const std::string rank = Grouped(*fleet.validatorRank);
      out.facts.push_back(Fact{
          words.lookup(kDetailValidatorRankText),
          fleet.validatorRankOf
              ? words.format(words.lookup(kDetailValidatorRankValueText),
                             {rank, Grouped(*fleet.validatorRankOf)})
              : PositionText(*fleet.validatorRank)});
    }
    if (fleet.validatorScore) {
      out.facts.push_back(
          Fact{words.lookup(kDetailValidatorScoreText), ScoreText(*fleet.validatorScore)});
    }
    out.facts.push_back(Fact{words.lookup(kDetailReplicaScoreText), ScoreText(fleet.replicaScore)});
    if (fleet.incentive) {
      anyIncentive = true;
      std::string value = IncentiveText(*fleet.incentive);
      if (fleet.incentiveStale) value += " · " + words.lookup(kDetailStaleText);
      out.facts.push_back(Fact{words.lookup(kDetailIncentiveText), value});
    }
    out.facts.push_back(
        Fact{words.lookup(kDetailGenerationText), std::to_string(fleet.generation)});
    if (fleet.validToEpoch > 0) {
      out.facts.push_back(Fact{
          words.lookup(kDetailValidUntilText),
          words.format(words.lookup(kDetailEpochText), {std::to_string(fleet.validToEpoch)})});
    }
    detail.fleets.push_back(std::move(out));
  }
  if (anyIncentive) detail.note = words.lookup(kDetailIncentiveNoteText);
  detail.closeText = words.lookup(kCloseText);
  return detail;
}

// ---- the Earnings page's Top 200 tile ----------------------------------------------------------

enum class TileKind {
  Hidden,     // signed out, not loaded, failed, or neither projected inside nor claimed
  Unclaimed,  // projected inside the top 200: the claim prompt on Earnings
  Claimed,    // a fleet is bound: its UID, validator rank and selection
};

struct TileFleet {
  std::string status;     // "UID 81 · rank #1", or "UID 81" before the validator ranks it
  std::string selection;  // "Selected" / "Not selected"; ""
  bool selected = false;  // the validator selected it this tempo
};

struct Tile {
  TileKind kind = TileKind::Hidden;
  std::string title;  // "Top 200"
  Label label = Label::Estimate;
  std::string labelText;
  std::string position;       // "#173"
  std::string projectedLine;  // "projected · +12%"
  std::string body;           // how the board ranks
  bool bindingOpen = false;
  // binding closed: the note in place of the claim button
  std::string soonTitle;  // "Claiming opens soon"
  std::string soonBody;
  // binding open: the existing claim flow
  bool showClaim = false;  // "Claim your spot", the ur.io Top 200 page
  std::string claimText;
  bool showSign = false;  // "Sign fleet binding"
  std::string signText;
  std::string borderline;  // within 20% of the line, with the claim button
  std::string warning;     // with the claim button
  // claimed
  std::vector<TileFleet> fleets;
  std::string boundDetail;
  std::string demotion;  // a claimed network within 20% of the line, or under it
  std::string seeBoard;  // "See the Protocol leaderboard"
};

// The tile reads GET /sn/top/me, so it shows the same projected position,
// margin and label as the Protocol tab. It shows for a network projected
// inside the top 200 (the claim prompt) and for a claimed one; while binding
// is closed "Claim your spot" becomes the note "Claiming opens soon" and "Sign
// fleet binding" stays hidden; once binding opens the claim flow is the one
// the tile always had.
inline Tile TileFor(const MeState& state, bool signedIn, const Words& words) {
  Tile tile;
  if (!signedIn || !state.loaded || !state.me) return tile;
  const Row& row = state.me->row;
  const bool claimed = !row.fleets.empty();
  const ClaimPrompt prompt = ClaimPromptFor(row, state.snapshot, words);
  if (!claimed && !prompt.show) return tile;
  tile.kind = claimed ? TileKind::Claimed : TileKind::Unclaimed;
  tile.title = words.lookup(kTop200Text);
  tile.label = LabelOf(row);
  tile.labelText = words.lookup(LabelText(tile.label));
  tile.position = row.position > 0 ? PositionText(row.position) : std::string("-");
  tile.projectedLine =
      words.format(words.lookup(kTileProjectedText), {SignedPercentText(row.margin)});
  tile.body = words.lookup(kTileBodyText);
  tile.bindingOpen = state.snapshot && state.snapshot->bindingOpen;
  tile.seeBoard = words.lookup(kTileSeeBoardText);
  if (tile.bindingOpen) {
    tile.showSign = true;
    tile.signText = words.lookup(kSignBindingText);
  }
  if (claimed) {
    for (const Fleet& fleet : row.fleets) {
      TileFleet out;
      out.status = fleet.validatorRank
                       ? words.format(words.lookup(kBoundStatusText),
                                      {std::to_string(fleet.uid), Grouped(*fleet.validatorRank)})
                       : UidText(fleet.uid, words);
      if (fleet.selected) {
        out.selected = *fleet.selected;
        out.selection =
            words.lookup(*fleet.selected ? kDetailSelectedText : kDetailNotSelectedText);
      }
      tile.fleets.push_back(std::move(out));
    }
    tile.boundDetail = words.lookup(kBoundDetailText);
    if (std::isfinite(row.margin) && row.margin < kBorderlineMargin) {
      tile.demotion = words.lookup(kDemotionText);
    }
    return tile;
  }
  if (tile.bindingOpen) {
    tile.showClaim = true;
    tile.claimText = words.lookup(kClaimSpotText);
    tile.borderline = prompt.borderline;
    tile.warning = prompt.warning;
  } else {
    tile.soonTitle = words.lookup(kTileClaimingSoonText);
    tile.soonBody = words.lookup(kTileClaimingSoonBodyText);
  }
  return tile;
}

}  // namespace urnw::protocol_board
