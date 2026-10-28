// SPDX-License-Identifier: MPL-2.0
#include "TunnelIndicator.hpp"

#include <algorithm>
#include <cmath>

#include <gtk/gtk.h>

#include "UrMotion.hpp"

namespace urnw {

TunnelIndicator::TunnelIndicator(int sizePx) : size_(sizePx) {
  set_valign(Gtk::Align::CENTER);
  set_halign(Gtk::Align::CENTER);
  // a square settling from 1.06x reaches past the box for a moment
  set_overflow(Gtk::Overflow::VISIBLE);
  // the pill around it says what it means
  gtk_accessible_update_state(GTK_ACCESSIBLE(gobj()), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
  Settle();
}

void TunnelIndicator::measure_vfunc(Gtk::Orientation, int, int& minimum, int& natural,
                                    int& minimum_baseline, int& natural_baseline) const {
  minimum = natural = size_;
  minimum_baseline = natural_baseline = -1;
}

void TunnelIndicator::Settle() {
  for (int i = 0; i < tunnel::kMaxDepth; ++i) {
    ++generation_[i];
    const bool shown = i < depth_;
    visible_[i] = shown ? 1.0 : 0.0;
    scale_[i] = shown ? 1.0 : tunnel::kEntryScale;
  }
  queue_draw();
}

void TunnelIndicator::SetDepth(int depth, bool animate) {
  depth = std::clamp(depth, 0, tunnel::kMaxDepth);
  if (depth == depth_) return;
  const int from = depth_;
  depth_ = depth;
  // animations off: no motion at all, the final state at once
  if (!animate || !motion::ShouldAnimate()) {
    Settle();
    return;
  }
  // Each square that changes fades from where it is now, so a change that
  // lands mid-fade turns it around rather than jumping. One that has not
  // started yet holds its pose through its delay: the stagger.
  for (const tunnel::Step& step : tunnel::Plan(from, depth)) {
    const int i = step.index;
    const uint64_t generation = ++generation_[i];
    const double fromVisible = visible_[i];
    const double fromScale = scale_[i];
    const double toVisible = step.in ? 1.0 : 0.0;
    const double toScale = step.in ? 1.0 : tunnel::kEntryScale;
    motion::AnimateValue(*this, step.delayMs, step.durationMs, motion::kStandardP1,
                         motion::kStandardP2,
                         [this, i, generation, fromVisible, fromScale, toVisible,
                          toScale](double eased) {
                           if (generation_[i] != generation) return;
                           visible_[i] = fromVisible + (toVisible - fromVisible) * eased;
                           scale_[i] = fromScale + (toScale - fromScale) * eased;
                           queue_draw();
                         });
  }
}

void TunnelIndicator::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) {
  const double w = get_width();
  const double h = get_height();
  if (w <= 0 || h <= 0) return;
  const double size = std::min(w, h);
  // room for a square at its entry scale, which reaches past the box
  const double spill = std::ceil(size * (tunnel::kEntryScale - 1.0));
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(
      static_cast<float>(-spill), static_cast<float>(-spill), static_cast<float>(w + 2 * spill),
      static_cast<float>(h + 2 * spill));
  auto cr = snapshot->append_cairo(&bounds);

  GdkRGBA color;
  gtk_widget_get_color(GTK_WIDGET(gobj()), &color);
  cr->translate((w - size) / 2.0, (h - size) / 2.0);
  // square corners: rounded ones soften the tunnel
  cr->set_line_join(Cairo::Context::LineJoin::MITER);
  const double vanish = tunnel::VanishingPoint(size);
  for (int i = 0; i < tunnel::kMaxDepth; ++i) {
    if (visible_[i] <= 0.0) continue;
    const tunnel::Square square = tunnel::SquareAt(i, size);
    cr->save();
    // a square fading scales about the vanishing point, so it recedes into
    // the tunnel rather than growing from its own centre
    cr->translate(vanish, vanish);
    cr->scale(scale_[i], scale_[i]);
    cr->translate(-vanish, -vanish);
    cr->set_source_rgba(color.red, color.green, color.blue,
                        color.alpha * square.opacity * visible_[i]);
    cr->set_line_width(square.stroke);
    cr->rectangle(square.x, square.y, square.side, square.side);
    cr->stroke();
    cr->restore();
  }
}

}  // namespace urnw
