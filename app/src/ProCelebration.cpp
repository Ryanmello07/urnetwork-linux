// SPDX-License-Identifier: MPL-2.0
#include "ProCelebration.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include <gtk/gtk.h>

#include "Ui.hpp"
#include "UrMotion.hpp"

namespace urnw {
namespace {

constexpr double kPi = 3.14159265358979323846;

// a steady stream: a new sprite about every 150 ms (with a little jitter)
// until the last one can still leave the window before the confetti ends
constexpr double kSpawnIntervalSeconds = 0.15;
constexpr double kSpawnJitterSeconds = 0.03;
// time to cross the window, fast to slow
constexpr double kMinCrossingSeconds = 1.2;
constexpr double kMaxCrossingSeconds = 1.9;
// the most sprites in the air at once; the schedule skips a take-off that
// would exceed it (the interval and crossing times keep it near a dozen)
constexpr int kMaxLiveSprites = 30;
constexpr double kPitchDegrees = 20;
constexpr int kTrailCount = 2;
constexpr double kTrailStepPx = 18;

// The Compose FastOutSlowIn curve (a cubic bezier 0.4,0 / 0.2,1), sampled by
// Newton iteration on the x axis: the sprites' horizontal ease and the
// mosaic ramps use it so the port moves exactly like android.
double FastOutSlowIn(double x) {
  x = std::clamp(x, 0.0, 1.0);
  constexpr double x1 = 0.4, y1 = 0.0, x2 = 0.2, y2 = 1.0;
  double t = x;
  for (int i = 0; i < 8; ++i) {
    const double mt = 1 - t;
    const double bx = 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t * t * t;
    const double dbx = 3 * mt * mt * x1 + 6 * mt * t * (x2 - x1) + 3 * t * t * (1 - x2);
    if (dbx < 1e-6) break;
    t -= (bx - x) / dbx;
    t = std::clamp(t, 0.0, 1.0);
  }
  const double mt = 1 - t;
  return 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t;
}

// ---- the pixel sprites (android pro_flight_sunglasses / pixel_eye_cover /
// pixel_face_cover), in cells of the 12-unit grid ----------------------------

struct Cell {
  int x, y;
};

// the sunglasses outline: 22 x 5 cells, the privacy-glasses staircase
const Cell kSunglassesOutline[] = {
    {0, 0},  {0, 2},  {1, 2},  {1, 3},  {2, 3},  {2, 4},  {3, 4},  {3, 5},  {8, 5},  {8, 4},
    {9, 4},  {9, 3},  {10, 3}, {10, 2}, {12, 2}, {12, 3}, {13, 3}, {13, 4}, {14, 4}, {14, 5},
    {19, 5}, {19, 4}, {20, 4}, {20, 3}, {21, 3}, {21, 2}, {22, 2}, {22, 0}};
// the black lens highlights
const Cell kSunglassesGlints[] = {{2, 1},  {4, 1},  {3, 2},  {5, 2},  {4, 3},  {6, 3},
                                  {13, 1}, {15, 1}, {14, 2}, {16, 2}, {15, 3}, {17, 3}};
// the square eye cover: a 22 x 8 censor bar with the same stepped corners
const Cell kEyeCoverOutline[] = {{2, 0},  {20, 0}, {20, 1}, {21, 1}, {21, 2}, {22, 2}, {22, 6},
                                 {21, 6}, {21, 7}, {20, 7}, {20, 8}, {2, 8},  {2, 7},  {1, 7},
                                 {1, 6},  {0, 6},  {0, 2},  {1, 2},  {1, 1},  {2, 1}};
const Cell kEyeCoverGlints[] = {{3, 1}, {18, 6}};
// the circular face cover: a 12 x 12 stepped disc
const Cell kFaceCoverOutline[] = {{4, 0},  {8, 0},  {8, 1},  {10, 1}, {10, 2}, {11, 2}, {11, 4},
                                  {12, 4}, {12, 8}, {11, 8}, {11, 10}, {10, 10}, {10, 11}, {8, 11},
                                  {8, 12}, {4, 12}, {4, 11}, {2, 11}, {2, 10}, {1, 10}, {1, 8},
                                  {0, 8},  {0, 4},  {1, 4},  {1, 2},  {2, 2},  {2, 1},  {4, 1}};
const Cell kFaceCoverGlints[] = {{3, 2}, {2, 3}};

constexpr Rgba kSpriteInk{0x10 / 255.0, 0x10 / 255.0, 0x10 / 255.0, 1.0};  // #101010

struct SpriteShape {
  const Cell* outline;
  int outlineCount;
  const Cell* glints;
  int glintCount;
  Rgba body;
  Rgba glint;
  double cellsWide;
  double cellsHigh;
  // the on-screen size at scale 1, in px (android: 96x22, 96x35, 64x64 dp)
  double widthPx;
  double heightPx;
};

const SpriteShape& ShapeOf(ProFlightSprite::Kind kind) {
  static const SpriteShape sunglasses{kSunglassesOutline, 28, kSunglassesGlints, 12, kUrPink,
                                      kSpriteInk, 22, 5, 96, 22};
  static const SpriteShape eyeCover{kEyeCoverOutline, 20, kEyeCoverGlints, 2, kSpriteInk,
                                    kUrPink, 22, 8, 96, 35};
  static const SpriteShape faceCover{kFaceCoverOutline, 28, kFaceCoverGlints, 2, kProGold,
                                     kSpriteInk, 12, 12, 64, 64};
  switch (kind) {
    case ProFlightSprite::Kind::Sunglasses: return sunglasses;
    case ProFlightSprite::Kind::EyeCover: return eyeCover;
    case ProFlightSprite::Kind::FaceCover: return faceCover;
  }
  return sunglasses;
}

// Paints one shape with its top-left at the current origin, `width` px wide.
void PaintShape(const Cairo::RefPtr<Cairo::Context>& cr, const SpriteShape& shape, double width,
                double height, double alpha) {
  const double cell = width / shape.cellsWide;
  (void)height;
  cr->begin_new_path();
  for (int i = 0; i < shape.outlineCount; ++i) {
    const double x = shape.outline[i].x * cell;
    const double y = shape.outline[i].y * cell;
    if (i == 0) cr->move_to(x, y);
    else cr->line_to(x, y);
  }
  cr->close_path();
  cr->set_source_rgba(shape.body.r, shape.body.g, shape.body.b, alpha);
  cr->fill();
  cr->set_source_rgba(shape.glint.r, shape.glint.g, shape.glint.b, alpha);
  for (int i = 0; i < shape.glintCount; ++i) {
    cr->rectangle(shape.glints[i].x * cell, shape.glints[i].y * cell, cell, cell);
  }
  cr->fill();
}

}  // namespace

// ---- the clock ------------------------------------------------------------

void ProFlightClock::Start() {
  active_ = true;
  ++sequence_;
  startUs_ = g_get_monotonic_time();
}

void ProFlightClock::Stop() { active_ = false; }

double ProFlightClock::Seconds() const {
  if (!active_) return 0;
  return (g_get_monotonic_time() - startUs_) / 1e6;
}

double ProFlightClock::CellPx() const {
  if (!active_) return 0;
  const double seconds = Seconds();
  const double outEnd = kProFlightPixelateOutStartSeconds + kProFlightPixelateOutSeconds;
  double ramp = 1;
  if (seconds < kProFlightPixelateInSeconds) {
    ramp = FastOutSlowIn(seconds / kProFlightPixelateInSeconds);
  } else if (seconds > kProFlightPixelateOutStartSeconds) {
    ramp = FastOutSlowIn(std::clamp((outEnd - seconds) / kProFlightPixelateOutSeconds, 0.0, 1.0));
  }
  return ramp * kProFlightPixelateMaxCell;
}

// ---- the burst ------------------------------------------------------------

std::vector<ProFlightSprite> ProFlightBurst(uint64_t seed) {
  std::mt19937_64 random(seed);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::vector<ProFlightSprite> sprites;
  double takeOff = 0;
  const double lastTakeOff = kProFlightConfettiSeconds - kMaxCrossingSeconds;
  while (takeOff <= lastTakeOff) {
    ProFlightSprite sprite;
    const int kind = static_cast<int>(unit(random) * 5);  // 40 / 40 / 20
    sprite.kind = kind < 2   ? ProFlightSprite::Kind::Sunglasses
                  : kind < 4 ? ProFlightSprite::Kind::EyeCover
                             : ProFlightSprite::Kind::FaceCover;
    sprite.delaySeconds = takeOff;
    sprite.crossingSeconds = kMinCrossingSeconds + unit(random) * (kMaxCrossingSeconds - kMinCrossingSeconds);
    sprite.lane = 0.08 + unit(random) * 0.84;
    sprite.bobAmplitude = 12 + std::floor(unit(random) * 29);
    sprite.bobCycles = 2 + unit(random) * 2;
    sprite.phaseOffset = unit(random) * 2 * kPi;
    sprite.scale = 0.6 + unit(random) * 0.6;
    // the small ones fly behind
    sprite.behind = sprite.scale < 0.85;
    const long live = std::count_if(sprites.begin(), sprites.end(), [takeOff](const ProFlightSprite& s) {
      return s.delaySeconds + s.crossingSeconds > takeOff;
    });
    if (live < kMaxLiveSprites) sprites.push_back(sprite);
    takeOff += kSpawnIntervalSeconds + (unit(random) * 2 - 1) * kSpawnJitterSeconds;
  }
  // behind first so the front layer draws over it
  std::stable_sort(sprites.begin(), sprites.end(),
                   [](const ProFlightSprite& a, const ProFlightSprite& b) { return a.behind && !b.behind; });
  return sprites;
}

// ---- the overlay ----------------------------------------------------------

ProCelebrationOverlay::ProCelebrationOverlay(ProFlightClock& clock) : clock_(clock) {
  // decoration only: never a target, never focusable
  set_can_target(false);
  set_can_focus(false);
  set_hexpand(true);
  set_vexpand(true);
  set_visible(false);
  gtk_accessible_update_state(GTK_ACCESSIBLE(gobj()), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
  set_draw_func(sigc::mem_fun(*this, &ProCelebrationOverlay::Draw));
}

ProCelebrationOverlay::~ProCelebrationOverlay() {
  if (tick_) remove_tick_callback(tick_);
}

void ProCelebrationOverlay::Launch() {
  if (!motion::ShouldAnimate()) return;
  if (clock_.Active()) return;
  clock_.Start();
  sprites_ = ProFlightBurst(clock_.Sequence() * 0x9E3779B97F4A7C15ULL + g_get_monotonic_time());
  set_visible(true);
  queue_draw();
  if (on_frame) on_frame();
  tick_ = add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
    if (clock_.Seconds() >= kProFlightTotalSeconds) {
      Finish();
      return false;
    }
    queue_draw();
    if (on_frame) on_frame();
    return true;
  });
}

void ProCelebrationOverlay::Finish() {
  tick_ = 0;
  clock_.Stop();
  sprites_.clear();
  set_visible(false);
  if (on_frame) on_frame();
}

void ProCelebrationOverlay::Draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
  if (!clock_.Active()) return;
  const double seconds = clock_.Seconds();
  for (const ProFlightSprite& sprite : sprites_) {
    const double local = (seconds - sprite.delaySeconds) / sprite.crossingSeconds;
    if (local <= 0 || local >= 1) continue;
    const double t = FastOutSlowIn(local);
    const SpriteShape& shape = ShapeOf(sprite.kind);
    const double spriteWidth = shape.widthPx * sprite.scale;
    const double travel = width + 2 * spriteWidth;
    const double laneY = height * sprite.lane;
    const double layerAlpha = sprite.behind ? 0.55 : 1.0;

    auto paint = [&](double progress, double alpha, double scale) {
      const double phase = 2 * kPi * sprite.bobCycles * progress + sprite.phaseOffset;
      const double x = -spriteWidth + progress * travel;
      // the vertical velocity sets the pitch: nose up while rising (y
      // decreasing on screen), nose down while falling
      const double y = laneY + sprite.bobAmplitude * std::sin(phase) - shape.heightPx * scale / 2;
      const double pitch = -kPitchDegrees * std::cos(phase) * kPi / 180;
      cr->save();
      cr->translate(x, y);
      cr->scale(scale, scale);
      cr->translate(shape.widthPx / 2, shape.heightPx / 2);
      cr->rotate(pitch);
      cr->translate(-shape.widthPx / 2, -shape.heightPx / 2);
      PaintShape(cr, shape, shape.widthPx, shape.heightPx, alpha);
      cr->restore();
    };

    // the trail: fading copies a step behind
    for (int i = kTrailCount; i >= 1; --i) {
      const double trailT = std::max(0.0, t - i * kTrailStepPx / travel);
      paint(trailT, layerAlpha * (0.28 - 0.1 * i), sprite.scale * (1 - 0.08 * i));
    }
    paint(t, layerAlpha, sprite.scale);
  }
}

// ---- the mosaic container -------------------------------------------------

PixelateBin::PixelateBin(ProFlightClock& clock) : clock_(clock) {
  set_layout_manager(Gtk::BinLayout::create());
  set_hexpand(true);
  set_vexpand(true);
}

PixelateBin::~PixelateBin() {
  if (child_) child_->unparent();
}

void PixelateBin::SetChild(Gtk::Widget& child) {
  if (child_) child_->unparent();
  child_ = &child;
  child.set_parent(*this);
}

void PixelateBin::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) {
  if (!child_) return;
  const double cell = clock_.Active() ? clock_.CellPx() : 0;
  const int width = get_width();
  const int height = get_height();
  if (cell <= 1 || width <= 0 || height <= 0) {
    snapshot_child(*child_, snapshot);
    return;
  }

  // Render the child at 1/cell resolution, then draw that small texture
  // back over the full bounds with nearest-neighbour filtering: each texel
  // becomes one cell of the mosaic. The renderer needs a realized native.
  GtkNative* native = gtk_widget_get_native(GTK_WIDGET(gobj()));
  GskRenderer* renderer = native ? gtk_native_get_renderer(native) : nullptr;
  if (!renderer) {
    snapshot_child(*child_, snapshot);
    return;
  }
  const double scale = 1.0 / cell;
  auto small = Gtk::Snapshot::create();
  small->scale(static_cast<float>(scale), static_cast<float>(scale));
  snapshot_child(*child_, small);
  GskRenderNode* node = gtk_snapshot_to_node(small->gobj());
  if (!node) return;
  const graphene_rect_t viewport = GRAPHENE_RECT_INIT(
      0.0f, 0.0f, static_cast<float>(std::ceil(width * scale)), static_cast<float>(std::ceil(height * scale)));
  GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &viewport);
  gsk_render_node_unref(node);
  if (!texture) {
    snapshot_child(*child_, snapshot);
    return;
  }
  const graphene_rect_t bounds =
      GRAPHENE_RECT_INIT(0.0f, 0.0f, static_cast<float>(viewport.size.width * cell),
                         static_cast<float>(viewport.size.height * cell));
#if GTK_CHECK_VERSION(4, 10, 0)
  gtk_snapshot_append_scaled_texture(snapshot->gobj(), texture, GSK_SCALING_FILTER_NEAREST, &bounds);
#else
  // older GTK cannot pick the filter: the upscale reads as a blur that follows
  // the same ramp, the nearest available approximation
  gtk_snapshot_append_texture(snapshot->gobj(), texture, &bounds);
#endif
  g_object_unref(texture);
}

}  // namespace urnw
