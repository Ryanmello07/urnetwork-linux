// The provider statistics' "Demand" chart (support part P008): how often the
// network offered this device to clients in each minute of the last hour, as
// 60 vertical bars, oldest on the left and the current, partial minute on the
// right. Drawn with Cairo beside the TransferCharts and laid out like them:
// the title top left in their title face with the total right-aligned in the
// same row, the caption under it, the bars over a 1px baseline in the
// provider green, and the axis ends under the bars. In place of the bars it
// shows one muted line (loading, unavailable).
//
// The text is set with Pango, so it falls back across scripts, and the caption
// wraps onto as many lines as the pane's width needs (height-for-width).
// Holds no state of its own beyond what it draws; EarningsPage decides it
// (ProviderStatusPresentation.hpp). SPDX-License-Identifier: MPL-2.0
#pragma once

#include <vector>

#include <gtkmm.h>

namespace urnw {

class DemandChart : public Gtk::DrawingArea {
 public:
  // The texts arrive translated: the title (provider_status_demand), the
  // caption (provider_status_histogram_title, also the accessible
  // description) and the axis ends (provider_status_histogram_start, _end).
  DemandChart(Glib::ustring title, Glib::ustring caption, Glib::ustring axisStart,
              Glib::ustring axisEnd);

  // One muted line in place of the bars and the axis (loading, unavailable).
  void ShowMessage(const Glib::ustring& message);
  // The bars (HistogramFor's fractions, oldest first) and the total in the
  // title row. A non-empty `emptyLine` keeps the baseline and the axis and
  // centres it over them instead of bars.
  void ShowBars(std::vector<double> fractions, const Glib::ustring& total,
                const Glib::ustring& emptyLine);

 protected:
  Gtk::SizeRequestMode get_request_mode_vfunc() const override;
  void measure_vfunc(Gtk::Orientation orientation, int for_size, int& minimum, int& natural,
                     int& minimum_baseline, int& natural_baseline) const override;

 private:
  void OnDraw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
  // the caption's height at `width`, wrapped (one line before the first
  // allocation)
  int CaptionHeight(int width) const;
  void UpdateAccessibleDescription();

  Glib::ustring title_;
  Glib::ustring caption_;
  Glib::ustring axisStart_;
  Glib::ustring axisEnd_;
  Glib::ustring message_;  // non-empty: drawn in place of the bars
  Glib::ustring total_;
  Glib::ustring emptyLine_;
  std::vector<double> fractions_;
  // the caption's layout, shared by the measure and the draw so the two agree
  Glib::RefPtr<Pango::Layout> captionLayout_;
};

}  // namespace urnw
