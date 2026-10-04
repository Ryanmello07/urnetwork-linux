// SPDX-License-Identifier: MPL-2.0
#include "BittensorManualSheet.hpp"

#include <string>

#include <adwaita.h>
#include <urnetwork_sdk.hpp>

#include "BittensorWalletFlow.hpp"
#include "Formatters.hpp"
#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

constexpr int kSheetMinWidth = 480;

Gtk::Label* MakeWrappedNote(const Glib::ustring& text, const char* cssClass) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->add_css_class(cssClass);
  label->set_xalign(0);
  label->set_wrap(true);
  label->set_ellipsize(Pango::EllipsizeMode::NONE);
  return label;
}

}  // namespace

GtkWidget* NewBittensorWalletChooser(GtkWindow* parent) {
  GtkWidget* dialog = adw_message_dialog_new(
      parent, T_("bittensor_choose_wallet", "Choose your Bittensor wallet"), nullptr);
  adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), "cancel", T_("cancel", "Cancel"));
  std::string body;
  for (const auto& wallet : bittensor::kChooserWallets) {
    const std::string walletId(wallet.walletId);
    // wallet names are product names: never translated (the SDK names them)
    const std::string name = urnet::bittensorWalletDisplayName(walletId);
    adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), walletId.c_str(), name.c_str());
    if (!wallet.hintKey.empty()) {
      const std::string hintKey(wallet.hintKey);
      const std::string hintEnglish(wallet.hintEnglish);
      if (!body.empty()) body += "\n";
      body += name + ": " + g_dpgettext2(GETTEXT_PACKAGE, hintKey.c_str(), hintEnglish.c_str());
    }
  }
  if (!body.empty()) adw_message_dialog_set_body(ADW_MESSAGE_DIALOG(dialog), body.c_str());
  const std::string first(bittensor::kChooserWallets[0].walletId);
  adw_message_dialog_set_response_appearance(ADW_MESSAGE_DIALOG(dialog), first.c_str(),
                                             ADW_RESPONSE_SUGGESTED);
  adw_message_dialog_set_default_response(ADW_MESSAGE_DIALOG(dialog), first.c_str());
  adw_message_dialog_set_close_response(ADW_MESSAGE_DIALOG(dialog), "cancel");
  return dialog;
}

BittensorManualSheet::BittensorManualSheet(Gtk::Window& parent, SdkHost& host,
                                           const SdkHost::BittensorManualRequest& request)
    : host_(host), flow_(request.flow) {
  set_transient_for(parent);
  set_modal(true);
  // wallet names are product names: never translated
  const std::string walletName = urnet::bittensorWalletDisplayName(request.walletId);
  set_title(walletName);
  set_default_size(kSheetMinWidth, -1);
  add_css_class("ur-sheet");

  auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
  column->set_margin(24);
  column->set_size_request(kSheetMinWidth, -1);

  column->append(*MakeWrappedNote(
      Format(T_("bittensor_manual_sign_instructions",
                "Sign this message with the {} wallet that holds your coldkey, then paste the "
                "address and the signature below."),
             walletName),
      "ur-key"));

  column->append(*MakeWrappedNote(T_("bittensor_message_to_sign", "Message to sign"),
                                  "ur-row-note"));
  // the exact text the wallet signs: selectable, never reflowed into other bytes
  auto* message = Gtk::make_managed<Gtk::Label>(request.message);
  message->set_selectable(true);
  message->set_xalign(0);
  message->set_wrap(true);
  message->set_wrap_mode(Pango::WrapMode::CHAR);
  message->add_css_class("monospace");
  column->append(*message);
  auto* copy = Gtk::make_managed<Gtk::Button>(T_("copy", "Copy"));
  copy->set_halign(Gtk::Align::START);
  copy->signal_clicked().connect(
      [this, text = request.message] { get_clipboard()->set_text(text); });
  column->append(*copy);

  addressEntry_ = Gtk::make_managed<Gtk::Entry>();
  addressEntry_->add_css_class("ur-input");
  addressEntry_->set_placeholder_text(
      T_("earnings_address_placeholder", "Enter a Bittensor address"));
  kit::SetAccessibleLabel(*addressEntry_,
                          T_("earnings_address_placeholder", "Enter a Bittensor address"));
  if (!request.expectedAddress.empty()) {
    // the challenge is bound to the typed address: only that account can sign
    addressEntry_->set_text(request.expectedAddress);
    addressEntry_->set_editable(false);
  }
  column->append(*addressEntry_);

  column->append(*MakeWrappedNote(T_("bittensor_signature_label", "Signature"), "ur-row-note"));
  signatureEntry_ = Gtk::make_managed<Gtk::Entry>();
  signatureEntry_->add_css_class("ur-input");
  signatureEntry_->set_placeholder_text(
      T_("bittensor_signature_placeholder", "Paste the 0x… signature"));
  kit::SetAccessibleLabel(*signatureEntry_, T_("bittensor_signature_label", "Signature"));
  signatureEntry_->signal_activate().connect([this] { OnContinue(); });
  column->append(*signatureEntry_);

  errorLine_ = MakeWrappedNote({}, "ur-caption");
  errorLine_->add_css_class("ur-danger-text");
  errorLine_->set_visible(false);
  column->append(*errorLine_);

  auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  buttons->set_halign(Gtk::Align::END);
  auto* cancel = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  cancel->signal_clicked().connect([this] { set_visible(false); });
  buttons->append(*cancel);
  auto* next = Gtk::make_managed<Gtk::Button>(T_("continue_txt", "Continue"));
  next->add_css_class("suggested-action");
  next->signal_clicked().connect([this] { OnContinue(); });
  buttons->append(*next);
  column->append(*buttons);

  set_child(*column);

  // closing without an answer ends the flow by the user's choice
  signal_hide().connect([this] {
    if (settled_) return;
    settled_ = true;
    host_.CancelBittensorManual(flow_);
  });
}

void BittensorManualSheet::OnContinue() {
  if (settled_) return;
  const urnet::BittensorWalletResult result =
      host_.SubmitBittensorManual(TrimWhitespace(addressEntry_->get_text().raw()),
                                  TrimWhitespace(signatureEntry_->get_text().raw()));
  if (bittensor::Classify(result.ErrorCode, /*manual=*/true) == bittensor::Outcome::Retry) {
    const bittensor::ErrorText text = bittensor::ErrorTextFor(result.ErrorCode);
    errorLine_->set_text(g_dpgettext2(GETTEXT_PACKAGE, std::string(text.key).c_str(),
                                      std::string(text.english).c_str()));
    errorLine_->set_visible(true);
    return;
  }
  // a proof went on to the flow, or the flow was answered with the refusal
  settled_ = true;
  set_visible(false);
}

}  // namespace urnw
