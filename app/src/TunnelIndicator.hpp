// The slow-internet tunnel, drawn (TunnelGeometry.hpp): six nested square
// outlines, all kept, each with its own visibility, so a change fades squares
// in or out in place and never inserts or removes anything. The colour is the
// widget's CSS colour; the callers give it .ur-amber (#F5C242).
//
// Motion runs through UrMotion. With animations off (motion::ShouldAnimate)
// a change lands at once, as every gated path in the app does.
//
// Decorative: the pill around it says what it means.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <array>
#include <cstdint>

#include <gtkmm.h>

#include "TunnelGeometry.hpp"

namespace urnw {

class TunnelIndicator : public Gtk::Widget {
 public:
  explicit TunnelIndicator(int sizePx);

  // Shows `depth` squares (0 to tunnel::kMaxDepth). `animate` runs the
  // staggered fade (tunnel::Plan); otherwise, or with animations off, the
  // squares land at once.
  void SetDepth(int depth, bool animate);
  int depth() const { return depth_; }

 protected:
  void measure_vfunc(Gtk::Orientation orientation, int for_size, int& minimum, int& natural,
                     int& minimum_baseline, int& natural_baseline) const override;
  void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;

 private:
  // Every square's pose at once, cancelling the fades in flight.
  void Settle();

  int size_;
  int depth_ = 0;
  // per square: how visible (0..1) and its scale about the vanishing point
  // (1 when settled, tunnel::kEntryScale while hidden)
  std::array<double, tunnel::kMaxDepth> visible_{};
  std::array<double, tunnel::kMaxDepth> scale_{};
  // per square, bumped by every change that fades it: a fade in flight from
  // an earlier change stops writing
  std::array<uint64_t, tunnel::kMaxDepth> generation_{};
};

}  // namespace urnw
