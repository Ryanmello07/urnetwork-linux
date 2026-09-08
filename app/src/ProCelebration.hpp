// The Pro celebration (android ProFlightClock.kt / ProSunglassesFlight.kt): a
// 15 s confetti stream of pixel sunglasses, eye covers and face discs racing
// left to right over the whole window, while the content under them is
// pixelated — the mosaic fades in over the first 5 s, holds while the
// confetti flies, and fades out over the 5 s after the last sprite has left,
// so the window is sharp again at 20 s. One clock drives both; the overlay
// takes no input; nothing plays when animations are off.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <gtkmm.h>

namespace urnw {

// The flight timeline, in seconds.
inline constexpr double kProFlightTotalSeconds = 20.0;
inline constexpr double kProFlightConfettiSeconds = 15.0;
inline constexpr double kProFlightPixelateInSeconds = 5.0;
inline constexpr double kProFlightPixelateOutStartSeconds = 15.0;
inline constexpr double kProFlightPixelateOutSeconds = 5.0;
// the mosaic's largest cell, in px
inline constexpr double kProFlightPixelateMaxCell = 24.0;

// The clock of one flight: started by Start(), read by the overlay and the
// mosaic container every frame. Sequence 0 is idle.
class ProFlightClock {
 public:
  void Start();
  void Stop();
  bool Active() const { return active_; }
  // seconds since the flight started (0 when idle)
  double Seconds() const;
  uint64_t Sequence() const { return sequence_; }
  // the mosaic cell size for the current moment: ramp in, hold, ramp out
  double CellPx() const;

 private:
  bool active_ = false;
  uint64_t sequence_ = 0;
  gint64 startUs_ = 0;
};

// One sprite of the burst, fixed for the whole flight.
struct ProFlightSprite {
  enum class Kind { Sunglasses, EyeCover, FaceCover };
  Kind kind = Kind::Sunglasses;
  double delaySeconds = 0;
  double crossingSeconds = 1.5;
  double lane = 0.5;  // fraction of the height
  double bobAmplitude = 24;  // px
  double bobCycles = 3;
  double phaseOffset = 0;
  double scale = 1;
  bool behind = false;
};

// The whole take-off schedule of one flight, computed once at launch.
std::vector<ProFlightSprite> ProFlightBurst(uint64_t seed);

// The confetti overlay: a full-window drawing area above everything that
// paints the sprites of the current frame with Cairo. It never takes input.
class ProCelebrationOverlay : public Gtk::DrawingArea {
 public:
  explicit ProCelebrationOverlay(ProFlightClock& clock);
  ~ProCelebrationOverlay() override;

  // Starts a flight (a new seeded burst). Does nothing when animations are
  // off, or while a flight is already in the air.
  void Launch();
  // A widget to redraw on every frame of the flight (the mosaic container).
  std::function<void()> on_frame;

 private:
  void Draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
  void Finish();

  ProFlightClock& clock_;
  std::vector<ProFlightSprite> sprites_;
  guint tick_ = 0;
};

// A single-child container that draws its child pixelated while the flight
// clock says so: the child is rendered to a texture at 1/cell resolution
// and drawn back scaled up with nearest-neighbour filtering (the mosaic). No
// texture is made when the cell is 1px, so the container costs nothing when
// idle. The child keeps receiving input as usual.
class PixelateBin : public Gtk::Widget {
 public:
  explicit PixelateBin(ProFlightClock& clock);
  ~PixelateBin() override;

  void SetChild(Gtk::Widget& child);

 protected:
  void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;

 private:
  ProFlightClock& clock_;
  Gtk::Widget* child_ = nullptr;
};

}  // namespace urnw
