// Post Quantum Identity (PQI) surfaces — port of the apple
// ProviderIdentitiesView (+ PostQuantumIdentityStore's formatting and caching
// rules): ProviderIdentitiesSheet, the live "Provider Identities" list that
// Settings opens, with one row per provider with an established,
// identity-verified e2e session (identicon | grouped hash | client id; the
// texts click-copy), and the identicon pieces the provider-locations badge
// shares.
//
// Identicons are ALWAYS the canonical SDK raster
// (urnet::renderIdenticonPng, rendered at 2x the display size for
// crispness), cached per (hash, size) and clipped with the standard slight
// rounding (radius = size / 6) — the same bytes every platform renders for a
// key, so shared icons compare exactly.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <adwaita.h>
#include <gtkmm.h>

#include "SdkHost.hpp"

namespace urnw {

// The canonical identity key hash display rule, shared by every platform:
// split the 52-char hash into 4-char groups and show the first 4 groups, an
// ellipsis, then the last 2 groups. Copy always uses the full un-grouped
// hash, never this display form.
std::string FormatIdentityKeyHashForDisplay(const std::string& hash);

// Identicon raster cache, keyed by (key hash, display size) — the raster
// derives from the key, which the hash captures. Renders through the
// canonical SDK png at 2x the display size.
class IdenticonCache {
 public:
  Glib::RefPtr<Gdk::Pixbuf> Get(const std::vector<uint8_t>& key, const std::string& hash,
                                int size);
  void Clear() { cache_.clear(); }

 private:
  std::map<std::string, Glib::RefPtr<Gdk::Pixbuf>> cache_;
};

// The one widget that renders an identity key identicon: the (2x) SDK raster
// scaled to `size` and clipped with the standard rounding (radius = size / 6).
// With no pixbuf it keeps its footprint with a quiet placeholder, so layouts
// do not jump when the key loads.
class IdenticonWidget : public Gtk::DrawingArea {
 public:
  explicit IdenticonWidget(int size);
  void SetPixbuf(Glib::RefPtr<Gdk::Pixbuf> pixbuf);

 private:
  void Draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

  int size_;
  Glib::RefPtr<Gdk::Pixbuf> pixbuf_;
};

// One identity row (each provider in the list and the provider-locations
// badge join): the client id, the canonical hash, and the raw public identity
// key the identicons render from.
struct IdentityRow {
  std::string clientId;
  std::string hash;
  std::vector<uint8_t> key;
};

// the provider-locations trailing badge next to the client id (android
// BADGE_IDENTICON_SIZE); rasters render at 2x like every other size
constexpr int kBadgeIdenticonSize = 16;

// The providers with an established, identity-verified e2e session, read
// through the SdkHost PQI accessor and decoded (PublicKey crosses as base64,
// the hash computed through the canonical SDK rule). Shared by the identities
// list and the provider-locations badge join.
std::vector<IdentityRow> ReadProviderIdentityRows(SdkHost& host);

// value equality on (client id, hash) — the identicons derive from the key,
// which the hash captures (apple ProviderIdentityRow ==)
bool SameIdentityRows(const std::vector<IdentityRow>& a, const std::vector<IdentityRow>& b);

class ProviderIdentitiesSheet : public Gtk::Window {
 public:
  ProviderIdentitiesSheet(Gtk::Window& parent, SdkHost& host);

  void Open();
  // Live-update: rebuild the rows from the SdkHost accessor (the identity set
  // changes rarely and stays small, so a full rebuild per change is fine).
  void Refresh();

 private:
  SdkHost& host_;
  IdenticonCache cache_;
  AdwToastOverlay* toastOverlay_ = nullptr;
  Gtk::Box listBox_{Gtk::Orientation::VERTICAL};
  std::vector<IdentityRow> rows_;  // last applied rows, to skip rebuilds
};

}  // namespace urnw
