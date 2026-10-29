// SPDX-License-Identifier: MPL-2.0
#include "ProtocolBoard.hpp"

#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <utility>

#include "ApiKeysPresentation.hpp"  // the RFC 3339 reader
#include "DisplayText.hpp"
#include "Formatters.hpp"
#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"
#include "UrTheme.hpp"

namespace urnw {

namespace pb = protocol_board;

namespace {

constexpr int kRowHeight = 44;      // a ranked row (the mockup's 44)
constexpr int kLineHeight = 34;     // the Top 200 line between 200 and 201
constexpr int kPositionWidth = 60;  // "#1,240" in the row's key face
constexpr int kScoreWidth = 64;     // "1,312.4"
constexpr int kArrowWidth = 12;     // the trend arrow beside the score
constexpr int kChartHeight = 96;
constexpr int kPlaceholderRows = 10;  // the rows a first load stands in for
constexpr int kSheetWidth = 400;
constexpr int kPositionPx = 40;  // the card's "#173"
constexpr int kTagPx = 22;       // the emoji tag beside the card's name
constexpr int kMoreWithinRows = 6;  // the next page is asked this many rows from the end
constexpr int kSeekFrames = 90;     // a seek's scroll waits at most this many frames (~1.5 s)
constexpr int64_t kUnlaidVisibleRows = 16;  // a screenful before the pane has a size
// the chart's plot inside its box: room for the line's label above the 200
// line and for the latest day's dot at the right edge
constexpr double kChartPadX = 4.0;
constexpr double kChartPadY = 6.0;
constexpr double kChartLabelPx = 10.0;

// The accent the board marks the line, the tint, the trend up and the latest
// day with (the primary button's blue, Ui.hpp's palette has no name for it).
constexpr Rgba kLineBlue{0x63 / 255.0, 0x8B / 255.0, 0xFC / 255.0, 1.0};  // #638BFC

void SetSource(const Cairo::RefPtr<Cairo::Context>& cr, const Rgba& c, double alpha = 1.0) {
  cr->set_source_rgba(c.r, c.g, c.b, c.a * alpha);
}

void SetClass(Gtk::Widget& widget, const char* cssClass, bool on) {
  if (on) {
    widget.add_css_class(cssClass);
  } else {
    widget.remove_css_class(cssClass);
  }
}

Gtk::Label* MakeLabel(const Glib::ustring& text, const char* cssClass, float xalign = 0.f) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  if (cssClass != nullptr) label->add_css_class(cssClass);
  label->set_xalign(xalign);
  label->set_valign(Gtk::Align::CENTER);
  return label;
}

// A line that wraps rather than cut a sentence.
Gtk::Label* MakeWrapped(const Glib::ustring& text, const char* cssClass) {
  auto* label = MakeLabel(text, cssClass);
  label->set_wrap(true);
  label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  return label;
}

// A label at an explicit pixel size (a pango attribute: the class keeps the face).
void SetPixelSize(Gtk::Label& label, int sizePx) {
  Pango::AttrList attrs;
  auto size = Pango::Attribute::create_attr_size_absolute(sizePx * PANGO_SCALE);
  attrs.insert(size);
  label.set_attributes(attrs);
}

// A pane row whose height is its content (padding 12,padY), on the pane's
// row rhythm with its hairline.
struct PaddedRow {
  Gtk::Box* root = nullptr;
  Gtk::Box* content = nullptr;
};

PaddedRow MakePadded(int padY, int spacing = 6) {
  PaddedRow out;
  out.root = kit::MakePaneRow(0);
  out.content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, spacing);
  out.content->set_hexpand(true);
  out.content->set_margin_top(padY);
  out.content->set_margin_bottom(padY);
  if (auto* inner = dynamic_cast<Gtk::Box*>(out.root->get_first_child())) {
    inner->append(*out.content);
  }
  return out;
}

// The label chip's tone: lime for likely, amber for borderline, muted for the
// two that say no; an estimate is the neutral outline.
const char* LabelChipClass(pb::Label label) {
  switch (label) {
    case pb::Label::Likely:
      return "ur-protocol-chip-likely";
    case pb::Label::Borderline:
      return "ur-protocol-chip-borderline";
    case pb::Label::Unlikely:
    case pb::Label::NotMeasurable:
      return "ur-protocol-chip-muted";
    case pb::Label::Estimate:
      return "ur-protocol-chip-estimate";
  }
  return "ur-protocol-chip-estimate";
}

void SetLabelChip(Gtk::Label& chip, pb::Label label, const std::string& text) {
  chip.set_text(text);
  for (const char* tone : {"ur-protocol-chip-likely", "ur-protocol-chip-borderline",
                           "ur-protocol-chip-muted", "ur-protocol-chip-estimate"}) {
    chip.remove_css_class(tone);
  }
  chip.add_css_class(LabelChipClass(label));
}

Gtk::Label* MakeChip(const Glib::ustring& text, const char* toneClass) {
  auto* chip = MakeLabel(text, "ur-protocol-chip");
  chip->add_css_class(toneClass);
  chip->set_halign(Gtk::Align::START);
  return chip;
}

// The small mark on a row the validator cannot measure: a ring with a slash,
// in the muted tone. The row's accessible name says "Not measurable".
Gtk::Widget* MakeNotMeasurableMark() {
  auto* mark = Gtk::make_managed<Gtk::DrawingArea>();
  mark->set_content_width(14);
  mark->set_content_height(14);
  mark->set_valign(Gtk::Align::CENTER);
  mark->set_draw_func([](const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    const double r = std::min(width, height) / 2.0 - 1.5;
    const double cx = width / 2.0;
    const double cy = height / 2.0;
    SetSource(cr, kUrTextMuted);
    cr->set_line_width(1.5);
    cr->arc(cx, cy, r, 0, 2 * M_PI);
    cr->stroke();
    cr->move_to(cx - r * 0.7, cy + r * 0.7);
    cr->line_to(cx + r * 0.7, cy - r * 0.7);
    cr->stroke();
  });
  kit::MarkDecorative(*mark);
  return mark;
}

// ---- the sdk's answers, as plain data ------------------------------------------------
// A result that arrived with neither a transport error nor the server's error,
// and with its snapshot (every answer the server gives carries one; a nil Go
// result arrives as an engaged optional with every field unset), is the
// answer; anything else failed.

pb::Snapshot SnapshotOf(const urnet::SnTopSnapshot& s) {
  pb::Snapshot out;
  out.id = s.id;
  out.tempoIndex = s.tempo_index;
  // the C ABI hands the server's time over as its RFC 3339 text, null when absent
  out.computedAtUnixSeconds =
      api_keys::ParseRfc3339UnixSeconds(s.computed_at.value_or(std::string())).value_or(0);
  out.settlementEpoch = s.settlement_epoch;
  out.totalRanked = s.total_ranked;
  out.headSeats = s.head_seats > 0 ? s.head_seats : pb::kHeadSeats;
  out.claimedFleets = s.claimed_fleets;
  out.openSeats = s.open_seats;
  out.projectedCutoff = s.projected_cutoff;
  if (s.has_claimed_cutoff) out.claimedCutoff = s.claimed_cutoff;
  out.bindingOpen = s.binding_open;
  out.bindingNote = s.binding_note;
  if (s.has_precision_at_200) out.precisionAt200 = s.precision_at_200;
  out.stale = s.stale;
  return out;
}

pb::Fleet FleetOf(const urnet::SnTopFleet& f) {
  pb::Fleet out;
  out.uid = f.uid;
  out.generation = f.generation;
  out.validToEpoch = f.valid_to_epoch;
  out.replicaScore = f.replica_score;
  if (f.has_validator_score) out.validatorScore = f.validator_score;
  if (f.has_validator_rank) out.validatorRank = f.validator_rank;
  if (f.has_validator_rank_of) out.validatorRankOf = f.validator_rank_of;
  if (f.has_selected) out.selected = f.selected;
  if (f.has_incentive) out.incentive = f.incentive;
  out.incentiveStale = f.incentive_stale;
  return out;
}

pb::Row RowOf(const urnet::SnTopRow& r) {
  pb::Row out;
  out.position = r.position;
  out.name = r.name;
  out.tag = r.tag;
  out.projectedScore = r.projected_score;
  out.margin = r.margin;
  out.trend = r.trend;
  out.prefixes = r.prefixes;
  out.sharedPrefixes = r.shared_prefixes;
  out.label = r.label;
  out.measurable = r.measurable;
  out.claimPrompt = r.claim_prompt;
  out.isMe = r.is_me;
  if (r.fleets) {
    for (const urnet::SnTopFleet& fleet : *r.fleets) out.fleets.push_back(FleetOf(fleet));
  }
  return out;
}

pb::Me MeOf(const urnet::SnTopMe& m) {
  pb::Me out;
  if (m.row) out.row = RowOf(*m.row);
  out.soloScore = m.solo_score;
  out.distinctPrefixes = m.distinct_prefixes;
  out.sharedPrefixes = m.shared_prefixes;
  out.sharedWithUnits = m.shared_with_units;
  out.notMeasurableReason = m.not_measurable_reason;
  if (m.history) {
    for (const urnet::SnTopHistoryPoint& point : *m.history) {
      out.history.push_back(pb::HistoryPoint{point.day, point.rank, point.score});
    }
  }
  return out;
}

pb::TopAnswer TopAnswerOf(const std::optional<urnet::SnTopResult>& result,
                          const std::optional<std::string>& err) {
  pb::TopAnswer answer;
  answer.ok = result.has_value() && !err.has_value() && !result->error.has_value() &&
              result->snapshot.has_value();
  if (!answer.ok) {
    g_warning("protocol board: GET /sn/top failed: %s",
              err ? err->c_str()
                  : result && result->error ? result->error->message.c_str() : "no snapshot");
    return answer;
  }
  answer.snapshot = SnapshotOf(*result->snapshot);
  if (result->rows) {
    for (const urnet::SnTopRow& row : *result->rows) answer.rows.push_back(RowOf(row));
  }
  if (result->me) answer.me = RowOf(*result->me);
  answer.next = result->next;
  return answer;
}

pb::MeAnswer MeAnswerOf(const std::optional<urnet::SnTopMeResult>& result,
                        const std::optional<std::string>& err) {
  pb::MeAnswer answer;
  answer.ok = result.has_value() && !err.has_value() && !result->error.has_value() &&
              result->snapshot.has_value();
  if (!answer.ok) {
    g_warning("protocol board: GET /sn/top/me failed: %s",
              err ? err->c_str()
                  : result && result->error ? result->error->message.c_str() : "no snapshot");
    return answer;
  }
  answer.snapshot = SnapshotOf(*result->snapshot);
  // no row: the network is not on the board
  if (result->me && result->me->row) answer.me = MeOf(*result->me);
  return answer;
}

// The account's Api behind pb::Service. Each answer arrives on an sdk thread,
// is copied into plain data there and posted to the main loop, where it is
// handed over only while the board is alive; the binding drops one its
// request no longer waits for.
class SdkService final : public pb::Service {
 public:
  SdkService(SdkHost& host, std::shared_ptr<bool> alive) : host_(host), alive_(std::move(alive)) {}

  void Top(const pb::TopArgs& args, std::function<void(pb::TopAnswer)> done) override {
    urnet::SnTopArgs request;
    request.limit = args.limit;
    if (!args.cursor.empty()) request.cursor = args.cursor;
    if (!args.filter.empty()) request.filter = args.filter;
    if (!args.snapshot.empty()) request.snapshot = args.snapshot;
    try {
      host_.api().snTop(
          std::optional<urnet::SnTopArgs>(request),
          [alive = alive_, done](std::optional<urnet::SnTopResult> result,
                                 std::optional<std::string> err) {
            pb::TopAnswer answer = TopAnswerOf(result, err);
            PostToMain([alive, done, answer = std::move(answer)]() mutable {
              if (*alive) done(std::move(answer));
            });
          });
    } catch (const std::exception& e) {
      g_warning("protocol board: GET /sn/top was not sent: %s", e.what());
      PostToMain([alive = alive_, done] {
        if (*alive) done(pb::TopAnswer{});
      });
    }
  }

  void TopMe(std::function<void(pb::MeAnswer)> done) override {
    try {
      host_.api().snTopMe([alive = alive_, done](std::optional<urnet::SnTopMeResult> result,
                                                 std::optional<std::string> err) {
        pb::MeAnswer answer = MeAnswerOf(result, err);
        PostToMain([alive, done, answer = std::move(answer)]() mutable {
          if (*alive) done(std::move(answer));
        });
      });
    } catch (const std::exception& e) {
      g_warning("protocol board: GET /sn/top/me was not sent: %s", e.what());
      PostToMain([alive = alive_, done] {
        if (*alive) done(pb::MeAnswer{});
      });
    }
  }

 private:
  SdkHost& host_;
  std::shared_ptr<bool> alive_;
};

// ---- the preview's board ------------------------------------------------------------
// URNETWORK_PREVIEW_SAMPLE: an obviously synthetic board ("sample-net-…"),
// answered at once through the same binding, so the states, the paging and the
// line can be looked at without an account or a server.

class SampleService final : public pb::Service {
 public:
  explicit SampleService(std::string state) : state_(std::move(state)) {}

  void Top(const pb::TopArgs& args, std::function<void(pb::TopAnswer)> done) override {
    pb::TopAnswer answer;
    if (state_ == "failed") {
      done(answer);
      return;
    }
    answer.ok = true;
    answer.snapshot = Snapshot();
    const int64_t total = state_ == "empty" ? 0 : pb::kListPositions;
    int64_t from = 1;
    if (args.cursor.rfind("p", 0) == 0) from = std::atoll(args.cursor.c_str() + 1) + 1;
    const int64_t to = std::min<int64_t>(total, from + std::max(1, args.limit) - 1);
    for (int64_t position = from; position <= to; ++position) {
      answer.rows.push_back(RowAt(position));
    }
    if (to < total) answer.next = "p" + std::to_string(to);
    answer.me = MeRow();
    done(answer);
  }

  void TopMe(std::function<void(pb::MeAnswer)> done) override {
    pb::MeAnswer answer;
    if (state_ == "failed") {
      done(answer);
      return;
    }
    answer.ok = true;
    answer.snapshot = Snapshot();
    pb::Me me;
    me.row = MeRow();
    me.soloScore = me.row.projectedScore * 1.18;
    me.distinctPrefixes = 18;
    me.sharedPrefixes = 3;
    me.sharedWithUnits = 2;
    if (!me.row.measurable) me.notMeasurableReason = "shared_egress";
    // the last 90 days, climbing toward the row's position
    const int64_t last = pb::DayNumber("2026-10-29").value_or(0);
    const double start = static_cast<double>(std::max<int64_t>(me.row.position * 2 + 60, 412));
    for (int64_t day = 0; day < pb::kHistoryDays; ++day) {
      const double t = static_cast<double>(day) / (pb::kHistoryDays - 1);
      const double rank = start * std::pow(static_cast<double>(me.row.position) / start, t) +
                          6.0 * std::sin(static_cast<double>(day) / 3.0) * (1.0 - t);
      me.history.push_back(pb::HistoryPoint{DayText(last - (pb::kHistoryDays - 1) + day),
                                            std::max<int64_t>(1, std::llround(rank)), 0.0});
    }
    me.history.back().rank = me.row.position;
    answer.me = me;
    done(answer);
  }

 private:
  static std::string DayText(int64_t dayNumber) {
    GDateTime* time = g_date_time_new_from_unix_utc(dayNumber * 86400);
    if (time == nullptr) return std::string();
    gchar* text = g_date_time_format(time, "%Y-%m-%d");
    std::string out = text != nullptr ? text : "";
    g_free(text);
    g_date_time_unref(time);
    return out;
  }

  bool BindingOpen() const { return state_ == "open" || state_ == "claimed"; }

  pb::Snapshot Snapshot() const {
    pb::Snapshot snapshot;
    snapshot.id = "sample";
    snapshot.tempoIndex = 4821;
    snapshot.totalRanked = state_ == "empty" ? 0 : 41230;
    snapshot.projectedCutoff = 41.2;
    snapshot.bindingOpen = BindingOpen();
    snapshot.stale = state_ == "stale";
    snapshot.computedAtUnixSeconds = g_get_real_time() / G_USEC_PER_SEC - (snapshot.stale ? 5 * 3600 : 1800);
    return snapshot;
  }

  int64_t MePosition() const {
    if (state_ == "open") return 196;
    if (state_ == "unmeasurable") return 4210;
    if (state_ == "claimed") return 10;
    return 173;
  }

  static std::string Label(const pb::Row& row, bool labelsOn) {
    if (!row.measurable) return "not_measurable";
    if (!labelsOn) return "estimate";
    if (row.margin >= pb::kBorderlineMargin) return "likely";
    if (row.margin > -pb::kBorderlineMargin) return "borderline";
    return "unlikely";
  }

  pb::Row RowAt(int64_t position) const {
    static const char* const kTags[] = {"\xF0\x9F\x90\xB8", "", "\xF0\x9F\xA6\x8A\xF0\x9F\x8C\xB5",
                                        "\xF0\x9F\x8D\x8B", "", "\xF0\x9F\x9A\x80",
                                        "\xF0\x9F\xA7\x8A"};
    pb::Row row;
    row.position = position;
    row.name = position % 4 == 3 ? std::string() : "sample-net-" + std::to_string(position);
    row.tag = kTags[position % 7];
    row.projectedScore = 41.2 * std::pow(200.0 / static_cast<double>(position), 0.38);
    row.margin = row.projectedScore / 41.2 - 1.0;
    row.trend = (position * 7) % 9 - 4;
    row.prefixes = std::max<int64_t>(2, 60 - position / 8);
    row.sharedPrefixes = position % 5;
    row.measurable = position % 7 != 4;
    row.label = Label(row, BindingOpen());
    row.claimPrompt = row.measurable && position <= pb::kHeadSeats;
    if (position % 9 == 1) {
      pb::Fleet fleet;
      fleet.uid = 80 + position % 170;
      fleet.generation = 2;
      fleet.validToEpoch = 16;
      fleet.replicaScore = row.projectedScore;
      fleet.validatorScore = row.projectedScore;
      fleet.validatorRank = 1 + position / 9;
      fleet.validatorRankOf = 142;
      fleet.selected = position <= pb::kHeadSeats;
      fleet.incentive = 0.018 / static_cast<double>(1 + position / 9);
      fleet.incentiveStale = position == 1;
      row.fleets.push_back(fleet);
    }
    row.isMe = position == MePosition();
    if (row.isMe && state_ == "open") row.name.clear();  // the switch off: Anonymous
    if (row.isMe && state_ != "unmeasurable") row.measurable = true;
    if (row.isMe) row.label = Label(row, BindingOpen() && state_ != "claimed");
    if (row.isMe && state_ == "open") row.label = "borderline";
    if (row.isMe) row.claimPrompt = row.measurable && position <= pb::kHeadSeats;
    return row;
  }

  pb::Row MeRow() const {
    pb::Row row = RowAt(MePosition());
    if (state_ == "unmeasurable") {
      row.name.clear();
      row.measurable = false;
      row.label = "not_measurable";
      row.claimPrompt = false;
      row.margin = -0.41;
      row.trend = 0;
    }
    return row;
  }

  std::string state_;
};

}  // namespace

// ---- the words ---------------------------------------------------------------------

Gtk::Label* MakeProtocolLabelChip(pb::Label label, const std::string& text) {
  auto* chip = MakeLabel(text, "ur-protocol-chip");
  SetLabelChip(*chip, label, text);
  return chip;
}

std::string ProtocolBoardLookup(const pb::Text& text) {
  const std::string key = text.key;
  // The catalog scan (tests/CatalogLookupTest.cpp) reads a {key, English}
  // pair only when its English reads as UI text; these three single
  // lowercase words are looked up by name, where it sees them.
  if (key == "leaderboard_protocol_projected") {
    return T_("leaderboard_protocol_projected", "projected");
  }
  if (key == "leaderboard_protocol_detail_claimed") {
    return T_("leaderboard_protocol_detail_claimed", "claimed");
  }
  if (key == "leaderboard_protocol_detail_stale") {
    return T_("leaderboard_protocol_detail_stale", "stale");
  }
  return T_(text.key, text.english);
}

pb::Words ProtocolBoardWords() {
  pb::Words words;
  words.lookup = &ProtocolBoardLookup;
  words.format = [](const std::string& pattern, const std::vector<std::string>& args) {
    return FormatArgs(pattern.c_str(), args);
  };
  words.date = [](int64_t unixSeconds) { return LocalDate(unixSeconds); };
  words.dateTime = [](int64_t unixSeconds) { return LocalDateTime(unixSeconds); };
  return words;
}

// ---- the progress chart --------------------------------------------------------------

// The caller's daily projected rank over the last 90 days, with the Top 200
// line across; up is better. A press (or Left/Right with the keyboard) names
// the nearest day's rank under the chart. The geometry is ChartFor's.
class ProtocolRankChart : public Gtk::DrawingArea {
 public:
  ProtocolRankChart() {
    set_content_height(kChartHeight);
    set_hexpand(true);
    set_focusable(true);
    set_draw_func(sigc::mem_fun(*this, &ProtocolRankChart::Draw));
    auto click = Gtk::GestureClick::create();
    click->signal_pressed().connect([this](int, double x, double) { Press(x); });
    add_controller(click);
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) { return Key(keyval); }, false);
    add_controller(keys);
  }

  void SetChart(const pb::Chart& chart) {
    chart_ = chart;
    selected_.reset();
    // an image named by its summary ("Projected rank over 90 days, now 173")
    kit::SetAccessibleLabel(*this, chart_.accessibleName);
    queue_draw();
  }

  std::function<void(std::optional<size_t> index)> on_point;

 private:
  double Left() const { return kChartPadX; }
  double Right() const { return std::max(Left(), get_width() - kChartPadX - 4.0); }
  double Top() const { return kChartPadY + kChartLabelPx; }
  double Bottom() const { return std::max(Top(), get_height() - kChartPadY); }

  void Press(double x) {
    if (chart_.points.empty() || Right() <= Left()) return;
    grab_focus();
    Select(pb::NearestPoint(chart_, (x - Left()) / (Right() - Left())));
  }

  bool Key(guint keyval) {
    if (chart_.points.empty()) return false;
    const size_t last = chart_.points.size() - 1;
    size_t at = selected_.value_or(last);
    switch (keyval) {
      case GDK_KEY_Left:
        at = selected_ ? (at > 0 ? at - 1 : 0) : last;
        break;
      case GDK_KEY_Right:
        at = selected_ ? std::min(at + 1, last) : last;
        break;
      case GDK_KEY_Home:
        at = 0;
        break;
      case GDK_KEY_End:
        at = last;
        break;
      case GDK_KEY_Escape:
        if (!selected_) return false;
        Select(std::nullopt);
        return true;
      default:
        return false;
    }
    Select(at);
    return true;
  }

  void Select(std::optional<size_t> index) {
    selected_ = index;
    queue_draw();
    if (on_point) on_point(index);
  }

  void Draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    (void)width;
    if (!chart_.show) return;
    const double left = Left();
    const double right = Right();
    const double top = Top();
    const double bottom = Bottom();
    if (right <= left || bottom <= top) return;
    const auto xOf = [&](double x) { return left + x * (right - left); };
    const auto yOf = [&](double y) { return top + y * (bottom - top); };

    // the Top 200 line: dashed, in the accent, named at its right end
    const double lineY = std::round(yOf(chart_.lineY)) + 0.5;
    cr->save();
    SetSource(cr, kLineBlue, 0.8);
    cr->set_line_width(1.0);
    cr->set_dash(std::vector<double>{4.0, 4.0}, 0.0);
    cr->move_to(left, lineY);
    cr->line_to(right, lineY);
    cr->stroke();
    cr->restore();
    auto label = create_pango_layout(chart_.lineLabel);
    Pango::FontDescription font;
    font.set_family("PP Neue Montreal");
    font.set_absolute_size(kChartLabelPx * PANGO_SCALE);
    label->set_font_description(font);
    int labelWidth = 0;
    int labelHeight = 0;
    label->get_pixel_size(labelWidth, labelHeight);
    // at the line's left end, where the oldest days are: above the line when
    // they ranked under it (the common climb), below it when they ranked above
    bool ranksAboveThere = false;
    for (const pb::ChartPoint& point : chart_.points) {
      if (xOf(point.x) > left + labelWidth) break;
      if (point.y < chart_.lineY) ranksAboveThere = true;
    }
    double labelY = ranksAboveThere ? lineY + 2.0 : lineY - labelHeight - 1.0;
    if (labelY < 0) labelY = lineY + 2.0;
    if (labelY + labelHeight > height) labelY = lineY - labelHeight - 1.0;
    SetSource(cr, kUrTextMuted);
    cr->move_to(left, labelY);
    label->show_in_cairo_context(cr);

    // the rank, day by day
    SetSource(cr, kUrText);
    cr->set_line_width(2.0);
    cr->set_line_join(Cairo::Context::LineJoin::ROUND);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);
    for (size_t i = 0; i < chart_.points.size(); ++i) {
      const double x = xOf(chart_.points[i].x);
      const double y = yOf(chart_.points[i].y);
      if (i == 0) {
        cr->move_to(x, y);
      } else {
        cr->line_to(x, y);
      }
    }
    if (chart_.points.size() > 1) cr->stroke();
    cr->begin_new_path();

    // the latest day, in the accent
    const pb::ChartPoint& latest = chart_.points.back();
    SetSource(cr, kLineBlue);
    cr->arc(xOf(latest.x), yOf(latest.y), 3.5, 0, 2 * M_PI);
    cr->fill();

    // the pressed day: a faint rule down to it and a ring
    if (selected_ && *selected_ < chart_.points.size()) {
      const pb::ChartPoint& point = chart_.points[*selected_];
      const double x = std::round(xOf(point.x)) + 0.5;
      SetSource(cr, kUrBorderBase);
      cr->set_line_width(1.0);
      cr->move_to(x, top);
      cr->line_to(x, bottom);
      cr->stroke();
      SetSource(cr, kUrText);
      cr->set_line_width(1.5);
      cr->arc(xOf(point.x), yOf(point.y), 4.5, 0, 2 * M_PI);
      cr->stroke();
    }
  }

  pb::Chart chart_;
  std::optional<size_t> selected_;
};

// ---- the claimed row's detail sheet ---------------------------------------------------------

namespace {

// The sheet a claimed row opens: the network and its projected rank, then
// each fleet's UID with the validator's selection, its facts, and the note
// that on-chain incentive lags the validator's ranking. Close (or Escape)
// closes it; it has no other action, so no primary button.
std::shared_ptr<Gtk::Window> MakeDetailSheet(const pb::Detail& detail) {
  auto sheet = std::make_shared<Gtk::Window>();
  sheet->set_title(detail.title);
  sheet->set_default_size(kSheetWidth, -1);
  sheet->set_resizable(false);

  auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  column->set_margin_bottom(4);

  // the network: its tag, what it is called, and that it claimed
  auto* head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  head->set_margin(20);
  head->set_margin_bottom(12);
  if (!detail.tag.empty()) {
    auto* tag = MakeLabel(detail.tag, nullptr);
    SetPixelSize(*tag, 26);
    kit::MarkDecorative(*tag);
    head->append(*tag);
  }
  auto* titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
  titles->set_hexpand(true);
  auto* title = MakeWrapped(detail.title, "ur-row-title");
  SetPixelSize(*title, 20);
  titles->append(*title);
  titles->append(*MakeWrapped(detail.subtitle, "ur-caption"));
  head->append(*titles);
  column->append(*head);

  for (const pb::Fact& fact : detail.facts) {
    column->append(*kit::MakePaneKeyValueRow(fact.label, fact.value).root);
  }
  for (const pb::FleetDetail& fleet : detail.fleets) {
    auto group = kit::MakePaneGroupHeader(fleet.title);
    if (fleet.hasSelection && group.trailing != nullptr) {
      group.trailing->append(*MakeChip(fleet.selectionText, fleet.selected
                                                               ? "ur-protocol-chip-likely"
                                                               : "ur-protocol-chip-muted"));
      group.trailing->set_visible(true);
    }
    column->append(*group.root);
    for (const pb::Fact& fact : fleet.facts) {
      column->append(*kit::MakePaneKeyValueRow(fact.label, fact.value).root);
    }
  }
  if (!detail.note.empty()) {
    auto row = MakePadded(10);
    row.content->append(*MakeWrapped(detail.note, "ur-row-note"));
    column->append(*row.root);
  }

  auto* close = Gtk::make_managed<Gtk::Button>(detail.closeText);
  close->add_css_class("ur-pane-secondary");
  close->signal_clicked().connect([raw = sheet.get()] { raw->set_visible(false); });
  column->append(*close);

  sheet->set_child(*column);
  sheet->set_default_widget(*close);
  return sheet;
}

}  // namespace

// ---- the side block ---------------------------------------------------------------

ProtocolSide::ProtocolSide(std::shared_ptr<bool> alive, bool withHeader)
    : alive_(std::move(alive)) {
  Build(withHeader);
}

void ProtocolSide::Build(bool withHeader) {
  root_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  if (withHeader) root_->append(*kit::MakePaneGroupHeader(ProtocolBoardLookup(pb::kTabText)).root);

  // the status banner: neutral, never the error style
  {
    auto row = MakePadded(10);
    auto* banner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    banner->add_css_class("ur-protocol-banner");
    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name("help-about-symbolic");
    icon->add_css_class("ur-protocol-banner-icon");
    icon->set_valign(Gtk::Align::START);
    kit::MarkDecorative(*icon);
    banner->append(*icon);
    bannerText_ = MakeWrapped({}, "ur-protocol-banner-text");
    bannerText_->set_hexpand(true);
    banner->append(*bannerText_);
    row.content->append(*banner);
    bannerRow_ = row.root;
    root_->append(*row.root);
  }

  // placeholders of the card's own sizes while its first answer is out
  {
    auto row = MakePadded(12, 10);
    row.content->append(*kit::MakeSkeletonLabel("sample-network-name", "ur-row-title"));
    auto* position = kit::MakeSkeletonLabel("#000", "ur-stat-value");
    SetPixelSize(*position, kPositionPx);
    row.content->append(*position);
    row.content->append(*kit::MakeSkeletonLabel("Estimate  12% above the Top 200 line", "ur-key"));
    auto* chart = Gtk::make_managed<Gtk::Box>();
    chart->set_size_request(-1, kChartHeight);
    kit::SetSkeleton(*chart, true);
    row.content->append(*chart);
    row.content->append(
        *kit::MakeSkeletonLabel("Distinct IP prefixes: 18. Shared with other networks: 3.", "ur-row-note"));
    placeholderRow_ = row.root;
    root_->append(*row.root);
  }

  // one line in the card's place: signed out, failed (with its retry), not ranked
  {
    auto row = MakePadded(12, 8);
    lineText_ = MakeWrapped({}, "ur-key");
    row.content->append(*lineText_);
    retry_ = Gtk::make_managed<Gtk::Button>(ProtocolBoardLookup(pb::kTryAgainText));
    retry_->set_halign(Gtk::Align::START);
    retry_->signal_clicked().connect([this, alive = alive_] {
      if (*alive && on_retry) on_retry();
    });
    row.content->append(*retry_);
    lineRow_ = row.root;
    root_->append(*row.root);
  }

  // the card
  card_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  {
    auto row = MakePadded(10);
    auto* identity = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    tag_ = MakeLabel({}, nullptr);
    SetPixelSize(*tag_, kTagPx);
    kit::MarkDecorative(*tag_);
    identity->append(*tag_);
    name_ = MakeLabel({}, "ur-row-title");
    name_->set_hexpand(true);
    name_->set_ellipsize(Pango::EllipsizeMode::END);
    identity->append(*name_);
    row.content->append(*identity);
    card_->append(*row.root);
  }
  {
    // the switch shared with Points (Q12): one "show my name" for both boards
    const Glib::ustring showName = ProtocolBoardLookup(pb::kShowNameText);
    auto row = kit::MakePaneTwoLineRow(showName, {}, 44);
    publicSwitch_ = Gtk::make_managed<Gtk::Switch>();
    publicSwitch_->set_valign(Gtk::Align::CENTER);
    kit::SetAccessibleLabel(*publicSwitch_, showName);
    publicSwitch_->property_active().signal_changed().connect([this, alive = alive_] {
      if (!*alive || applyingSwitch_) return;
      if (on_public) on_public(publicSwitch_->get_active());
    });
    row.trailing->append(*publicSwitch_);
    card_->append(*row.root);
  }
  {
    // "#173" over its caption lines, then the label chip, the margin and the trend
    auto row = MakePadded(10, 8);
    positionBlock_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    position_ = MakeLabel({}, "ur-stat-value");
    SetPixelSize(*position_, kPositionPx);
    position_->set_valign(Gtk::Align::END);
    positionBlock_->append(*position_);
    auto* captions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    captions->set_valign(Gtk::Align::END);
    captions->set_hexpand(true);
    captions->set_margin_bottom(4);
    projected_ = MakeLabel({}, "ur-caption");
    ofRanked_ = MakeLabel({}, "ur-caption");
    // a wrapping label asks for a narrow line unless told how wide it may grow
    ofRanked_->set_wrap(true);
    ofRanked_->set_max_width_chars(40);
    captions->append(*projected_);
    captions->append(*ofRanked_);
    positionBlock_->append(*captions);
    row.content->append(*positionBlock_);
    auto* chips = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    label_ = MakeLabel({}, "ur-protocol-chip");
    label_->set_valign(Gtk::Align::CENTER);
    chips->append(*label_);
    margin_ = MakeWrapped({}, "ur-value");
    margin_->set_hexpand(true);
    margin_->set_max_width_chars(48);
    chips->append(*margin_);
    row.content->append(*chips);
    trend_ = MakeWrapped({}, "ur-key");
    row.content->append(*trend_);
    card_->append(*row.root);
  }
  {
    auto row = MakePadded(10, 4);
    chartTitle_ = MakeLabel({}, "ur-caption");
    kit::MarkDecorative(*chartTitle_);  // the chart's own name says it
    row.content->append(*chartTitle_);
    chart_ = Gtk::make_managed<ProtocolRankChart>();
    chart_->on_point = [this, alive = alive_](std::optional<size_t> index) {
      if (*alive) ShowPoint(index);
    };
    row.content->append(*chart_);
    chartPoint_ = MakeLabel({}, "ur-key");
    chartPoint_->set_visible(false);
    row.content->append(*chartPoint_);
    chartRow_ = row.root;
    card_->append(*row.root);
  }
  {
    // why this score: counts only, never an IP
    auto row = MakePadded(8, 4);
    why_ = MakeWrapped({}, "ur-row-note");
    solo_ = MakeWrapped({}, "ur-row-note");
    row.content->append(*why_);
    row.content->append(*solo_);
    card_->append(*row.root);
  }
  {
    auto row = MakePadded(8);
    notMeasurable_ = MakeWrapped({}, "ur-protocol-borderline");
    row.content->append(*notMeasurable_);
    notMeasurableRow_ = row.root;
    card_->append(*row.root);
  }
  {
    // the claim prompt: a note while binding is closed, the screen's one
    // primary button once it opens, and always the warning
    auto row = MakePadded(10);
    auto* prompt = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    prompt->add_css_class("ur-protocol-prompt");
    promptHeadline_ = MakeWrapped({}, "ur-row-title");
    prompt->append(*promptHeadline_);
    claimButton_ = Gtk::make_managed<Gtk::Button>();
    claimButton_->add_css_class("ur-pane-primary");
    claimButton_->add_css_class("ur-protocol-claim");
    claimButton_->signal_clicked().connect([this, alive = alive_] {
      if (*alive && on_claim) on_claim();
    });
    prompt->append(*claimButton_);
    promptSoon_ = MakeWrapped({}, "ur-key");
    prompt->append(*promptSoon_);
    promptBorderline_ = MakeWrapped({}, "ur-protocol-borderline");
    prompt->append(*promptBorderline_);
    promptWarning_ = MakeWrapped({}, "ur-row-note");
    prompt->append(*promptWarning_);
    row.content->append(*prompt);
    promptRow_ = row.root;
    card_->append(*row.root);
  }
  root_->append(*card_);

  // the explainer, in every state
  {
    auto row = MakePadded(10);
    explainer_ = MakeWrapped({}, "ur-row-note");
    row.content->append(*explainer_);
    root_->append(*row.root);
  }
}

void ProtocolSide::Render(const std::vector<std::string>& banner, const pb::Card& card,
                          const std::string& explainer) {
  std::string bannerText;
  for (const std::string& line : banner) {
    if (!bannerText.empty()) bannerText += "\n";
    bannerText += line;
  }
  bannerText_->set_text(bannerText);
  bannerRow_->set_visible(!banner.empty());
  explainer_->set_text(explainer);

  const bool placeholder = card.body == pb::CardBody::Placeholder;
  placeholderRow_->set_visible(placeholder);
  kit::SetBusy(*root_, placeholder);
  if (placeholder) {
    kit::SetAccessibleLabel(*placeholderRow_, ProtocolBoardLookup(pb::kLoadingText));
  }
  const bool line = card.body == pb::CardBody::SignIn || card.body == pb::CardBody::LoadFailed ||
                    card.body == pb::CardBody::NotRanked;
  lineRow_->set_visible(line);
  lineText_->set_text(card.bodyText);
  retry_->set_visible(card.body == pb::CardBody::LoadFailed);
  card_->set_visible(card.body == pb::CardBody::Ranked);
  if (card.body != pb::CardBody::Ranked) return;

  tag_->set_text(card.tag);
  tag_->set_visible(!card.tag.empty());
  name_->set_text(card.name);
  SetClass(*name_, "dim-label", !card.named);

  position_->set_text(card.position);
  projected_->set_text(card.projected);
  kit::SetTextOrCollapse(*ofRanked_, card.ofRanked);
  // the position reads as one fact: "#173 projected, of 41,230 ranked networks"
  std::string positionName = card.position + " " + card.projected;
  if (!card.ofRanked.empty()) positionName += ", " + card.ofRanked;
  kit::SetAccessibleLabel(*positionBlock_, positionName);
  for (Gtk::Label* part : {position_, projected_, ofRanked_}) kit::MarkDecorative(*part);

  SetLabelChip(*label_, card.label, card.labelText);
  margin_->set_text(card.margin);
  kit::SetTextOrCollapse(*trend_, card.trendText);
  SetClass(*trend_, "ur-protocol-trend-up", card.trend == pb::Trend::Up);
  SetClass(*trend_, "ur-protocol-trend-down", card.trend == pb::Trend::Down);

  chartRow_->set_visible(card.chart.show);
  chartTitle_->set_text(card.chart.title);
  // a new chart drops the day a press named on the old one
  const bool sameChart = chartShown_.points.size() == card.chart.points.size() &&
                         std::equal(chartShown_.points.begin(), chartShown_.points.end(),
                                    card.chart.points.begin(),
                                    [](const pb::ChartPoint& a, const pb::ChartPoint& b) {
                                      return a.rank == b.rank && a.x == b.x;
                                    }) &&
                         chartShown_.accessibleName == card.chart.accessibleName;
  if (!sameChart) {
    chartShown_ = card.chart;
    chart_->SetChart(card.chart);
    ShowPoint(std::nullopt);
  }

  why_->set_text(card.why);
  solo_->set_text(card.solo);
  notMeasurable_->set_text(card.notMeasurable);
  notMeasurableRow_->set_visible(!card.notMeasurable.empty());

  promptRow_->set_visible(card.claim.show);
  promptHeadline_->set_text(card.claim.headline);
  claimButton_->set_label(card.claim.buttonText);
  claimButton_->set_visible(card.claim.button);
  kit::SetTextOrCollapse(*promptSoon_, card.claim.soon);
  kit::SetTextOrCollapse(*promptBorderline_, card.claim.borderline);
  kit::SetTextOrCollapse(*promptWarning_, card.claim.warning);
}

void ProtocolSide::SetClaimPrimary(bool primary) {
  SetClass(*claimButton_, "ur-pane-primary", primary);
  SetClass(*claimButton_, "ur-pane-secondary", !primary);
}

void ProtocolSide::SetPublic(bool on, bool sensitive) {
  // THE ECHO GUARD: the handler cannot tell a user flip from this write
  applyingSwitch_ = true;
  publicSwitch_->set_active(on);
  applyingSwitch_ = false;
  publicSwitch_->set_sensitive(sensitive);
}

void ProtocolSide::ShowPoint(std::optional<size_t> index) {
  if (!index || *index >= chartShown_.points.size()) {
    chartPoint_->set_visible(false);
    return;
  }
  chartPoint_->set_text(pb::PointText(chartShown_.points[*index]));
  chartPoint_->set_visible(true);
}

// ---- the board ---------------------------------------------------------------------

ProtocolBoard::ProtocolBoard(SdkHost& host)
    : host_(host),
      binding_(settings_, [alive = alive_](int delayMs, std::function<void()> fire) {
        // a request's give-up: nothing once the board is gone
        Glib::signal_timeout().connect_once(
            [alive, fire = std::move(fire)] {
              if (*alive) fire();
            },
            delayMs);
      }) {
  wide_ = std::make_unique<ProtocolSide>(alive_, /*withHeader=*/true);
  compactSide_ = std::make_unique<ProtocolSide>(alive_, /*withHeader=*/false);
  for (ProtocolSide* side : {wide_.get(), compactSide_.get()}) {
    side->on_claim = [this] {
      if (on_claim) on_claim();
    };
    side->on_public = [this](bool requested) {
      if (on_public) on_public(requested);
    };
    side->on_retry = [this] { RetryMine(); };
  }
  BuildList();
  binding_.on_changed = [this] { Render(); };
  binding_.on_mine_changed = [this] {
    if (on_mine_changed) on_mine_changed();
  };
  // a seek's pages have landed: its row to the top (the end, when the list
  // ends before it)
  binding_.on_loaded_through = [this](int64_t reached) {
    ScrollToPosition(seekTarget_ ? std::min(*seekTarget_, reached) : reached);
    seekTarget_.reset();
  };
  Render();
}

ProtocolBoard::~ProtocolBoard() { Release(); }

void ProtocolBoard::Release() {
  *alive_ = false;  // every marshaled answer, timer and handler does nothing from here
  scrollConn_.disconnect();
  rangeConn_.disconnect();
  // the requests only: by now the window's teardown may have disposed the
  // widgets (EarningsPage, issue #13)
  binding_.on_changed = nullptr;
  binding_.on_mine_changed = nullptr;
  binding_.on_loaded_through = nullptr;
  binding_.Release();
}

void ProtocolBoard::BuildList() {
  listHost_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  listHost_->set_vexpand(true);

  // the side block on top of the list while pane C is folded away
  compactSlot_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  compactSlot_->append(*compactSide_->root());
  compactSlot_->set_visible(false);
  listHost_->append(*compactSlot_);

  // a failed refresh: the rows stay, under this notice and its retry
  {
    auto row = MakePadded(8);
    auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* text = MakeWrapped(ProtocolBoardLookup(pb::kSomethingWentWrongText), "ur-error-text");
    text->set_hexpand(true);
    line->append(*text);
    auto* again = Gtk::make_managed<Gtk::Button>(ProtocolBoardLookup(pb::kTryAgainText));
    again->add_css_class("flat");
    again->signal_clicked().connect([this, alive = alive_] {
      if (*alive) RetryList();
    });
    line->append(*again);
    row.content->append(*line);
    notice_ = row.root;
    notice_->set_visible(false);
    listHost_->append(*row.root);
  }

  // placeholders of the rows' own size while the first page is out
  placeholders_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  for (int i = 0; i < kPlaceholderRows; ++i) {
    auto* row = kit::MakePaneRow(kRowHeight);
    auto* inner = dynamic_cast<Gtk::Box*>(row->get_first_child());
    if (inner != nullptr) {
      inner->set_spacing(8);
      auto* position = kit::MakeSkeletonLabel("#000", "ur-key");
      position->set_size_request(kPositionWidth, -1);
      inner->append(*position);
      auto* name = kit::MakeSkeletonLabel(i % 3 == 0 ? "sample-network" : "sample-network-name",
                                          "ur-row-title");
      name->set_hexpand(true);
      name->set_halign(Gtk::Align::START);
      inner->append(*name);
      inner->append(*kit::MakeSkeletonLabel("000.0", "ur-value"));
    }
    placeholders_->append(*row);
  }
  kit::SetAccessibleLabel(*placeholders_, ProtocolBoardLookup(pb::kLoadingText));
  listHost_->append(*placeholders_);

  rows_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  kit::SetAccessibleLabel(*rows_, ProtocolBoardLookup(pb::kTabText));
  listHost_->append(*rows_);

  // a list without rows: the sign-in line, the empty line, or the failure
  // with its retry under it, centred together in the pane
  statusBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  statusBox_->set_vexpand(true);
  statusBox_->set_valign(Gtk::Align::CENTER);
  status_ = kit::MakePaneEmptyLine({});
  status_->set_vexpand(false);
  statusBox_->append(*status_);
  retry_ = Gtk::make_managed<Gtk::Button>(ProtocolBoardLookup(pb::kTryAgainText));
  retry_->set_halign(Gtk::Align::CENTER);
  retry_->set_visible(false);
  retry_->signal_clicked().connect([this, alive = alive_] {
    if (*alive) RetryList();
  });
  statusBox_->append(*retry_);
  listHost_->append(*statusBox_);

  // the next page: its progress, or its failure's retry
  auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  footer->set_margin(12);
  footer->set_halign(Gtk::Align::CENTER);
  footerSpinner_ = Gtk::make_managed<Gtk::Spinner>();
  footerSpinner_->set_visible(false);
  kit::SetAccessibleLabel(*footerSpinner_, ProtocolBoardLookup(pb::kLoadingText));
  footer->append(*footerSpinner_);
  footerRetry_ = Gtk::make_managed<Gtk::Button>(ProtocolBoardLookup(pb::kTryAgainText));
  footerRetry_->set_visible(false);
  footerRetry_->signal_clicked().connect([this, alive = alive_] {
    if (*alive) binding_.LoadMore();
  });
  footer->append(*footerRetry_);
  listHost_->append(*footer);
}

void ProtocolBoard::SetScroller(Gtk::ScrolledWindow* scroller) {
  scrollConn_.disconnect();
  rangeConn_.disconnect();
  scroller_ = scroller;
  if (scroller_ == nullptr) return;
  // the next page as the pane scrolls near the end of the rows, and the
  // indicator's thumb at the first row in view
  scrollConn_ = scroller_->get_vadjustment()->signal_value_changed().connect([this] {
    if (!*alive_) return;
    MaybeLoadMore();
    RefreshPosition();
  });
  // the range changes as pages land and the pane resizes: the thumb re-fits
  rangeConn_ = scroller_->get_vadjustment()->signal_changed().connect([this] {
    if (!*alive_) return;
    MaybeLoadMore();
    RefreshPosition();
  });
}

void ProtocolBoard::SetCompact(bool compact) {
  compact_ = compact;
  compactSlot_->set_visible(compact);
}

void ProtocolBoard::SetShowing(bool showing) {
  showing_ = showing;
  UpdateIndicator();
  if (!showing) return;
  auto alive = alive_;
  Glib::signal_idle().connect_once([this, alive] {
    if (*alive) MaybeLoadMore();
  });
}

bool ProtocolBoard::SignedIn() const {
  if (sampleSignedIn_) return *sampleSignedIn_;
  return can_call_api && can_call_api();
}

// The service is attached to the account's Api once, and again only for a
// different Api (a server or extender change replaces it): the binding's
// source is the Api's handle, which no other Api ever has.
void ProtocolBoard::EnsureService() {
  const uint64_t source = host_.api().handle();
  if (binding_.Attached() && binding_.Source() == source) return;
  binding_.Attach(std::make_unique<SdkService>(host_, alive_), source);
}

void ProtocolBoard::LoadMine() {
  if (samplePinned_) return;
  if (previewMode_ || !SignedIn()) {
    // no session: no service and no request (a 401 must never arrive to be
    // read as "not ranked"), and the departed account's place goes
    binding_.Release();
    Render();
    if (on_mine_changed) on_mine_changed();
    return;
  }
  EnsureService();
  binding_.RefreshMe();
  Render();
}

void ProtocolBoard::LoadList() {
  if (samplePinned_) return;
  listRequested_ = true;
  if (previewMode_ || !SignedIn()) {
    binding_.Release();
    Render();
    return;
  }
  EnsureService();
  binding_.Refresh();
  Render();
}

void ProtocolBoard::Reload() {
  if (samplePinned_ || previewMode_ || !SignedIn()) return;
  EnsureService();
  binding_.RefreshMe();
  if (listRequested_) binding_.Refresh();
}

void ProtocolBoard::ResetForSignOut() {
  binding_.Release();  // the departed account's requests: nothing they answer lands now
  listRequested_ = false;
  samplePinned_ = false;
  sampleSignedIn_.reset();
  ClearRows();
  Render();
}

void ProtocolBoard::RetryList() {
  if (!binding_.Attached()) {
    LoadList();
    return;
  }
  binding_.Refresh();
}

void ProtocolBoard::RetryMine() {
  if (!binding_.Attached()) {
    LoadMine();
    return;
  }
  binding_.RefreshMe();
}

std::optional<bool> ProtocolBoard::PublicName() const {
  const pb::MeState& mine = binding_.Mine();
  if (!mine.loaded || !mine.me) return std::nullopt;
  return !api_keys::Trimmed(SanitizeExternalDisplayText(mine.me->row.name)).empty();
}

void ProtocolBoard::SetPublic(bool on, bool sensitive) {
  wide_->SetPublic(on, sensitive);
  compactSide_->SetPublic(on, sensitive);
}

void ProtocolBoard::SetClaimPrimary(bool primary) {
  wide_->SetClaimPrimary(primary);
  compactSide_->SetClaimPrimary(primary);
}

void ProtocolBoard::ScrollToTop() {
  if (scroller_ == nullptr) return;
  auto adjustment = scroller_->get_vadjustment();
  adjustment->set_value(adjustment->get_lower());
}

// ---- rendering ------------------------------------------------------------------------

void ProtocolBoard::Render() {
  const pb::Words words = ProtocolBoardWords();
  const bool signedIn = SignedIn();
  const pb::ListState& list = binding_.List();
  const pb::MeState& mine = binding_.Mine();
  const std::vector<std::string> banner = pb::BannerLines(pb::ShownSnapshot(list, mine), words);
  const pb::Card card = pb::CardFor(mine, list.snapshot, signedIn, words);
  const std::string explainer = ProtocolBoardLookup(pb::kExplainerText);
  wide_->Render(banner, card, explainer);
  compactSide_->Render(banner, card, explainer);
  RenderList(words, list, pb::ListScreenFor(list, signedIn));
  if (on_list_changed) on_list_changed();
}

void ProtocolBoard::ClearRows() {
  RemoveAllChildren(*rows_);
  rowWidgets_.clear();
  drawnRows_.clear();
  drawnSnapshot_.clear();
  lineDrawn_ = false;
}

void ProtocolBoard::RenderList(const pb::Words& words, const pb::ListState& list,
                               const pb::ListScreen& screen) {
  placeholders_->set_visible(screen.body == pb::ListBody::Placeholder);
  kit::SetBusy(*listHost_, screen.body == pb::ListBody::Placeholder);
  const std::optional<pb::Text> text = pb::ListBodyText(screen.body);
  kit::SetTextOrCollapse(*status_, text ? Glib::ustring(ProtocolBoardLookup(*text)) : Glib::ustring());
  retry_->set_visible(screen.body == pb::ListBody::LoadFailed);
  statusBox_->set_visible(text.has_value());
  notice_->set_visible(screen.refreshFailed);
  footerSpinner_->set_visible(screen.loadingMore);
  if (screen.loadingMore) {
    footerSpinner_->start();
  } else {
    footerSpinner_->stop();
  }
  footerRetry_->set_visible(screen.moreFailed);
  if (screen.body != pb::ListBody::Rows) {
    ClearRows();
    UpdateIndicator();
    return;
  }

  // a page that extends the rows drawn is appended below them; any other
  // change (a refresh, a new snapshot, a row that reads differently) redraws
  std::vector<pb::RowView> views;
  std::vector<std::string> readings;
  views.reserve(list.rows.size());
  readings.reserve(list.rows.size());
  for (const pb::Row& row : list.rows) {
    views.push_back(pb::RowViewFor(row, words));
    const pb::RowView& view = views.back();
    readings.push_back(view.accessibleName + "\n" + view.tag + "\n" +
                       (view.aboveLine ? "i" : "") + (view.isMe ? "m" : "") +
                       (view.dimmed ? "d" : "") + (view.claimed ? "c" : ""));
  }
  const std::string snapshotId = list.snapshot ? list.snapshot->id : std::string();
  const bool extends = snapshotId == drawnSnapshot_ && readings.size() >= drawnRows_.size() &&
                       std::equal(drawnRows_.begin(), drawnRows_.end(), readings.begin());
  if (extends && readings.size() == drawnRows_.size()) return;
  size_t from = drawnRows_.size();
  if (!extends) {
    ClearRows();
    from = 0;
  }
  const std::optional<size_t> lineAt = pb::LineIndex(list.rows);
  for (size_t i = from; i < views.size(); ++i) {
    if (list.snapshot && lineAt && *lineAt == i && !lineDrawn_) {
      rows_->append(*MakeLine(pb::LineFor(*list.snapshot, words)));
      lineDrawn_ = true;
    }
    Gtk::Widget* row = MakeRow(views[i]);
    rows_->append(*row);
    rowWidgets_.emplace_back(views[i].position, row);
  }
  drawnSnapshot_ = snapshotId;
  drawnRows_ = std::move(readings);
  // a page that does not fill the pane can never be scrolled to its end, so
  // the next one is asked for once layout has run; the indicator re-fits
  auto alive = alive_;
  Glib::signal_idle().connect_once([this, alive] {
    if (!*alive) return;
    MaybeLoadMore();
    RefreshPosition();
  });
}

// One ranked row: the position, the tag and name, "You" and the label chip on
// the caller's row, the UID badges, the not-measurable mark, the score with
// its trend arrow. A claimed row is a button that opens its detail sheet.
Gtk::Widget* ProtocolBoard::MakeRow(const pb::RowView& view) {
  auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  line->set_hexpand(true);
  line->set_valign(Gtk::Align::CENTER);

  auto* position = MakeLabel(view.positionText, "ur-key");
  position->set_size_request(kPositionWidth, -1);
  line->append(*position);
  if (!view.tag.empty()) line->append(*MakeLabel(view.tag, nullptr));
  auto* name = MakeLabel(view.name, "ur-row-title");
  name->set_hexpand(true);
  name->set_ellipsize(Pango::EllipsizeMode::END);
  if (!view.named) name->add_css_class("dim-label");
  line->append(*name);
  if (view.isMe) {
    line->append(*MakeLabel(view.youText, "ur-protocol-you"));
    if (view.label) {
      auto* chip = MakeLabel({}, "ur-protocol-chip");
      SetLabelChip(*chip, *view.label, view.labelText);
      line->append(*chip);
    }
  }
  for (const std::string& badge : view.badges) line->append(*MakeLabel(badge, "ur-protocol-uid"));
  if (view.dimmed) line->append(*MakeNotMeasurableMark());
  auto* score = MakeLabel(view.score, "ur-value", 1.f);
  score->set_size_request(kScoreWidth, -1);
  line->append(*score);
  auto* arrow = MakeLabel(view.trendArrow, "ur-protocol-arrow", 0.5f);
  arrow->set_size_request(kArrowWidth, -1);
  SetClass(*arrow, "ur-protocol-trend-up", view.trend == pb::Trend::Up);
  SetClass(*arrow, "ur-protocol-trend-down", view.trend == pb::Trend::Down);
  if (!view.trendText.empty()) arrow->set_tooltip_text(view.trendText);
  line->append(*arrow);
  if (view.dimmed) line->add_css_class("ur-protocol-dimmed");
  // the row's name says all of it, once (pb::RowViewFor's accessibleName)
  for (Gtk::Widget* part = line->get_first_child(); part != nullptr; part = part->get_next_sibling()) {
    kit::MarkDecorative(*part);
  }

  Gtk::Widget* root = nullptr;
  if (view.claimed) {
    auto* button = Gtk::make_managed<Gtk::Button>();
    button->add_css_class("ur-pane-row");
    button->add_css_class("ur-protocol-row");
    button->set_size_request(-1, kRowHeight);
    button->set_child(*line);
    const int64_t at = view.position;
    button->signal_clicked().connect([this, alive = alive_, at] {
      if (*alive) OpenDetail(at);
    });
    root = button;
  } else {
    auto* row = kit::MakePaneRow(kRowHeight);
    if (auto* inner = dynamic_cast<Gtk::Box*>(row->get_first_child())) inner->append(*line);
    root = row;
  }
  kit::SetAccessibleLabel(*root, view.accessibleName);
  // the caller's row is the point of the board (card fill, never an accent,
  // as the other boards draw it); the rows inside the line are tinted
  if (view.isMe) {
    root->add_css_class("ur-earn-own-row");
  } else if (view.aboveLine) {
    root->add_css_class("ur-protocol-inside");
  }
  return root;
}

// The divider between 200 and 201: a dashed rule in the accent with "Top 200
// line · score 41.2", read as "Top 200 line, score 41.2".
Gtk::Widget* ProtocolBoard::MakeLine(const pb::Line& line) {
  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  box->set_size_request(-1, kLineHeight);
  box->set_margin_start(12);
  box->set_margin_end(12);
  auto* rule = Gtk::make_managed<Gtk::Box>();
  rule->add_css_class("ur-protocol-rule");
  rule->set_hexpand(true);
  rule->set_valign(Gtk::Align::CENTER);
  box->append(*rule);
  auto* text = MakeLabel(line.text, "ur-protocol-line");
  kit::MarkDecorative(*text);
  box->append(*text);
  auto* tail = Gtk::make_managed<Gtk::Box>();
  tail->add_css_class("ur-protocol-rule");
  tail->set_size_request(12, -1);
  tail->set_valign(Gtk::Align::CENTER);
  box->append(*tail);
  kit::MarkDecorative(*rule);
  kit::MarkDecorative(*tail);
  kit::SetAccessibleLabel(*box, line.accessibleName);
  return box;
}

void ProtocolBoard::MaybeLoadMore() {
  if (!showing_ || scroller_ == nullptr) return;
  const pb::ListState& list = binding_.List();
  const pb::ListScreen screen = pb::ListScreenFor(list, SignedIn());
  if (!screen.canLoadMore || list.loadingMore || list.loading || list.moreFailed) return;
  auto adjustment = scroller_->get_vadjustment();
  // before the pane has a size there is no end to be near
  if (adjustment->get_page_size() <= 0) return;
  const double remaining =
      adjustment->get_upper() - (adjustment->get_value() + adjustment->get_page_size());
  if (remaining <= kRowHeight * kMoreWithinRows) binding_.LoadMore();
}

// A seek's row is loaded: the scroller puts it at the top of the pane. Rows
// just appended have no place yet, and the scroll range grows only as layout
// runs, so the scroll lands from the frame clock, which sees the previous
// frame's finished layout. A seek that cannot land within a beat goes as far
// as the range allows, and stops, so it never fights the user.
void ProtocolBoard::ScrollToPosition(int64_t position) {
  pendingScroll_ = position;
  pendingFrames_ = 0;
  if (scroller_ == nullptr || scrollTicking_) return;
  scrollTicking_ = true;
  scroller_->add_tick_callback([this, alive = alive_](const Glib::RefPtr<Gdk::FrameClock>&) {
    if (!*alive) return false;  // the board is gone: touch nothing of it
    const bool last = ++pendingFrames_ >= kSeekFrames;
    if (!ApplyPendingScroll(last) && !last) return true;
    pendingScroll_.reset();
    scrollTicking_ = false;
    return false;
  });
}

bool ProtocolBoard::ApplyPendingScroll(bool last) {
  if (!pendingScroll_ || scroller_ == nullptr) return true;
  Gtk::Widget* target = nullptr;
  for (const auto& [at, widget] : rowWidgets_) {
    if (at == *pendingScroll_) {
      target = widget;
      break;
    }
  }
  Gtk::Widget* content = scroller_->get_child();
  if (auto* viewport = dynamic_cast<Gtk::Viewport*>(content)) content = viewport->get_child();
  if (target == nullptr || content == nullptr) return false;
  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(target->gobj(), content->gobj(), &bounds) ||
      bounds.size.height <= 0) {
    return false;  // not laid out yet
  }
  auto adjustment = scroller_->get_vadjustment();
  const double top = static_cast<double>(bounds.origin.y);
  const double reach =
      std::max(adjustment->get_lower(), adjustment->get_upper() - adjustment->get_page_size());
  if (top > reach + 0.5 && !last) return false;  // the range has not grown to it yet
  adjustment->set_value(std::clamp(top, adjustment->get_lower(), reach));
  RefreshPosition();
  return true;
}

// ---- the position indicator -----------------------------------------------------------
// A slider over positions 1..400 at the pane's right edge while the list is
// longer than the pane (mmm/DESIGNSTYLE.md "Long ranked lists: tab reset and a
// draggable position indicator"), drawn as the Points board's is: a faint
// track and the accent thumb, at the first row in view over the positions,
// the rows in view long. Dragging shows the rank and tier beside it; letting
// go (or a key on it) pages the list to that position and scrolls there.

namespace {

constexpr double kIndicatorWidth = 32.0;  // the 24px thumb with 4px of air each side
constexpr double kIndicatorPad = 8.0;     // the track stops short of the pane's ends
constexpr double kIndicatorTrackWidth = 6.0;

void RoundedRectPath(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w,
                     double h, double r) {
  r = std::min(r, std::min(w, h) / 2.0);
  cr->begin_new_sub_path();
  cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
  cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
  cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
  cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cr->close_path();
}

// "#200" with "Top 1%" beneath, by the SDK's shared helper over the whole
// ranked population.
std::pair<std::string, std::string> ScrubText(int64_t position, int64_t totalRanked) {
  std::string rank = pb::PositionText(position);
  std::string tier;
  if (auto parts = urnet::pointsLeaderboardScrollLabel(position, std::max(totalRanked, position))) {
    if (!parts->rank_text.empty()) rank = parts->rank_text;
    const std::string tierKey = leaderboard::TierLabelKey(parts->tier);
    if (tierKey == "leaderboard_tier_top") {
      tier = Format(T_("leaderboard_tier_top", "Top {}%"), parts->tier_percent);
    } else if (tierKey == "leaderboard_tier_rest") {
      tier = T_("leaderboard_tier_rest", "Everyone else");
    }
  }
  return {rank, tier};
}

}  // namespace

void ProtocolBoard::AttachIndicator(Gtk::Overlay& overlay) {
  // a slider to assistive tech; the role is construct-only, so the area comes
  // from the C constructor
  GtkWidget* raw = GTK_WIDGET(g_object_new(GTK_TYPE_DRAWING_AREA, "accessible-role",
                                           GTK_ACCESSIBLE_ROLE_SLIDER, nullptr));
  indicator_ = Gtk::manage(Glib::wrap(GTK_DRAWING_AREA(raw)));
  indicator_->set_content_width(static_cast<int>(kIndicatorWidth));
  indicator_->set_halign(Gtk::Align::END);
  indicator_->set_valign(Gtk::Align::FILL);
  indicator_->set_visible(false);
  indicator_->set_focusable(true);
  indicator_->set_cursor("grab");
  kit::SetAccessibleLabel(*indicator_, T_("leaderboard_position_indicator", "Leaderboard position"));
  indicator_->set_draw_func(sigc::mem_fun(*this, &ProtocolBoard::DrawIndicator));
  overlay.add_overlay(*indicator_);
  overlay.set_measure_overlay(*indicator_, false);

  auto drag = Gtk::GestureDrag::create();
  drag->set_button(GDK_BUTTON_PRIMARY);
  drag->signal_drag_begin().connect([this, alive = alive_, drag](double, double y) {
    if (*alive) OnDragBegin(y, drag);
  });
  drag->signal_drag_update().connect([this, alive = alive_](double, double dy) {
    if (*alive) OnDragUpdate(dy);
  });
  drag->signal_drag_end().connect([this, alive = alive_](double, double dy) {
    if (*alive) OnDragEnd(dy);
  });
  indicator_->add_controller(drag);
  // the keyboard: arrows by the rows in view, page keys by a tenth, Home and End
  auto key = Gtk::EventControllerKey::create();
  key->signal_key_pressed().connect(
      [this, alive = alive_](guint keyval, guint, Gdk::ModifierType) -> bool {
        return *alive && OnIndicatorKey(keyval);
      },
      false);
  indicator_->add_controller(key);

  // the rank and tier beside the thumb while it is dragged
  indicatorLabel_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  indicatorLabel_->add_css_class("ur-earn-scrub");
  indicatorLabel_->set_halign(Gtk::Align::END);
  indicatorLabel_->set_valign(Gtk::Align::START);
  indicatorLabel_->set_margin_end(static_cast<int>(kIndicatorWidth) + 8);
  indicatorLabel_->set_visible(false);
  indicatorLabel_->set_can_target(false);
  kit::MarkDecorative(*indicatorLabel_);  // the slider's value text says the same
  indicatorRank_ = MakeLabel({}, "ur-value", 1.f);
  indicatorRank_->set_halign(Gtk::Align::END);
  indicatorTier_ = MakeLabel({}, "dim-label", 1.f);
  indicatorTier_->add_css_class("caption");
  indicatorTier_->set_halign(Gtk::Align::END);
  indicatorLabel_->append(*indicatorRank_);
  indicatorLabel_->append(*indicatorTier_);
  overlay.add_overlay(*indicatorLabel_);
  overlay.set_measure_overlay(*indicatorLabel_, false);
}

// The positions the list spans: the top 400, or fewer when fewer are ranked.
int64_t ProtocolBoard::IndicatorTotal() const {
  const pb::ListState& list = binding_.List();
  const int64_t last = list.rows.empty() ? 0 : list.rows.back().position;
  int64_t total = list.snapshot ? std::min<int64_t>(list.snapshot->totalRanked, pb::kListPositions)
                                : last;
  return std::max(total, last);
}

// The rows a screenful of the pane holds; a typical pane's before it has a size.
int64_t ProtocolBoard::VisibleRows() const {
  const double page = scroller_ != nullptr ? scroller_->get_vadjustment()->get_page_size() : 0.0;
  if (page <= 0) return kUnlaidVisibleRows;
  return std::max<int64_t>(1, static_cast<int64_t>(page / kRowHeight));
}

// The first row in view, from the scroller: the rows are found by where they
// are laid out (the Top 200 line and the side block on top of the list are
// not rows), then the thumb follows it.
void ProtocolBoard::RefreshPosition() {
  if (scroller_ != nullptr && !rowWidgets_.empty()) {
    Gtk::Widget* content = scroller_->get_child();
    if (auto* viewport = dynamic_cast<Gtk::Viewport*>(content)) content = viewport->get_child();
    const double offset = scroller_->get_vadjustment()->get_value();
    size_t low = 0;
    size_t high = rowWidgets_.size();
    while (content != nullptr && low < high) {
      const size_t middle = low + (high - low) / 2;
      graphene_rect_t bounds;
      if (!gtk_widget_compute_bounds(rowWidgets_[middle].second->gobj(), content->gobj(), &bounds)) {
        break;
      }
      if (bounds.origin.y + bounds.size.height <= offset) {
        low = middle + 1;
      } else {
        high = middle;
      }
    }
    firstVisible_ = rowWidgets_[std::min(low, rowWidgets_.size() - 1)].first;
  } else {
    firstVisible_ = 1;
  }
  UpdateIndicator();
}

leaderboard::Thumb ProtocolBoard::IndicatorThumb(double& trackTop, double& trackHeight) const {
  trackTop = 0;
  trackHeight = 0;
  if (scroller_ == nullptr || indicator_ == nullptr) return {};
  // the track spans the scroller, not the pane's header above it
  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(GTK_WIDGET(scroller_->gobj()), GTK_WIDGET(indicator_->gobj()),
                                 &bounds)) {
    return {};
  }
  trackTop = bounds.origin.y + kIndicatorPad;
  trackHeight = bounds.size.height - 2 * kIndicatorPad;
  auto adjustment = scroller_->get_vadjustment();
  leaderboard::Thumb thumb =
      leaderboard::ThumbFor(firstVisible_, VisibleRows(), IndicatorTotal(), trackHeight,
                            adjustment->get_upper(), adjustment->get_page_size());
  if (thumb.visible && dragging_) thumb.top = dragTop_;
  return thumb;
}

void ProtocolBoard::UpdateIndicator() {
  if (indicator_ == nullptr) return;
  const pb::ListState& list = binding_.List();
  const bool rows = showing_ && list.loaded && !list.rows.empty();
  double trackTop = 0;
  double trackHeight = 0;
  const leaderboard::Thumb thumb = rows ? IndicatorThumb(trackTop, trackHeight) : leaderboard::Thumb{};
  const bool show = rows && thumb.visible;
  if (show != indicatorShown_) {
    indicatorShown_ = show;
    indicator_->set_visible(show);
    // the list steps in from under the indicator's strip
    listHost_->set_margin_end(show ? static_cast<int>(kIndicatorWidth) : 0);
    if (!show) {
      dragging_ = false;
      HideDragLabel(/*fade=*/false);
    }
    if (on_indicator_changed) on_indicator_changed();
  }
  if (!show) return;
  const int64_t position = dragging_ ? dragPosition_ : firstVisible_;
  const auto scrub = ScrubText(position, list.snapshot ? list.snapshot->totalRanked : 0);
  const std::string valueText = scrub.second.empty() ? scrub.first : scrub.first + ", " + scrub.second;
  gtk_accessible_update_property(GTK_ACCESSIBLE(indicator_->gobj()),
                                 GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, 1.0,
                                 GTK_ACCESSIBLE_PROPERTY_VALUE_MAX,
                                 static_cast<double>(IndicatorTotal()),
                                 GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, static_cast<double>(position),
                                 GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT, valueText.c_str(), -1);
  indicator_->queue_draw();
}

void ProtocolBoard::DrawIndicator(const Cairo::RefPtr<Cairo::Context>& cr, int width, int) {
  double trackTop = 0;
  double trackHeight = 0;
  const leaderboard::Thumb thumb = IndicatorThumb(trackTop, trackHeight);
  if (!thumb.visible) return;
  const double centre = width / 2.0;
  // the faint track
  RoundedRectPath(cr, centre - kIndicatorTrackWidth / 2, trackTop, kIndicatorTrackWidth,
                  trackHeight, kIndicatorTrackWidth / 2);
  SetSource(cr, kUrTextMuted, 0.22);
  cr->fill();
  // the accent thumb, brighter in hand
  RoundedRectPath(cr, centre - leaderboard::kThumbWidth / 2, trackTop + thumb.top,
                  leaderboard::kThumbWidth, thumb.height, 6.0);
  SetSource(cr, kUrAccent, dragging_ ? 1.0 : 0.85);
  cr->fill();
  // a grip: three short dark lines across the thumb's middle
  cr->set_source_rgba(0, 0, 0, 0.35);
  cr->set_line_width(1.5);
  const double gripY = trackTop + thumb.top + thumb.height / 2;
  for (int line = -1; line <= 1; ++line) {
    cr->move_to(centre - 5, gripY + line * 4);
    cr->line_to(centre + 5, gripY + line * 4);
  }
  cr->stroke();
}

void ProtocolBoard::OnDragBegin(double y, const Glib::RefPtr<Gtk::GestureDrag>& drag) {
  double trackTop = 0;
  double trackHeight = 0;
  const leaderboard::Thumb thumb = IndicatorThumb(trackTop, trackHeight);
  if (!thumb.visible || y < trackTop || y > trackTop + trackHeight) {
    drag->set_state(Gtk::EventSequenceState::DENIED);
    return;
  }
  drag->set_state(Gtk::EventSequenceState::CLAIMED);
  indicator_->grab_focus();
  dragging_ = true;
  // a press on the thumb picks it up where it is; a press on the track brings
  // the thumb under the pointer
  const double pressed = y - trackTop;
  const bool onThumb = pressed >= thumb.top && pressed <= thumb.top + thumb.height;
  dragStartTop_ = onThumb ? thumb.top
                          : std::clamp(pressed - thumb.height / 2, 0.0,
                                       std::max(0.0, trackHeight - thumb.height));
  dragTop_ = dragStartTop_;
  indicator_->set_cursor("grabbing");
  OnDragUpdate(0);
}

void ProtocolBoard::OnDragUpdate(double dy) {
  if (!dragging_) return;
  double trackTop = 0;
  double trackHeight = 0;
  const leaderboard::Thumb thumb = IndicatorThumb(trackTop, trackHeight);
  if (!thumb.visible) return;
  dragTop_ = std::clamp(dragStartTop_ + dy, 0.0, std::max(0.0, trackHeight - thumb.height));
  dragPosition_ =
      leaderboard::RankForThumbTop(dragTop_, thumb.height, trackHeight, IndicatorTotal());
  ShowDragLabel(dragPosition_);
  UpdateIndicator();
}

void ProtocolBoard::OnDragEnd(double dy) {
  if (!dragging_) return;
  OnDragUpdate(dy);
  dragging_ = false;
  if (indicator_ != nullptr) indicator_->set_cursor("grab");
  HideDragLabel(/*fade=*/true);
  Seek(dragPosition_);
  UpdateIndicator();
}

bool ProtocolBoard::OnIndicatorKey(guint keyval) {
  using leaderboard::Step;
  std::optional<Step> step;
  switch (keyval) {
    case GDK_KEY_Up: case GDK_KEY_KP_Up: step = Step::Up; break;
    case GDK_KEY_Down: case GDK_KEY_KP_Down: step = Step::Down; break;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: step = Step::PageUp; break;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: step = Step::PageDown; break;
    case GDK_KEY_Home: case GDK_KEY_KP_Home: step = Step::Home; break;
    case GDK_KEY_End: case GDK_KEY_KP_End: step = Step::End; break;
    default: break;
  }
  if (!step || IndicatorTotal() <= 0) return false;
  const int64_t position =
      leaderboard::StepRank(firstVisible_, *step, VisibleRows(), IndicatorTotal());
  ShowDragLabel(position);
  HideDragLabel(/*fade=*/true);
  Seek(position);
  return true;
}

// A position already loaded is a scroll; any other pages the list to a
// screenful past it first (the binding's LoadThrough, so the row can sit at
// the top of the pane), and the scroller goes there when they land.
void ProtocolBoard::Seek(int64_t position) {
  seekTarget_ = std::max<int64_t>(1, position);
  const int64_t through = std::min<int64_t>(pb::kListPositions, *seekTarget_ + VisibleRows());
  if (!binding_.LoadThrough(through)) {
    // the list cannot be paged further: as far as it goes
    seekTarget_.reset();
    ScrollToPosition(std::min(position, binding_.LastPosition()));
  }
}

void ProtocolBoard::ShowDragLabel(int64_t position) {
  if (indicatorLabel_ == nullptr) return;
  ++labelGen_;  // overtakes a fade in progress
  const pb::ListState& list = binding_.List();
  const auto scrub = ScrubText(position, list.snapshot ? list.snapshot->totalRanked : 0);
  indicatorRank_->set_text(scrub.first);
  kit::SetTextOrCollapse(*indicatorTier_, scrub.second);
  indicatorLabel_->set_visible(true);  // a hidden widget measures as nothing
  double trackTop = 0;
  double trackHeight = 0;
  const leaderboard::Thumb thumb = IndicatorThumb(trackTop, trackHeight);
  int minimum = 0, natural = 0, minimumBaseline = 0, naturalBaseline = 0;
  indicatorLabel_->measure(Gtk::Orientation::VERTICAL, -1, minimum, natural, minimumBaseline,
                           naturalBaseline);
  const double middle = trackTop + thumb.top + thumb.height / 2;
  indicatorLabel_->set_margin_top(static_cast<int>(std::max(0.0, middle - natural / 2.0)));
  indicatorLabel_->set_opacity(1.0);
}

// The label leaves after a beat: a short hold, then it goes.
void ProtocolBoard::HideDragLabel(bool fade) {
  if (indicatorLabel_ == nullptr) return;
  if (!fade) {
    ++labelGen_;
    indicatorLabel_->set_visible(false);
    return;
  }
  const uint64_t gen = labelGen_;
  auto alive = alive_;
  Glib::signal_timeout().connect_once(
      [this, alive, gen] {
        if (!*alive || gen != labelGen_ || dragging_) return;
        indicatorLabel_->set_visible(false);
      },
      900);
}

void ProtocolBoard::OpenDetail(int64_t position) {
  if (sheet_busy && sheet_busy()) {
    g_message("protocol board: detail sheet suppressed - a modal is already open");
    return;
  }
  const pb::ListState& list = binding_.List();
  const auto row = std::find_if(list.rows.begin(), list.rows.end(),
                                [position](const pb::Row& r) { return r.position == position; });
  if (row == list.rows.end() || row->fleets.empty()) return;
  if (present_sheet) present_sheet(MakeDetailSheet(pb::DetailFor(*row, ProtocolBoardWords())));
}

// ---- preview -----------------------------------------------------------------------

void ProtocolBoard::ShowPreviewState() {
  samplePinned_ = false;
  sampleSignedIn_.reset();
  binding_.Release();
  ClearRows();
  Render();
  if (on_mine_changed) on_mine_changed();
}

void ProtocolBoard::ApplyPreviewSample() {
  const char* state = g_getenv("URNETWORK_PREVIEW_PROTOCOL");
  g_warning("protocol board: preview sample pinned - the board is SYNTHETIC");
  samplePinned_ = true;
  sampleSignedIn_ = true;
  listRequested_ = true;
  binding_.Attach(std::make_unique<SampleService>(state != nullptr ? state : ""), /*source=*/1);
  binding_.RefreshMe();
  binding_.Refresh();
  auto alive = alive_;
  if (g_getenv("URNETWORK_PREVIEW_PROTOCOL_LINE") != nullptr) {
    Glib::signal_timeout().connect_once(
        [this, alive] {
          if (*alive) Seek(196);
        },
        300);
  }
  if (g_getenv("URNETWORK_PREVIEW_PROTOCOL_DETAIL") != nullptr) {
    Glib::signal_timeout().connect_once(
        [this, alive] {
          if (*alive) OpenDetail(1);
        },
        300);
  }
}

}  // namespace urnw
