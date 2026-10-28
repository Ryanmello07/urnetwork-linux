// SPDX-License-Identifier: MPL-2.0
#include "SlowInternetPill.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <gtk/gtk.h>

#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"
#include "UrMotion.hpp"

namespace urnw {
namespace {

// The pill's look (apple/DELAY.md §5.2): black, square corners and the
// island's 35% amber outline. The 1 px border is inside the bounds, so with
// it the copy sits 6 px from the leading edge and 10 from the trailing one,
// and the pill is 30 px tall. The label 14 and the numbers 13, semibold, the
// numbers in tabular digits; the arrows 10 bold at 55%, the worse
// direction's in amber. The recovered check is a 16 px green square. Two
// priorities above the application's sheet, as the Connect page's own.
constexpr const char* kPillCss = R"CSS(
.ur-slow-pill {
  background-color: #000000; border: 1px solid alpha(#F5C242, .35); border-radius: 0;
  min-height: 28px; padding: 0 9px 0 5px;
}
.ur-slow-pill-label { color: #F5C242; font-size: 14px; font-weight: 600; }
.ur-slow-pill-number {
  color: #F8F8F8; font-size: 13px; font-weight: 600; font-feature-settings: "tnum";
}
.ur-slow-pill-arrow { color: alpha(#F8F8F8, .55); font-size: 10px; font-weight: 700; }
.ur-slow-pill-worse .ur-slow-pill-number { color: #F5C242; }
.ur-slow-pill-worse .ur-slow-pill-arrow { color: alpha(#F5C242, .75); }
.ur-slow-pill-recovered { color: #F8F8F8; font-size: 13px; font-weight: 600; }
.ur-slow-pill-check {
  background-color: #87FB67; color: #101010; border-radius: 0;
  min-width: 16px; min-height: 16px; -gtk-icon-size: 12px;
}
)CSS";

void EnsurePillCss() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, kPillCss);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
}

std::string Text(const slow_internet::Copy& copy) { return T_(copy.key, copy.english); }

}  // namespace

// The copy's lines, picked as SwiftUI's ViewThatFits picks them: the first
// visible line whose natural width fits the width given, else the last. The
// others are allocated too but unmapped (child-visible off, as AdwSqueezer
// does it), so the width asked for never depends on which line shows: no
// narrower than the narrowest line, as wide as the widest.
class SlowInternetPill::Lines : public Gtk::Widget {
 public:
  ~Lines() override {
    for (Gtk::Widget* line : lines_) line->unparent();
  }

  void Append(Gtk::Widget& line) {
    line.set_parent(*this);
    lines_.push_back(&line);
  }

  // The line that shows in `width`, and the width it takes there.
  std::pair<Gtk::Widget*, int> Fit(int width) const {
    std::pair<Gtk::Widget*, int> fit{nullptr, 0};
    for (Gtk::Widget* line : lines_) {
      if (!line->get_visible()) continue;
      int minimum = 0, natural = 0, ignored = 0;
      line->measure(Gtk::Orientation::HORIZONTAL, -1, minimum, natural, ignored, ignored);
      fit = {line, std::max(minimum, std::min(width, natural))};
      if (natural <= width) break;
    }
    return fit;
  }

 protected:
  void measure_vfunc(Gtk::Orientation orientation, int, int& minimum, int& natural,
                     int& minimum_baseline, int& natural_baseline) const override {
    minimum = natural = 0;
    minimum_baseline = natural_baseline = -1;
    bool first = true;
    for (Gtk::Widget* line : lines_) {
      if (!line->get_visible()) continue;
      int lineMinimum = 0, lineNatural = 0, ignored = 0;
      line->measure(orientation, -1, lineMinimum, lineNatural, ignored, ignored);
      if (orientation == Gtk::Orientation::HORIZONTAL) {
        minimum = first ? lineMinimum : std::min(minimum, lineMinimum);
      } else {
        minimum = std::max(minimum, lineMinimum);
      }
      natural = std::max(natural, lineNatural);
      first = false;
    }
  }

  void size_allocate_vfunc(int width, int height, int baseline) override {
    const auto [fit, fitWidth] = Fit(width);
    for (Gtk::Widget* line : lines_) {
      if (!line->get_visible()) continue;
      line->set_child_visible(line == fit);
      int minimum = 0, natural = 0, ignored = 0;
      line->measure(Gtk::Orientation::HORIZONTAL, -1, minimum, natural, ignored, ignored);
      // the line that shows takes the width; the others, unmapped, their own
      const int lineWidth = line == fit ? std::max(width, fitWidth) : natural;
      line->size_allocate(Gtk::Allocation(0, 0, lineWidth, height), baseline);
    }
  }

 private:
  std::vector<Gtk::Widget*> lines_;
};

SlowInternetPill::SlowInternetPill() {
  // One element to assistive tech: a status readout, named by what it says.
  // The role can only be set before the accessible state is first touched.
  g_object_set(gobj(), "accessible-role", GTK_ACCESSIBLE_ROLE_STATUS, nullptr);
  EnsureDrawerCss();  // .ur-amber
  EnsurePillCss();
  set_valign(Gtk::Align::CENTER);
  // nothing to show until a reading says so
  set_visible(false);
  set_opacity(0.0);
  // the room in a new header at once, and after each layout while on screen
  property_parent().signal_changed().connect([this] { room_ = HeaderRoom(); });
  signal_map().connect([this] {
    layoutHook_.disconnect();
    if (Gtk::Native* native = get_native()) {
      layoutHook_ = native->get_surface()->signal_layout().connect(
          [this](int, int) { CheckRoom(); });
    }
  });
  signal_unmap().connect([this] { layoutHook_.disconnect(); });

  frame_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  frame_->add_css_class("ur-slow-pill");
  kit::MarkDecorative(*frame_);
  frame_->set_parent(*this);

  // the tunnel, and the recovered check over it
  auto* icon = Gtk::make_managed<Gtk::Overlay>();
  icon->set_valign(Gtk::Align::CENTER);
  tunnel_ = Gtk::make_managed<TunnelIndicator>(18);
  tunnel_->add_css_class("ur-amber");
  icon->set_child(*tunnel_);
  check_ = Gtk::make_managed<Gtk::Image>();
  check_->set_from_icon_name("object-select-symbolic");
  check_->add_css_class("ur-slow-pill-check");
  check_->set_halign(Gtk::Align::CENTER);
  check_->set_valign(Gtk::Align::CENTER);
  check_->set_opacity(0.0);
  icon->add_overlay(*check_);
  frame_->append(*icon);

  // the copy: the whole line, the worse direction alone, the label alone, and
  // the recovered words, which may be cut short rather than widen the pill
  lines_ = Gtk::make_managed<Lines>();
  lines_->set_valign(Gtk::Align::CENTER);
  full_ = MakeLine();
  worseOnly_ = MakeLine();
  labelOnly_ = MakeLine();
  for (Line* line : {&full_, &worseOnly_, &labelOnly_}) lines_->Append(*line->root);
  recovered_ = Gtk::make_managed<Gtk::Label>(Text(slow_internet::RecoveredCopy()));
  recovered_->add_css_class("ur-slow-pill-recovered");
  recovered_->set_xalign(0);
  recovered_->set_ellipsize(Pango::EllipsizeMode::END);
  recovered_->set_visible(false);
  lines_->Append(*recovered_);
  frame_->append(*lines_);
}

SlowInternetPill::~SlowInternetPill() {
  // the hold's timeout, the resize's idle and the layout hook call back into
  // the pill
  hold_.disconnect();
  roomResize_.disconnect();
  layoutHook_.disconnect();
  if (frame_) frame_->unparent();
}

SlowInternetPill::Line SlowInternetPill::MakeLine() {
  Line line;
  line.root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  line.label = Gtk::make_managed<Gtk::Label>();
  line.label->add_css_class("ur-slow-pill-label");
  line.root->append(*line.label);
  const auto number = [&line](const char* arrow) {
    Number out;
    out.root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    auto* glyph = Gtk::make_managed<Gtk::Label>(arrow);
    glyph->add_css_class("ur-slow-pill-arrow");
    out.root->append(*glyph);
    out.value = Gtk::make_managed<Gtk::Label>();
    out.value->add_css_class("ur-slow-pill-number");
    out.root->append(*out.value);
    line.root->append(*out.root);
    return out;
  };
  line.up = number("↑");
  line.down = number("↓");
  return line;
}

void SlowInternetPill::FillLine(Line& line, const slow_internet::PillContent& content) {
  line.label->set_text(Text(slow_internet::PillLabelCopy(content.fallback)));
  const auto fill = [](Number& number, const std::string& text, bool worse) {
    number.value->set_text(text);
    number.root->set_visible(!text.empty());
    if (worse) {
      number.root->add_css_class("ur-slow-pill-worse");
    } else {
      number.root->remove_css_class("ur-slow-pill-worse");
    }
  };
  fill(line.up, content.up, content.worse == slow_internet::Direction::Up);
  fill(line.down, content.down, content.worse == slow_internet::Direction::Down);
}

void SlowInternetPill::measure_vfunc(Gtk::Orientation orientation, int, int& minimum,
                                     int& natural, int& minimum_baseline,
                                     int& natural_baseline) const {
  frame_->measure(orientation, -1, minimum, natural, minimum_baseline, natural_baseline);
  if (orientation != Gtk::Orientation::HORIZONTAL) return;
  int linesMinimum = 0, linesNatural = 0, ignored = 0;
  lines_->measure(orientation, -1, linesMinimum, linesNatural, ignored, ignored);
  // the frame's own width around the copy: its border, its padding, the
  // tunnel and the gap after it
  const int chrome = natural - linesNatural;
  natural = room_ < 0 ? minimum
                      : std::clamp(chrome + lines_->Fit(room_ - chrome).second, minimum, natural);
}

void SlowInternetPill::size_allocate_vfunc(int width, int height, int baseline) {
  int frameMinimum = 0, frameNatural = 0, linesMinimum = 0, linesNatural = 0, ignored = 0;
  frame_->measure(Gtk::Orientation::HORIZONTAL, -1, frameMinimum, frameNatural, ignored, ignored);
  lines_->measure(Gtk::Orientation::HORIZONTAL, -1, linesMinimum, linesNatural, ignored, ignored);
  const int chrome = frameNatural - linesNatural;
  const int frameWidth = std::max(frameMinimum, chrome + lines_->Fit(width - chrome).second);
  frame_->size_allocate(Gtk::Allocation(width - frameWidth, 0, frameWidth, height), baseline);
}

void SlowInternetPill::CheckRoom() {
  const int room = HeaderRoom();
  if (room == room_) return;
  room_ = room;
  if (roomResize_.connected()) return;
  roomResize_ = Glib::signal_idle().connect([this]() -> bool {
    // a one-shot that removes itself by returning false: its handle is
    // dropped, never disconnected from inside its own slot
    roomResize_ = sigc::connection();
    queue_resize();
    return false;
  });
}

int SlowInternetPill::HeaderRoom() const {
  const auto* header = dynamic_cast<const Gtk::Box*>(get_parent());
  if (header == nullptr) return -1;
  int room = header->get_width();
  for (const Gtk::Widget* child = header->get_first_child(); child != nullptr;
       child = child->get_next_sibling()) {
    if (child == this || !child->get_visible()) continue;
    int minimum = 0, natural = 0, ignored = 0;
    child->measure(Gtk::Orientation::HORIZONTAL, -1, minimum, natural, ignored, ignored);
    // the other, and the gap between it and the next
    room -= natural + header->get_spacing();
  }
  return std::max(0, room);
}

bool SlowInternetPill::Animates() const {
  // motion only while the page is on screen: a hidden window takes the end
  // state, and so does a desktop with animations off
  const Gtk::Widget* parent = get_parent();
  return motion::ShouldAnimate() && parent != nullptr && parent->get_mapped();
}

void SlowInternetPill::Apply(const slow_internet::Reading& reading) {
  if (applied_ && reading.SameAs(reading_)) return;
  applied_ = true;
  reading_ = reading;
  const bool animate = Animates();
  const std::optional<slow_internet::PillContent> previous = shown_;
  shown_ = slow_internet::PillContent::Next(shown_, reading);
  if (!shown_) {
    // once: a pill already leaving keeps going
    if (previous) Leave(animate);
    return;
  }
  if (shown_->recovered) {
    // a later normal reading holds it
    const bool wasRecovered = previous && previous->recovered;
    if (!wasRecovered) Recover(animate);
    return;
  }
  // slow, also in the recovered state's place; a pill coming up shows its
  // words at once
  hold_.disconnect();
  Draw(*shown_, animate && up_);
  tunnel_->SetDepth(slow_internet::Depth(reading), animate);
  if (!up_) Appear(animate);
}

void SlowInternetPill::Draw(const slow_internet::PillContent& content, bool animate) {
  const bool swap = content.recovered != content_.recovered;
  content_ = content;
  if (!content.recovered) {
    FillLine(full_, content);
    FillLine(worseOnly_, content.WorseOnly());
    FillLine(labelOnly_, content.LabelOnly());
  }
  for (Line* line : {&full_, &worseOnly_, &labelOnly_}) line->root->set_visible(!content.recovered);
  recovered_->set_visible(content.recovered);
  kit::SetAccessibleLabel(*this, slow_internet::PillAccessibleName(content, Text));

  // only the numbers changed: nothing moves
  if (animate && !swap) return;
  const uint64_t generation = ++swapGeneration_;
  const double toCheck = content.recovered ? 1.0 : 0.0;
  if (!animate) {
    check_->set_opacity(toCheck);
    lines_->set_opacity(1.0);
    return;
  }
  const double fromCheck = check_->get_opacity();
  lines_->set_opacity(0.0);
  motion::AnimateValue(*this, 0, content.recovered ? tunnel::kFadeInMs : tunnel::kFadeOutMs,
                       motion::kStandardP1, motion::kStandardP2,
                       [this, generation, fromCheck, toCheck](double eased) {
                         if (generation != swapGeneration_) return;
                         check_->set_opacity(fromCheck + (toCheck - fromCheck) * eased);
                         lines_->set_opacity(eased);
                       });
}

void SlowInternetPill::Appear(bool animate) {
  up_ = true;
  const uint64_t generation = ++fadeGeneration_;
  // the room as the header stands, so its first frame asks for the right line
  room_ = HeaderRoom();
  set_visible(true);
  if (!animate) {
    set_opacity(1.0);
    return;
  }
  const double from = get_opacity();
  motion::AnimateValue(*this, 0, tunnel::kFadeInMs, motion::kStandardP1, motion::kStandardP2,
                       [this, generation, from](double eased) {
                         if (generation != fadeGeneration_) return;
                         set_opacity(from + (1.0 - from) * eased);
                       });
}

void SlowInternetPill::Recover(bool animate) {
  hold_.disconnect();
  Draw(*shown_, animate);
  tunnel_->SetDepth(0, animate);
  // the notification's 3 s, on a timeout rather than the frame clock, so a
  // hidden window ends it too
  hold_ = Glib::signal_timeout().connect(
      [this]() -> bool {
        // a one-shot that removes itself by returning false: its handle is
        // dropped, never disconnected from inside its own slot
        hold_ = sigc::connection();
        shown_.reset();
        FadeOut(0, Animates());
        return false;
      },
      static_cast<unsigned>(slow_internet::kRecoveredMillis));
}

void SlowInternetPill::Leave(bool animate) {
  hold_.disconnect();
  const int exitDelayMs = tunnel::ContainerFadeOutDelayMs(tunnel_->depth());
  tunnel_->SetDepth(0, animate);
  FadeOut(exitDelayMs, animate);
}

void SlowInternetPill::FadeOut(int delayMs, bool animate) {
  up_ = false;
  const uint64_t generation = ++fadeGeneration_;
  if (!animate) {
    set_opacity(0.0);
    set_visible(false);
    return;
  }
  const double from = get_opacity();
  motion::AnimateValue(
      *this, delayMs, tunnel::kFadeOutMs, motion::kStandardP1, motion::kStandardP2,
      [this, generation, from](double eased) {
        if (generation != fadeGeneration_) return;
        set_opacity(from * (1.0 - eased));
      },
      [this, generation] {
        if (generation != fadeGeneration_) return;
        set_visible(false);
      });
}

}  // namespace urnw
