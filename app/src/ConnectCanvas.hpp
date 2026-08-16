// The connect hero canvas — the signature visual, a faithful GTK4/cairo port
// of windows:ConnectCanvas.{h,cpp} (itself iOS Main/Connect/ConnectButton).
// Spec: docs/parity/connect-canvas.md — every metric is written in iOS's
// 256pt canvas space and scaled by side/256.
//
// The canvas is DECORATIVE and non-interactive: click/focus/name live on the
// hero button that wraps it (ConnectPage). Five states: Disconnected (the
// electric-blue core + bounded pulse burst), Connecting (GlobeConnector
// lattice + the live provider grid), Connected (five 180pt brand circles
// sliding in), Error/Processing (a faint glyph, 500ms delayed).
//
// Motion budget (normative): repeating/one-shot motion rides an internal
// frame-clock callback that runs ONLY while something animates; the external
// Tick() (the page's shared ~10fps clock) advances grid-dot transitions only.
// gtk-enable-animations off = motion GONE, everything snaps.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <map>
#include <string>
#include <vector>

#include <gtkmm.h>

#include <urnetwork_sdk.hpp>

namespace urnw {

class ConnectCanvas : public Gtk::Widget {
 public:
  enum class State { Disconnected, Connecting, Connected, Error, Processing };

  ConnectCanvas();

  void SetState(State state);
  State state() const { return state_; }
  // The live provider grid (ignored unless state == Connecting — iOS freezes
  // the grid the instant the connection lands). Empty list = bare lattice.
  //
  // The dots are REAL SDK data or nothing: this is the only way a dot is ever
  // created, and the empty push is a normal reading (no session, rpc-only
  // session, a connection not carrying traffic yet) that renders as the bare
  // lattice. Nothing here invents a point. A preview/demo feed belongs in the
  // PAGE (docs/parity/connect-canvas.md §15), pushed through this same entry
  // point, so the shipped canvas has exactly one source of dots.
  //
  // What the colours MEAN (ProviderGridPoint::State → the dot's fill; the
  // whole legend, in SDK terms — see the table in the .cpp for the hex):
  //   "InEvaluation"     pale yellow  — offered, the SDK has not ruled on it
  //   "EvaluationFailed" coral        — evaluated and rejected
  //   "NotAdded"         coral        — not in the window, not carrying traffic
  //   "Added"            green        — in the provider window, carrying traffic
  //   "Removed"          transparent  — iOS's own extra case: it left the grid
  //   anything else      pale yellow  — never render an unaccepted provider as
  //                                     one the SDK has accepted
  //
  // Grow-in/colour-blend transitions are only armed when this canvas is
  // presenting AND OS animations are on — the page's shared ~10 fps clock,
  // the only thing that advances them, runs under exactly that same
  // condition (ConnectPage::UpdateClock drives Tick() and
  // SetPresentationActive from ONE boolean; do not split them). Off that
  // condition a pushed grid is drawn settled on the very next frame instead
  // of sitting at scale 0 forever.
  void SetGrid(const std::vector<urnet::ProviderGridPoint>& points, int64_t gridWidth,
               int64_t gridHeight);

  // Diagnostics for the page (a dot layer that renders nothing looks exactly
  // like a grid that was never pushed — these two tell those apart from
  // outside, without the canvas ever inventing a point to prove it is alive).
  // point_count() is live dots held; grid_cols() is the divisor the cells are
  // laid out on, max(gridWidth, gridHeight) — 0 means "no layout, nothing can
  // be drawn no matter how many points are held".
  size_t point_count() const { return dots_.size(); }
  int64_t grid_cols() const { return std::max<int64_t>(std::max(gridWidth_, gridHeight_), 0); }
  void SetHovered(bool hovered);
  void SetFocusRingVisible(bool visible);
  void SetPresentationActive(bool active);
  // the page's shared ~10fps tick: advances grid-dot color/size transitions
  void Tick();

 protected:
  void measure_vfunc(Gtk::Orientation orientation, int for_size, int& minimum, int& natural,
                     int& minimum_baseline, int& natural_baseline) const override;
  void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;

 private:
  // ProviderGridPoint::State, parsed. `Removed` is iOS's own extra case: a
  // point that has left the grid fades out over one transition rather than
  // vanishing between frames. Order is load-bearing — it indexes the colour
  // table in the .cpp.
  enum class PointState { InEvaluation, EvaluationFailed, NotAdded, Added, Removed };
  struct Dot {
    int32_t x = 0, y = 0;         // grid cell coords, as the SDK reports them
    PointState state = PointState::InEvaluation;
    PointState previous = PointState::InEvaluation;
    double colorProgress = 1.0;   // previous -> state blend
    double sizeProgress = 0.0;    // grow-in
    bool seen = false;            // marked during a SetGrid diff
    bool Animating() const { return colorProgress < 1.0 || sizeProgress < 1.0; }
  };

  // ---- animation clock ------------------------------------------------------
  void EnsureAnimClock();
  bool AnimStep(gint64 nowUs);  // returns whether anything is still animating
  static bool AnimationsEnabled();

  void StartIdlePulse();  // the bounded 3-burst pulse (Disconnected only)
  void RunBlobs(bool in);
  void ShuffleBlobs();
  void ClearPoints();
  static PointState ParsePointState(const std::string& value);
  // Every dot at its settled pose with nothing left to animate (Removed dots
  // are dropped — their whole transition WAS the fade-out). The dot layer's
  // half of the reduce-motion rule, and the "frozen" half of Connected.
  void SettleDots();

  void DrawCanvas(const Cairo::RefPtr<Cairo::Context>& cr, double width, double height);
  void AddGlobePath(const Cairo::RefPtr<Cairo::Context>& cr, double originX, double originY,
                    double side) const;

  State state_ = State::Disconnected;
  bool presenting_ = false;
  bool hovered_ = false;
  bool focusRing_ = false;

  // layer opacities (state crossfade, 500ms ease-in-out)
  double idleOpacity_ = 1.0, idleTarget_ = 1.0;
  double gridOpacity_ = 0.0, gridTarget_ = 0.0;
  double glyphOpacity_ = 0.0;
  bool glyphShown_ = false;
  gint64 glyphShownAtUs_ = 0;  // entrance: 500ms delay + 300ms fade
  gint64 fadeStartUs_ = -1;
  double idleFadeFrom_ = 1.0, gridFadeFrom_ = 0.0;

  // pulse burst: 3 cycles x 1500ms ease-out
  int pulseBurstsLeft_ = 0;
  gint64 pulseStartUs_ = -1;
  double pulseScale_ = 1.0, pulseOpacity_ = 0.0;

  // blob slide: 1000ms ease-in-out; progress 0=parked out, 1=settled in
  bool blobsIn_ = false;
  bool blobsVisible_ = false;
  double blobProgress_ = 0.0;
  gint64 blobStartUs_ = -1;
  double blobFrom_ = 0.0, blobTo_ = 0.0;
  std::vector<int> blobColorOrder_{0, 1, 2, 3, 4};
  std::vector<int> blobOffsetOrder_{0, 1, 2, 3, 4};
  std::mt19937 blobRng_{0x5EED0BE};  // constant seed: deterministic arrangements (parity)

  // hover lift: 180ms ease-out to 1.03
  double hoverScale_ = 1.0;
  gint64 hoverStartUs_ = -1;
  double hoverFrom_ = 1.0, hoverTo_ = 1.0;

  bool animClockActive_ = false;

  // the provider grid
  std::map<std::string, Dot> dots_;
  int64_t gridWidth_ = 0, gridHeight_ = 0;
  bool dotsAnimating_ = false;
};

}  // namespace urnw
