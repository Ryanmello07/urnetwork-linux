// The native crypto invoice (mmm/ZCASH-CONTRACT.md §6.2), a state of the upgrade sheet
// (UpgradeSheet.cpp), rendered from crypto_pay::PresentInvoice over the
// invoice controller's state (CryptoInvoiceHost::State()), top to bottom:
//
//   1. the network to send on: "USDC on Solana", "Zcash, shielded", "Monero"
//      (USDC adds the Solana-only warning);
//   2. the QR of the server's payment URI, drawn ink on paper in a
//      Gtk::DrawingArea from the vendored qrcodegen (PaymentQr.hpp), and Open
//      in wallet, which hands the URI to the desktop's handler for the coin's
//      scheme (g_app_info_launch_default_for_uri; the Flatpak's goes through
//      the OpenURI portal). Both only for a URI the SDK's
//      checkCryptoPaymentUri accepted. When no app takes the URI, the QR stays
//      on screen for a phone and nothing alarming is said;
//   3. "Send exactly" with the amount and its ticker, "To this address" with
//      the address (monospace, wrapping), USDC's memo, each with Copy (the
//      display's clipboard), which reads "Copied" for a moment;
//   4. "Quote expires in m:ss" with Refresh quote, or the expired line;
//   5. the status steps, what is left to send after a partial payment, or the
//      review copy alone;
//   6. the wallets that work, and Zcash's note about exchanges;
//   7. Pay another way (while nothing was received) and Close.
//
// The view only shows and reports: the sheet owns the invoice's actions, the
// visibility that starts and stops the controller, and what paid leads to.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <array>
#include <functional>
#include <string>

#include <gtkmm.h>

#include "CryptoPay.hpp"
#include "PaymentQr.hpp"

namespace urnw {

class CryptoInvoiceView : public Gtk::Box {
 public:
  CryptoInvoiceView();
  ~CryptoInvoiceView() override;

  void Render(const crypto_pay::InvoiceView& invoice);

  std::function<void()> on_refresh;          // Refresh quote
  std::function<void()> on_pay_another_way;  // Pay another way
  std::function<void()> on_close;            // Close

 private:
  // A caption, a value and its Copy button. `text` is what Copy puts on the
  // clipboard (the amount without its ticker, a wallet takes a number).
  struct CopyRow {
    Gtk::Box* root = nullptr;
    Gtk::Label* value = nullptr;
    Gtk::Button* copy = nullptr;
    std::string text;
    sigc::connection reset;  // "Copied" back to "Copy"
  };
  void BuildCopyRow(CopyRow& row, const Glib::ustring& caption);
  void Copy(CopyRow& row);
  void SetCopyText(CopyRow& row, const std::string& shown, const std::string& copied);

  struct StepRow {
    Gtk::Box* root = nullptr;
    Gtk::Label* dot = nullptr;
    Gtk::Label* text = nullptr;
  };
  void RenderSteps(const crypto_pay::InvoicePresentation& presentation);

  void DrawQr(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
  void OpenInWallet();

  Gtk::Label* network_ = nullptr;
  Gtk::Label* usdcWarning_ = nullptr;
  Gtk::DrawingArea* qr_ = nullptr;
  Gtk::Button* openWallet_ = nullptr;
  CopyRow amount_;
  CopyRow address_;
  CopyRow memo_;
  Gtk::Box* countdownRow_ = nullptr;
  Gtk::Label* countdown_ = nullptr;
  Gtk::Label* expired_ = nullptr;
  Gtk::Button* refresh_ = nullptr;
  Gtk::Box* steps_ = nullptr;
  // waiting, payment seen, confirming n of N, paid
  std::array<StepRow, 4> stepRows_{};
  Gtk::Label* remaining_ = nullptr;
  Gtk::Label* review_ = nullptr;
  Gtk::Label* wallets_ = nullptr;
  Gtk::Label* zcashNote_ = nullptr;
  Gtk::Label* notice_ = nullptr;
  Gtk::Button* payAnotherWay_ = nullptr;
  Gtk::Button* close_ = nullptr;

  crypto_pay::QrModules qrModules_;
  std::string qrUri_;    // the URI qrModules_ encodes
  std::string openUri_;  // what Open in wallet opens: a checked URI, or ""
};

}  // namespace urnw
