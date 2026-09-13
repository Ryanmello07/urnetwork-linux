// The ip-version histogram (connect/IPV6.md D2): under the transport bar in
// the client statistics card, the connect window's ADDED providers as dots
// stacked in three wrapping rows -- "Both" (dualstack), "v4" (v4-only) and
// "v6" (v6-only) -- so a glance says which families the tunnel can carry
// right now and how much redundancy each has. An empty row shows just its
// label: "no v6 exit" is a reading, not an absence.
//
// The dots are the connect canvas's own dots at the connect canvas's own
// size: one grid cell in iOS's 256pt space (IpFamilyHistogramGeometry.hpp),
// filled in the canvas's Added green -- INCLUDING the canvas's extender rings
// (EXTENDER.md K2: "the drawer's ip family histogram draws the same dots and
// the same rings at its dot size"). Which row a provider lands in, the dot
// size and the ring radii are the pure functions in
// IpFamilyHistogramGeometry.hpp and ExtenderRingGeometry.hpp; this widget only
// draws them. Rows wrap through the transport bar's WrapRow.
//
// DECORATIVE: the dots carry no interaction. The whole component names itself
// to accessibility with its three counts, since a wall of identical circles
// says nothing to a screen reader.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <gtkmm.h>

#include <urnetwork_sdk.hpp>

#include "ExtenderRingGeometry.hpp"
#include "IpFamilyHistogramGeometry.hpp"
#include "TransportBar.hpp"

namespace urnw {

class IpFamilyHistogram : public Gtk::Box {
 public:
  IpFamilyHistogram();

  // Feed the live provider grid (LiveStats::gridPoints / gridWidth /
  // gridHeight), on the same push the hero canvas rides. An empty list is a
  // normal reading (no session, nothing added yet) and renders as three bare
  // labels. Dedups by value: a push that changes no count and no dot size
  // touches nothing.
  void SetGrid(const std::vector<urnet::ProviderGridPoint>& points, int64_t gridWidth,
               int64_t gridHeight);

 private:
  void BuildUi();
  void RebuildRow(ipfamily::Row row);
  void UpdateAccessibleLabel();

  // per row: the wrapping dot strip
  WrapRow* rows_[ipfamily::kRowCount] = {nullptr, nullptr, nullptr};
  ipfamily::Rows counts_;
  // Per row, one entry per dot: that provider's extender colors in the SDK's
  // order (empty over a direct route). Held because the dots are rebuilt from
  // it, and compared because a push that changes no count, no dot size and no
  // extender set must touch no widget.
  std::vector<std::vector<std::string>> rowExtenders_[ipfamily::kRowCount];
  int dotDiameter_ = 0;
};

}  // namespace urnw
