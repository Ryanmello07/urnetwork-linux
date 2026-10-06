// The provide line on the connect page, under the discoverable line
// (ConnectPage::ApplyStats): "Providing to N clients" while the device
// provides, "Providing (paused)" while it is paused, and nothing otherwise.
// Windows' rule and place (its ConnectPage::ApplyStats and ProvideStatsRow).
//
// N is the provider's connected network peers. A tunnel session's device is
// read directly; with no session the provider-only device's count comes from
// the daemon's status, the same status the provide dot and the discoverable
// line read (LiveStats). A daemon that predates the count, or a read that
// failed, gives none, and then the line is left out entirely rather than
// reading 0: the provide dot above still shows that the device provides.
//
// Pure (no GTK, gettext or SDK): tests/ProvideLineTest.cpp runs it anywhere.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw {

enum class ProvideLine {
  None,     // no line
  Paused,   // "Providing (paused)"
  Clients,  // "Providing to N clients"
};

constexpr ProvideLine ProvideLineFor(bool provideEnabled, bool providePaused,
                                     bool clientsUnknown) {
  if (!provideEnabled || clientsUnknown) return ProvideLine::None;
  return providePaused ? ProvideLine::Paused : ProvideLine::Clients;
}

}  // namespace urnw
