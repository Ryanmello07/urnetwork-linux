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
  // whole legend, in SDK terms — see the table in the .cpp for the hex).
  // Checked against the provider state machine itself, not just the parity
  // doc: connect/ip_remote_multi_client_monitor.go declares the five states
  // with IsTerminal()/IsActive(), and the SDK counts the provider window as
  // exactly the IsActive() points — which are exactly the Added ones. So green
  // means carrying, and nothing else does.
  //   "InEvaluation"     pale yellow  — offered, the SDK has not ruled on it
  //   "EvaluationFailed" coral        — evaluated and rejected (terminal)
  //   "NotAdded"         coral        — not in the window, not carrying (terminal)
  //   "Added"            green        — in the provider window, carrying traffic;
  //                                     the only state the SDK calls Active, and
  //                                     the only one it counts into the window
  //   "Removed"          transparent  — was Added, then left the window (terminal)
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
  //
  // THE DIMENSIONS ARE A HIGH-WATER MARK, not a per-push value — see the long
  // note over the definition. In one sentence: the SDK's grid side never
  // contracts, but SdkHost reads the side and the point list through separate
  // locks, so a snapshot can carry a stale/zero side beside fresh points, and
  // honouring such a push literally would hide dots the SDK really reported.
  void SetGrid(const std::vector<urnet::ProviderGridPoint>& points, int64_t gridWidth,
               int64_t gridHeight);

  // Diagnostics for the page (a dot layer that renders nothing looks exactly
  // like a grid that was never pushed — these tell those apart from outside,
  // without the canvas ever inventing a point to prove it is alive).
  //   point_count() live dots held.
  //   grid_cols()   the divisor the cells are actually laid out on — THE ONE
  //                 the draw uses, so the readout cannot disagree with the
  //                 pixels. 0 means "no layout: nothing can be drawn no matter
  //                 how many points are held".
  //   drawn_count() dots the layer issued on the last frame (the globe clip may
  //                 still trim one that sits past the rim). held > 0 &&
  //                 drawn == 0 IS the "held but invisible" state, readable from
  //                 outside without guessing: with cols == 0 there is no layout
  //                 to draw on, and with cols > 0 the dots are still growing in
  //                 and the tick clock has not reached them yet.
  size_t point_count() const { return dots_.size(); }
  int64_t grid_cols() const;
  size_t drawn_count() const { return drawnCount_; }
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
  // The cell divisor the dot layer lays out on: the session's high-water grid
  // side, widened to cover any point the SDK actually placed beyond it. 0 only
  // when no side has ever been reported this session.
  int64_t LayoutCols() const;
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
  // The grid side as the SDK has reported it SO FAR THIS SESSION (high-water:
  // the SDK's own side never contracts), reset with the points.
  int64_t gridWidth_ = 0, gridHeight_ = 0;
  // The largest cell index the SDK has actually placed a point on, +1. A point
  // at X proves the side is at least X+1, so this widens a stale reported side
  // to one that can hold the real points. Derived only from pushed points;
  // never a stand-in for a side that was never reported.
  int64_t pointExtent_ = 0;
  bool dotsAnimating_ = false;
  size_t drawnCount_ = 0;  // dots issued on the last frame (diagnostic)
  // Diagnostics are latched, never per-push: SetGrid is the ~10 fps feed path
  // and the conditions worth reporting persist across whole spells of it.
  bool noLayoutWarned_ = false;   // one warning per session
  bool freezeDropLogged_ = false;  // one line per frozen spell
  size_t loggedHeld_ = static_cast<size_t>(-1);  // last reported shape
  int64_t loggedCols_ = -1;
};

}  // namespace urnw
