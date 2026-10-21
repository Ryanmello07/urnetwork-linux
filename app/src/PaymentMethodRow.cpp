// SPDX-License-Identifier: MPL-2.0
#include "PaymentMethodRow.hpp"

#include <algorithm>

#include "CryptoPay.hpp"
#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

namespace urnw {

Glib::ustring PaymentMethodName(const std::string& method) {
  const crypto_pay::Text label = crypto_pay::MethodLabel(method);
  return label.key ? Glib::ustring(T_(label.key, label.english)) : Glib::ustring(method);
}

PaymentMethodRow::PaymentMethodRow()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 6), selected_(crypto_pay::kMethodCard) {
  EnsureDrawerCss();
  // a group to assistive tech, named "Pay with"; each segment is named by its
  // method. The role is construct-only, so the box comes from the C
  // constructor (PlanPicker's monthly card does the same).
  toggles_ = Gtk::manage(Glib::wrap(GTK_BOX(g_object_new(
      GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_HORIZONTAL, "accessible-role",
      GTK_ACCESSIBLE_ROLE_GROUP, nullptr))));
  toggles_->add_css_class("linked");
  kit::SetAccessibleLabel(*toggles_, T_("pay_method_label", "Pay with"));
  append(*toggles_);

  unavailable_ = Gtk::make_managed<Gtk::Label>(
      T_("pay_method_unavailable", "Temporarily unavailable"));
  unavailable_->add_css_class("dim-label");
  unavailable_->add_css_class("caption");
  unavailable_->set_xalign(0);
  unavailable_->set_wrap(true);
  unavailable_->set_visible(false);
  append(*unavailable_);

  methods_ = crypto_pay::RowMethods({});
  Rebuild();
}

bool PaymentMethodRow::SetMethods(const std::vector<std::string>& methods) {
  const std::vector<std::string> next = crypto_pay::RowMethods(methods);
  if (next == methods_) return false;
  methods_ = next;
  selected_ = crypto_pay::MethodSelection(methods_, selected_);
  Rebuild();
  return true;
}

void PaymentMethodRow::Select(const std::string& method) {
  selected_ = crypto_pay::MethodSelection(methods_, method);
  Sync();
}

void PaymentMethodRow::SetUnavailable(bool unavailable) { unavailable_->set_visible(unavailable); }

void PaymentMethodRow::Rebuild() {
  syncing_ = true;
  buttons_.clear();
  RemoveAllChildren(*toggles_);  // managed: removing destroys them
  Gtk::ToggleButton* group = nullptr;
  for (const std::string& method : methods_) {
    const crypto_pay::Text label = crypto_pay::MethodLabel(method);
    if (!label.key) continue;  // RowMethods lists only methods with a label
    auto* toggle = Gtk::make_managed<Gtk::ToggleButton>(T_(label.key, label.english));
    toggle->set_hexpand(true);
    if (group) {
      toggle->set_group(*group);
    } else {
      group = toggle;
    }
    toggle->signal_toggled().connect([this, toggle, method] {
      // one report per pick, not per segment toggling off; none for Sync
      if (syncing_ || !toggle->get_active()) return;
      selected_ = method;
      if (on_select) on_select(method);
    });
    toggles_->append(*toggle);
    buttons_.emplace_back(method, toggle);
  }
  syncing_ = false;
  Sync();
  // no coin listed: no row at all (§6.1)
  set_visible(crypto_pay::MethodRowShown(methods_));
}

void PaymentMethodRow::Sync() {
  syncing_ = true;
  // the group turns the others off
  for (const auto& [method, toggle] : buttons_) {
    if (method == selected_) toggle->set_active(true);
  }
  syncing_ = false;
}

}  // namespace urnw
