// SPDX-License-Identifier: MPL-2.0
#include "ExtenderShareSheet.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <vector>

#include <glib.h>

#include "ConnectorShape.hpp"
#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

#include <qrcodegen.hpp>

namespace urnw {
namespace {

// The quiet zone the QR spec requires, in MODULES on each side.
constexpr int kQuietModules = 4;
// Device pixels per module the canvas aims for, and the bounds it is clamped
// between. A 48-address share WITH settings is ~1300 bytes, which at level H
// is a version-40-ish code of ~177 modules: 3 px each would want 555 px, so
// the cap decides and the code draws at 2 px a module. A four-address share is
// 41 modules and takes the floor instead.
constexpr int kTargetPxPerModule = 3;
constexpr int kMinCodeSide = 240;
constexpr int kMaxCodeSide = 420;
// The centred glyph, as a fraction of the drawn code. Level H tolerates about
// 30 % erasure; this covers about 5 % of the area, and a round trip through a
// decoder was measured at this fraction for both a 41-module and a 173-module
// code.
constexpr double kGlyphFraction = 0.22;
// K7's "4 px outline of the connector shape around it".
constexpr double kGlyphOutline = 4.0;

// The code is drawn in ink on paper REGARDLESS of the app's dark theme: a
// scanner reads dark-on-light, and an inverted code is a code that does not
// scan on most phones.
constexpr Rgba kPaper{1.0, 1.0, 1.0, 1.0};
constexpr Rgba kInk{0.0, 0.0, 0.0, 1.0};

// Encodes `text` at level H into a row-major module bitmap. False (and an
// empty bitmap) when the payload cannot be encoded at all -- too long for
// version 40-H, which a 48-address share WITH a settings block can reach. That
// is not fatal to the screen: the payload is still on it as copyable text,
// which is exactly why K7 asks for both.
bool EncodeShareCode(const std::string& text, std::vector<bool>* modules, int* count) {
  modules->clear();
  *count = 0;
  if (text.empty()) return false;
  try {
    const qrcodegen::QrCode qr =
        qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::HIGH);
    const int size = qr.getSize();
    modules->resize(static_cast<size_t>(size) * static_cast<size_t>(size));
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        (*modules)[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] =
            qr.getModule(x, y);
      }
    }
    *count = size;
    return true;
  } catch (const std::exception& e) {
    g_warning("extender share: the payload does not fit in a QR code (%zu bytes): %s",
              text.size(), e.what());
    return false;
  }
}

// The canvas side this code wants, in px.
int CodeCanvasSide(int moduleCount) {
  if (moduleCount <= 0) return kMinCodeSide;
  const int total = moduleCount + 2 * kQuietModules;
  return std::clamp(total * kTargetPxPerModule, kMinCodeSide, kMaxCodeSide);
}

// Draws the cached bitmap into a `side` square with the connector glyph centred.
//
// THE MODULE SIZE IS AN INTEGER NUMBER OF PIXELS, and that is load-bearing
// rather than tidiness. Drawn at a fractional module size every module edge
// lands mid-pixel and cairo antialiases it; a dense code (173 modules in a
// 300 px box) then greys out into something no decoder can read -- measured:
// a 1185-byte payload rendered at 1.66 px/module failed to decode at all,
// and the same payload at a snapped 1 px/module round-tripped. So the unit is
// floored and the whole code is centred in whatever space is left over.
void DrawShareCode(const Cairo::RefPtr<Cairo::Context>& cr, const std::vector<bool>& modules,
                   int moduleCount, double side) {
  if (moduleCount <= 0 || side <= 0) return;
  const int total = moduleCount + 2 * kQuietModules;
  const int unit = static_cast<int>(side) / total;

  cr->set_source_rgba(kPaper.r, kPaper.g, kPaper.b, kPaper.a);
  cr->rectangle(0, 0, side, side);
  cr->fill();
  if (unit < 1) return;  // no room at all: the copyable text carries the share

  const double drawn = static_cast<double>(unit) * total;
  const double origin = std::floor((side - drawn) / 2.0);

  cr->set_source_rgba(kInk.r, kInk.g, kInk.b, kInk.a);
  for (int y = 0; y < moduleCount; ++y) {
    for (int x = 0; x < moduleCount; ++x) {
      if (!modules[static_cast<size_t>(y) * static_cast<size_t>(moduleCount) +
                   static_cast<size_t>(x)]) {
        continue;
      }
      cr->rectangle(origin + (x + kQuietModules) * unit, origin + (y + kQuietModules) * unit,
                    unit, unit);
    }
  }
  cr->fill();

  // the centred connector: a connector-shaped paper plate with a 4 px outline,
  // then the glyph itself in ink
  const double glyph = drawn * kGlyphFraction;
  const double plate = glyph + 2 * kGlyphOutline;
  const double plateOrigin = origin + (drawn - plate) / 2.0;
  cr->set_source_rgba(kPaper.r, kPaper.g, kPaper.b, kPaper.a);
  AddConnectorPath(cr, plateOrigin, plateOrigin, plate);
  cr->fill_preserve();
  cr->set_line_width(kGlyphOutline);
  cr->stroke();
  const double glyphOrigin = origin + (drawn - glyph) / 2.0;
  cr->set_source_rgba(kInk.r, kInk.g, kInk.b, kInk.a);
  AddConnectorPath(cr, glyphOrigin, glyphOrigin, glyph);
  cr->fill();
}

}  // namespace

ExtenderShareSheet::ExtenderShareSheet(Gtk::Window& parent, SdkHost& host) : host_(host) {
  set_transient_for(parent);
  set_modal(true);
  set_title(T_("share_extenders", "Share extenders"));
  set_default_size(420, -1);
  set_resizable(false);
  add_css_class("ur-sheet");

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->set_margin(24);

  auto* heading = Gtk::make_managed<Gtk::Label>(T_("share_extenders", "Share extenders"));
  heading->add_css_class("ur-step-heading");
  heading->set_xalign(0);
  box->append(*heading);

  auto* hint = Gtk::make_managed<Gtk::Label>(
      T_("share_extenders_hint",
         "Scan this code with another URnetwork app to share these extenders."));
  hint->add_css_class("dim-label");
  hint->set_xalign(0);
  hint->set_wrap(true);
  box->append(*hint);

  code_ = Gtk::make_managed<Gtk::DrawingArea>();
  code_->set_content_width(kMinCodeSide);
  code_->set_content_height(kMinCodeSide);
  code_->set_halign(Gtk::Align::CENTER);
  code_->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    DrawShareCode(cr, codeModules_, moduleCount_, std::min(w, h));
  });
  // the code says nothing to a screen reader; the count and the payload do
  kit::MarkDecorative(*code_);
  box->append(*code_);

  // Shown INSTEAD of the code when there is nothing to encode, so the sheet is
  // never a blank square the user has to interpret.
  unavailable_ = kit::MakePaneEmptyLine({});
  unavailable_->set_visible(false);
  box->append(*unavailable_);

  count_ = Gtk::make_managed<Gtk::Label>();
  count_->set_xalign(0);
  box->append(*count_);

  // "Include extender settings", off by default (K7)
  auto settingsRow = kit::MakePaneTwoLineRow(
      T_("include_extender_settings", "Include extender settings"));
  includeSettings_ = Gtk::make_managed<Gtk::Switch>();
  includeSettings_->set_valign(Gtk::Align::CENTER);
  includeSettings_->set_active(false);
  kit::SetAccessibleLabel(*includeSettings_,
                          T_("include_extender_settings", "Include extender settings"));
  settingsRow.trailing->append(*includeSettings_);
  includeSettings_->property_active().signal_changed().connect([this] {
    if (!rebuilding_) Rebuild();
  });
  box->append(*settingsRow.root);

  // the payload as text: the fallback that works when a camera does not
  payload_ = Gtk::make_managed<Gtk::TextView>();
  payload_->set_editable(false);
  payload_->set_wrap_mode(Gtk::WrapMode::CHAR);
  payload_->set_monospace(true);
  payload_->add_css_class("ur-card-bordered");
  auto* payloadScroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  payloadScroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  payloadScroll->set_min_content_height(64);
  payloadScroll->set_child(*payload_);
  box->append(*payloadScroll);

  auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  actions->set_halign(Gtk::Align::END);
  copy_ = Gtk::make_managed<Gtk::Button>(T_("copy_share_text", "Copy share text"));
  copy_->add_css_class("ur-btn");
  copy_->add_css_class("ur-btn-secondary");
  copy_->signal_clicked().connect([this] {
    if (share_.text.empty()) return;  // the button is insensitive anyway
    get_clipboard()->set_text(share_.text);
  });
  actions->append(*copy_);
  auto* close = Gtk::make_managed<Gtk::Button>(T_("close", "Close"));
  close->add_css_class("ur-btn");
  close->signal_clicked().connect([this] { set_visible(false); });
  actions->append(*close);
  box->append(*actions);

  set_child(*box);
  Rebuild();
}

void ExtenderShareSheet::Rebuild() {
  const auto result = host_.BuildExtenderShare(includeSettings_->get_active());
  share_ = extender::SharePresentationFor(result.has_value(),
                                          result ? result->Text : std::string(),
                                          result ? result->Count : 0,
                                          result ? result->IncludesSettings : false);

  // No result at all means no device -- the tunnel is down and the SDK has no
  // directory to share. That is a different statement from "zero extenders",
  // and both of them have to be readable rather than blank.
  // Encoded ONCE per payload, here rather than in the draw function, and the
  // canvas is then sized from the module count so a dense code still gets
  // whole pixels per module.
  const bool encoded =
      share_.canRenderCode && EncodeShareCode(share_.text, &codeModules_, &moduleCount_);
  const int canvasSide = CodeCanvasSide(moduleCount_);
  code_->set_content_width(canvasSide);
  code_->set_content_height(canvasSide);

  // Three distinct readings, none of them a blank square: no answer at all
  // (no device -- the section's buttons are meant to be disabled before this
  // can happen, so it is the defensive path), an answer with nothing to encode
  // (the count says "0 extenders", which is the whole truth), and a payload.
  code_->set_visible(encoded);
  unavailable_->set_visible(!share_.ready);
  if (!share_.ready) unavailable_->set_text(T_("something_went_wrong", "Something went wrong."));
  code_->queue_draw();

  count_->set_text(Format(TN_("share_extenders_count", "{} extender", "{} extenders",
                              static_cast<unsigned long>(share_.count)),
                          share_.count));
  count_->set_visible(share_.ready);

  payload_->get_buffer()->set_text(share_.text);
  payload_->set_visible(share_.canCopy);
  copy_->set_sensitive(share_.canCopy);
  // A share whose settings the SDK declined to include must not leave the
  // switch claiming they are in the code. Guarded, because writing the switch
  // fires the handler that called this.
  if (share_.ready && includeSettings_->get_active() != share_.includesSettings) {
    rebuilding_ = true;
    includeSettings_->set_active(share_.includesSettings);
    rebuilding_ = false;
  }
}

}  // namespace urnw
