// SPDX-License-Identifier: MPL-2.0
#include "DemandChart.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <gtk/gtk.h>

#include "Ui.hpp"

namespace urnw {
namespace {

// the title row: TransferChart's title face (sans-serif, 11 px, muted) centred
// on y = 7, with the total right-aligned in the same row
constexpr double kTitleSize = 11;
constexpr double kTitleCenterY = 7;
constexpr int kTitleBand = 14;
// the caption under the title, small and muted, wrapped to the width
constexpr double kCaptionSize = 10;
constexpr int kCaptionGap = 2;
// the bars: as tall as the Blocked chart's row, over a 1 px baseline
constexpr int kPlotGap = 6;
constexpr int kPlotHeight = 66;
// the axis ends under the baseline
constexpr double kAxisSize = 9;
constexpr int kAxisGap = 3;
constexpr int kAxisBand = 12;
// the line in place of the bars, and the empty line over them
constexpr double kMessageSize = 11;
// the current minute is still filling, so its bar is drawn lighter
constexpr double kPartialMinuteAlpha = 0.5;

Pango::FontDescription Face(double sizePx) {
  Pango::FontDescription font;
  font.set_family("sans-serif");
  font.set_absolute_size(sizePx * PANGO_SCALE);
  return font;
}

void SetSource(const Cairo::RefPtr<Cairo::Context>& cr, const Rgba& c) {
  cr->set_source_rgba(c.r, c.g, c.b, c.a);
}

}  // namespace

DemandChart::DemandChart(Glib::ustring title, Glib::ustring caption, Glib::ustring axisStart,
                         Glib::ustring axisEnd)
    : title_(std::move(title)),
      caption_(std::move(caption)),
      axisStart_(std::move(axisStart)),
      axisEnd_(std::move(axisEnd)) {
  set_hexpand(true);
  set_draw_func(sigc::mem_fun(*this, &DemandChart::OnDraw));
  captionLayout_ = create_pango_layout(caption_);
  captionLayout_->set_font_description(Face(kCaptionSize));
  captionLayout_->set_wrap(Pango::WrapMode::WORD_CHAR);
  // an image named by its title (and what it shows), described by its caption
  gtk_accessible_update_property(GTK_ACCESSIBLE(gobj()), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                 caption_.c_str(), -1);
  UpdateAccessibleDescription();
}

void DemandChart::ShowMessage(const Glib::ustring& message) {
  message_ = message;
  total_.clear();
  emptyLine_.clear();
  fractions_.clear();
  UpdateAccessibleDescription();
  queue_draw();
}

void DemandChart::ShowBars(std::vector<double> fractions, const Glib::ustring& total,
                           const Glib::ustring& emptyLine) {
  message_.clear();
  fractions_ = std::move(fractions);
  total_ = total;
  emptyLine_ = emptyLine;
  UpdateAccessibleDescription();
  queue_draw();
}

// "Demand, 42 times in the last hour": the name re-announces with what the
// chart says now, the kit's "Label, value" (the caption is the description)
void DemandChart::UpdateAccessibleDescription() {
  Glib::ustring name = title_;
  const Glib::ustring& now = message_.empty() ? total_ : message_;
  if (!now.empty()) name += ", " + now;
  gtk_accessible_update_property(GTK_ACCESSIBLE(gobj()), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                 name.c_str(), -1);
}

Gtk::SizeRequestMode DemandChart::get_request_mode_vfunc() const {
  return Gtk::SizeRequestMode::HEIGHT_FOR_WIDTH;
}

void DemandChart::measure_vfunc(Gtk::Orientation orientation, int for_size, int& minimum,
                                int& natural, int& minimum_baseline,
                                int& natural_baseline) const {
  minimum_baseline = -1;
  natural_baseline = -1;
  if (orientation == Gtk::Orientation::HORIZONTAL) {
    // as wide as its row: the bars and the wrapped caption fit any width
    minimum = 0;
    natural = 0;
    return;
  }
  minimum = kTitleBand + kCaptionGap + CaptionHeight(for_size) + kPlotGap + kPlotHeight +
            kAxisGap + kAxisBand;
  natural = minimum;
}

int DemandChart::CaptionHeight(int width) const {
  // the widget's font settings may have changed since the layout was made
  captionLayout_->context_changed();
  captionLayout_->set_width(width > 0 ? width * PANGO_SCALE : -1);
  int layoutWidth = 0;
  int layoutHeight = 0;
  captionLayout_->get_pixel_size(layoutWidth, layoutHeight);
  return layoutHeight;
}

void DemandChart::OnDraw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
  (void)height;
  if (width <= 0) return;
  const double w = width;
  const Rgba muted = kUrTextMuted;

  // the title row: the total right-aligned, the title left of it, ellipsized
  // before it would run into the total
  int totalWidth = 0;
  int totalHeight = 0;
  Glib::RefPtr<Pango::Layout> total;
  if (message_.empty() && !total_.empty()) {
    total = create_pango_layout(total_);
    total->set_font_description(Face(kTitleSize));
    total->get_pixel_size(totalWidth, totalHeight);
  }
  auto title = create_pango_layout(title_);
  title->set_font_description(Face(kTitleSize));
  title->set_ellipsize(Pango::EllipsizeMode::END);
  title->set_width(std::max(1, width - (total ? totalWidth + 8 : 0)) * PANGO_SCALE);
  int titleWidth = 0;
  int titleHeight = 0;
  title->get_pixel_size(titleWidth, titleHeight);
  SetSource(cr, muted);
  cr->move_to(0, kTitleCenterY - titleHeight / 2.0);
  title->show_in_cairo_context(cr);
  if (total) {
    cr->move_to(w - totalWidth, kTitleCenterY - totalHeight / 2.0);
    total->show_in_cairo_context(cr);
  }

  // the caption, wrapped to the width (measured the same way)
  const double captionTop = kTitleBand + kCaptionGap;
  const int captionHeight = CaptionHeight(width);
  cr->move_to(0, captionTop);
  captionLayout_->show_in_cairo_context(cr);

  const double plotTop = captionTop + captionHeight + kPlotGap;
  const double plotBottom = plotTop + kPlotHeight;
  // one muted line centred over the bars' area, wrapped to the width
  auto centred = [&](const Glib::ustring& text) {
    auto layout = create_pango_layout(text);
    layout->set_font_description(Face(kMessageSize));
    layout->set_width(width * PANGO_SCALE);
    layout->set_wrap(Pango::WrapMode::WORD_CHAR);
    layout->set_alignment(Pango::Alignment::CENTER);
    int layoutWidth = 0;
    int layoutHeight = 0;
    layout->get_pixel_size(layoutWidth, layoutHeight);
    SetSource(cr, muted);
    cr->move_to(0, (plotTop + plotBottom) / 2 - layoutHeight / 2.0);
    layout->show_in_cairo_context(cr);
  };

  // loading or unavailable: the line stands in for the bars and the axis
  if (!message_.empty()) {
    centred(message_);
    return;
  }

  // the baseline (TransferChart's axis colour), and the bars standing on it;
  // a zero is no bar
  SetSource(cr, kUrBorderBase);
  cr->rectangle(0, plotBottom - 1, w, 1);
  cr->fill();
  const size_t count = fractions_.size();
  if (count > 0) {
    const double slot = w / static_cast<double>(count);
    const double gap = slot >= 3 ? 1 : 0;
    for (size_t i = 0; i < count; ++i) {
      const double fraction = std::clamp(fractions_[i], 0.0, 1.0);
      if (fraction <= 0) continue;
      const double x0 = std::round(static_cast<double>(i) * slot);
      const double x1 = std::max(x0 + 1, std::round(static_cast<double>(i + 1) * slot) - gap);
      const double barHeight = std::max(1.0, std::round(fraction * (kPlotHeight - 1)));
      SetSource(cr, kUrGreen.WithAlpha(i + 1 == count ? kPartialMinuteAlpha : 1.0));
      cr->rectangle(x0, plotBottom - 1 - barHeight, x1 - x0, barHeight);
      cr->fill();
    }
  }
  if (!emptyLine_.empty()) centred(emptyLine_);

  // the axis ends: the oldest minute on the left, now on the right, each
  // ellipsized to half the width so they never overlap
  auto axis = [&](const Glib::ustring& text, bool alignEnd) {
    auto layout = create_pango_layout(text);
    layout->set_font_description(Face(kAxisSize));
    layout->set_ellipsize(Pango::EllipsizeMode::END);
    layout->set_width(std::max(1, width / 2 - 4) * PANGO_SCALE);
    int layoutWidth = 0;
    int layoutHeight = 0;
    layout->get_pixel_size(layoutWidth, layoutHeight);
    SetSource(cr, muted);
    cr->move_to(alignEnd ? w - layoutWidth : 0, plotBottom + kAxisGap);
    layout->show_in_cairo_context(cr);
  };
  axis(axisStart_, false);
  axis(axisEnd_, true);
}

}  // namespace urnw
