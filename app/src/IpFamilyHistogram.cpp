// SPDX-License-Identifier: MPL-2.0
#include "IpFamilyHistogram.hpp"

#include <algorithm>

#include <glib.h>

#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

// the connect canvas's Added dot color (ConnectCanvas.cpp kDotColors[Added])
constexpr Rgba kAddedDot = kUrGreen;
// the gap between dots in a row and between wrapped rows, in px
constexpr int kDotGap = 2;
// the row label column, wide enough for the widest of the three labels so the
// dot strips line up
constexpr int kLabelWidth = 40;

// The label for a row, through the store (the same three words the provider
// rows tag themselves with).
const char* RowLabel(ipfamily::Row row) {
  switch (row) {
    case ipfamily::Row::Both: return T_("ip_family_both", "Both");
    case ipfamily::Row::V4: return T_("ip_family_v4", "v4");
    case ipfamily::Row::V6: return T_("ip_family_v6", "v6");
  }
  return "";
}

// One dot: a filled circle of the canvas's cell size in the Added green.
Gtk::DrawingArea* MakeAddedDot(int diameter) {
  auto* dot = Gtk::make_managed<Gtk::DrawingArea>();
  dot->set_content_width(diameter);
  dot->set_content_height(diameter);
  dot->set_valign(Gtk::Align::CENTER);
  dot->set_draw_func([](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    const double r = std::min(w, h) / 2.0;
    cr->arc(w / 2.0, h / 2.0, r, 0, 2 * G_PI);
    cr->set_source_rgba(kAddedDot.r, kAddedDot.g, kAddedDot.b, kAddedDot.a);
    cr->fill();
  });
  kit::MarkDecorative(*dot);
  return dot;
}

}  // namespace

IpFamilyHistogram::IpFamilyHistogram() : Gtk::Box(Gtk::Orientation::VERTICAL, 6) {
  EnsureDrawerCss();
  BuildUi();
}

void IpFamilyHistogram::BuildUi() {
  // title row, styled like the transport bar's so the two read as one stack
  auto* title = Gtk::make_managed<Gtk::Label>(T_("ip_families", "IP versions"));
  title->add_css_class("dim-label");
  title->add_css_class("ur-caption-11");
  title->set_xalign(0);
  append(*title);

  for (int i = 0; i < ipfamily::kRowCount; ++i) {
    const auto row = static_cast<ipfamily::Row>(i);
    auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* label = Gtk::make_managed<Gtk::Label>(RowLabel(row));
    label->add_css_class("ur-caption-11");
    label->set_xalign(0);
    label->set_size_request(kLabelWidth, -1);
    // top-aligned against a strip that may wrap to several lines
    label->set_valign(Gtk::Align::START);
    line->append(*label);
    rows_[i] = Gtk::make_managed<WrapRow>(kDotGap, kDotGap);
    rows_[i]->set_hexpand(true);
    line->append(*rows_[i]);
    append(*line);
  }
  UpdateAccessibleLabel();
}

void IpFamilyHistogram::SetGrid(const std::vector<urnet::ProviderGridPoint>& points,
                                int64_t gridWidth, int64_t gridHeight) {
  std::vector<ipfamily::Point> reduced;
  reduced.reserve(points.size());
  for (const auto& point : points) {
    reduced.push_back(ipfamily::Point{point.State, point.IpFamily});
  }
  const ipfamily::Rows counts = ipfamily::GroupPoints(reduced);
  const int dotDiameter = ipfamily::DotDiameter(gridWidth, gridHeight);

  const bool sizeChanged = dotDiameter != dotDiameter_;
  dotDiameter_ = dotDiameter;
  bool anyChanged = sizeChanged;
  for (int i = 0; i < ipfamily::kRowCount; ++i) {
    const auto row = static_cast<ipfamily::Row>(i);
    if (!sizeChanged && counts.at(row) == counts_.at(row)) continue;
    counts_.counts[i] = counts.counts[i];
    RebuildRow(row);
    anyChanged = true;
  }
  if (anyChanged) UpdateAccessibleLabel();
}

void IpFamilyHistogram::RebuildRow(ipfamily::Row row) {
  WrapRow* strip = rows_[static_cast<int>(row)];
  if (strip == nullptr) return;
  strip->Clear();
  const int count = counts_.at(row);
  for (int i = 0; i < count; ++i) strip->Append(*MakeAddedDot(dotDiameter_));
  // an empty strip collapses so the label sits alone on its line
  strip->set_visible(0 < count);
}

void IpFamilyHistogram::UpdateAccessibleLabel() {
  // "IP versions: Both 4, v4 1, v6 0" -- the counts are the whole reading
  std::string label = T_("ip_families", "IP versions");
  label += ":";
  for (int i = 0; i < ipfamily::kRowCount; ++i) {
    const auto row = static_cast<ipfamily::Row>(i);
    label += (i == 0 ? " " : ", ");
    label += RowLabel(row);
    label += " ";
    label += std::to_string(counts_.at(row));
  }
  kit::SetAccessibleLabel(*this, label);
}

}  // namespace urnw
