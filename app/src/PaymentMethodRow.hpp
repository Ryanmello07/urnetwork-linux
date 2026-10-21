// The payment-method row: Card · USDC · Zcash · Monero as linked toggle
// buttons (mmm/ZCASH-CONTRACT.md §6.1, §6.3 Linux: no AdwToggleGroup, which the
// AppImage's libadwaita 1.5 lacks), labelled "Pay with" for a screen reader.
// It sits directly above the buy button, after the plan cards, on the
// upgrade sheet's options and on the onboarding's welcome and offer pages.
//
// The segments are the SDK's urnet::paymentMethods answer
// (crypto_pay::RowMethods: Card first, then USDC, Zcash, Monero, each only
// when the server lists it as selling the plan). With no coin listed the row
// hides itself and the surface looks exactly as before. "Temporarily
// unavailable" shows under it while the selected coin's last start failed
// with asset_unavailable; the coin stays selectable for a retry.
//
// The row only shows and reports the choice: the host follows it (the button
// and its fine print, monthly greyed for a coin) and remembers it on the
// device (CryptoInvoiceHost::SetPaymentMethod).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <gtkmm.h>

namespace urnw {

// A method's name as its segment reads ("Card", "USDC", "Zcash", "Monero"),
// for "Pay with {}" and "Paying with {} is temporarily unavailable". The id
// itself for a method this app has no label for.
Glib::ustring PaymentMethodName(const std::string& method);

class PaymentMethodRow : public Gtk::Box {
 public:
  PaymentMethodRow();

  // The methods to offer (urnet::paymentMethods' answer). True when the list
  // changed, so the host chooses the selection again (Select); until then a
  // selection the list no longer has falls back to Card. on_select does not
  // fire for either.
  bool SetMethods(const std::vector<std::string>& methods);
  // Selects `method` when listed, Card otherwise. Does not fire on_select.
  void Select(const std::string& method);
  const std::string& Selected() const { return selected_; }
  // "Temporarily unavailable" under the row.
  void SetUnavailable(bool unavailable);

  // Fired when the user picks a method.
  std::function<void(const std::string& method)> on_select;

 private:
  void Rebuild();
  // the toggles show selected_
  void Sync();

  Gtk::Box* toggles_ = nullptr;
  Gtk::Label* unavailable_ = nullptr;
  std::vector<std::pair<std::string, Gtk::ToggleButton*>> buttons_;
  std::vector<std::string> methods_;
  std::string selected_;
  bool syncing_ = false;  // set_active from Sync, not a pick
};

}  // namespace urnw
