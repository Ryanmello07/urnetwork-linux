// SPDX-License-Identifier: MPL-2.0
#include "PostQuantumIdentity.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <gdkmm/general.h>
#include <gdkmm/pixbufloader.h>
#include <glib.h>

#include "I18n.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

// the identities list's identicon size (apple PostQuantumIdentityStore);
// rasters render at 2x it for crispness
constexpr int kRowIdenticonSize = 40;

std::vector<uint8_t> DecodeBase64(const std::string& base64) {
  std::vector<uint8_t> out;
  if (base64.empty()) return out;
  gsize len = 0;
  guchar* data = g_base64_decode(base64.c_str(), &len);
  if (data) {
    out.assign(data, data + len);
    g_free(data);
  }
  return out;
}

// split the canonical hash into 4-char groups
std::vector<std::string> HashGroups(const std::string& hash) {
  std::vector<std::string> groups;
  for (size_t i = 0; i < hash.size(); i += 4) {
    groups.push_back(hash.substr(i, 4));
  }
  return groups;
}

std::string JoinGroups(const std::vector<std::string>& groups) {
  std::string out;
  for (const auto& group : groups) {
    if (!out.empty()) out += ' ';
    out += group;
  }
  return out;
}

// the standard identicon rounding: radius = size / 6, everywhere
void RoundedRectPath(const Cairo::RefPtr<Cairo::Context>& cr, double inset, double size,
                     double radius) {
  const double lo = inset;
  const double hi = size - inset;
  const double r = std::max(0.0, radius - inset);
  cr->begin_new_path();
  cr->arc(hi - r, lo + r, r, -G_PI / 2, 0);
  cr->arc(hi - r, hi - r, r, 0, G_PI / 2);
  cr->arc(lo + r, hi - r, r, G_PI / 2, G_PI);
  cr->arc(lo + r, lo + r, r, G_PI, 3 * G_PI / 2);
  cr->close_path();
}

// click-to-copy: pointer cursor + released gesture
void MakeClickable(Gtk::Widget& widget, std::function<void()> action) {
  SetPointerCursor(widget);
  auto gesture = Gtk::GestureClick::create();
  gesture->signal_released().connect(
      [action = std::move(action)](int, double, double) { action(); });
  widget.add_controller(gesture);
}

}  // namespace

// The providers with an established, identity-verified e2e session, decoded
// from the JSON-crossing ProviderIdentityList (PublicKey crosses as base64).
// The hash is computed through the canonical SDK rule, like apple's
// identity.getPublicKeyHash(). Exposed (see the header) so the
// provider-locations badge joins against the same set.
std::vector<IdentityRow> ReadProviderIdentityRows(SdkHost& host) {
  std::vector<IdentityRow> rows;
  if (auto list = host.ProviderIdentities()) {
    rows.reserve(list->size());
    for (const auto& identity : *list) {
      if (!identity.ClientId || identity.ClientId->empty()) continue;
      std::vector<uint8_t> key = DecodeBase64(identity.PublicKey);
      if (key.empty()) continue;
      IdentityRow row;
      row.clientId = *identity.ClientId;
      row.hash = urnet::publicIdentityKeyHash(key.data(), static_cast<int32_t>(key.size()));
      row.key = std::move(key);
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

bool SameIdentityRows(const std::vector<IdentityRow>& a, const std::vector<IdentityRow>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].clientId != b[i].clientId || a[i].hash != b[i].hash) return false;
  }
  return true;
}

// ---- hash display rules (apple PostQuantumIdentityStore) --------------------

std::string FormatIdentityKeyHashForDisplay(const std::string& hash) {
  const std::vector<std::string> groups = HashGroups(hash);
  if (groups.size() <= 6) return JoinGroups(groups);
  std::vector<std::string> shown(groups.begin(), groups.begin() + 4);
  shown.push_back("…");
  shown.insert(shown.end(), groups.end() - 2, groups.end());
  return JoinGroups(shown);
}

// ---- identicon raster cache -------------------------------------------------

Glib::RefPtr<Gdk::Pixbuf> IdenticonCache::Get(const std::vector<uint8_t>& key,
                                              const std::string& hash, int size) {
  const std::string cacheKey = hash + ":" + std::to_string(size);
  if (auto it = cache_.find(cacheKey); it != cache_.end()) return it->second;
  Glib::RefPtr<Gdk::Pixbuf> pixbuf;
  try {
    // ALWAYS the canonical SDK raster, at 2x the display size (the widget
    // scales it back down, so it stays crisp on hidpi)
    const std::vector<uint8_t> png = urnet::renderIdenticonPng(key, size * 2);
    auto loader = Gdk::PixbufLoader::create();
    loader->write(png.data(), png.size());
    loader->close();
    pixbuf = loader->get_pixbuf();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[pqi] identicon render failed: %s\n", e.what());
    return {};
  }
  cache_[cacheKey] = pixbuf;
  return pixbuf;
}

// ---- IdenticonWidget --------------------------------------------------------

IdenticonWidget::IdenticonWidget(int size) : size_(size) {
  set_content_width(size_);
  set_content_height(size_);
  set_draw_func(sigc::mem_fun(*this, &IdenticonWidget::Draw));
}

void IdenticonWidget::SetPixbuf(Glib::RefPtr<Gdk::Pixbuf> pixbuf) {
  pixbuf_ = std::move(pixbuf);
  queue_draw();
}

void IdenticonWidget::Draw(const Cairo::RefPtr<Cairo::Context>& cr, int, int) {
  const double size = size_;
  const double radius = size / 6.0;
  cr->save();
  RoundedRectPath(cr, 0, size, radius);
  cr->clip();
  if (pixbuf_ && 0 < pixbuf_->get_width()) {
    const double scale = size / pixbuf_->get_width();
    cr->scale(scale, scale);
    Gdk::Cairo::set_source_pixbuf(cr, pixbuf_, 0, 0);
    cr->paint();
  } else {
    // key not available yet: a quiet placeholder with the same footprint, so
    // the layout does not jump when the key loads
    cr->set_source_rgba(0.5, 0.5, 0.5, 0.15);
    cr->paint();
  }
  cr->restore();
}

// ---- ProviderIdentitiesSheet ------------------------------------------------

ProviderIdentitiesSheet::ProviderIdentitiesSheet(Gtk::Window& parent, SdkHost& host)
    : host_(host) {
  EnsureDrawerCss();
  set_title(T_("provider_identities", "Provider Identities"));
  set_transient_for(parent);
  set_modal(true);
  set_default_size(480, 560);
  set_hide_on_close(true);
  AddEscapeToClose(*this);

  // AdwToastOverlay (C API) hosts the copied-to-clipboard toasts
  toastOverlay_ = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
  gtk_window_set_child(GTK_WINDOW(gobj()), GTK_WIDGET(toastOverlay_));

  auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  adw_toast_overlay_set_child(toastOverlay_, GTK_WIDGET(scroller->gobj()));

  listBox_.set_margin(16);
  listBox_.set_valign(Gtk::Align::START);
  scroller->set_child(listBox_);
}

void ProviderIdentitiesSheet::Open() {
  Refresh();
  present();
}

void ProviderIdentitiesSheet::Refresh() {
  std::vector<IdentityRow> rows = ReadProviderIdentityRows(host_);
  if (SameIdentityRows(rows, rows_)) return;  // rows carry no live values

  RemoveAllChildren(listBox_);
  auto copyable = [this](Gtk::Label* label, std::string value, bool isHash) {
    MakeClickable(*label, [this, value = std::move(value), isHash] {
      get_clipboard()->set_text(value);
      adw_toast_overlay_add_toast(
          toastOverlay_,
          adw_toast_new(isHash ? T_("identity_key_hash_copied", "Provider identity key hash copied")
                               : T_("client_id_copied", "Client ID copied")));
    });
  };
  for (const IdentityRow& row : rows) {
    auto* rowBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
    rowBox->set_margin_top(12);
    rowBox->set_margin_bottom(12);

    auto* icon = Gtk::make_managed<IdenticonWidget>(kRowIdenticonSize);
    icon->SetPixbuf(cache_.Get(row.key, row.hash, kRowIdenticonSize));
    icon->set_valign(Gtk::Align::CENTER);
    rowBox->append(*icon);

    auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    column->set_hexpand(true);
    column->set_valign(Gtk::Align::CENTER);

    // the identity key hash, click to copy the full hash
    auto* hashLabel = Gtk::make_managed<Gtk::Label>(FormatIdentityKeyHashForDisplay(row.hash));
    hashLabel->add_css_class("ur-mono-13");
    hashLabel->set_xalign(0);
    hashLabel->set_wrap(true);
    copyable(hashLabel, row.hash, /*isHash=*/true);
    column->append(*hashLabel);

    // the client id, click to copy
    auto* idLabel = Gtk::make_managed<Gtk::Label>(row.clientId);
    idLabel->add_css_class("ur-mono-11");
    idLabel->add_css_class("ur-label-faint");
    idLabel->set_xalign(0);
    idLabel->set_wrap(true);
    copyable(idLabel, row.clientId, /*isHash=*/false);
    column->append(*idLabel);

    rowBox->append(*column);
    listBox_.append(*rowBox);
    listBox_.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
  }
  rows_ = std::move(rows);
}

}  // namespace urnw
