// SPDX-License-Identifier: MPL-2.0
#include "Top200SignSheet.hpp"

#include <string>
#include <utility>

#include <glib.h>

#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

constexpr int kSheetMinWidth = 520;
constexpr int kSheetMaxHeight = 720;
constexpr int kCopiedResetMs = 1800;
// the fleet worker is shared by the identity read and the sign (and by a
// closed sheet's call still out): a busy worker is asked again
constexpr int kWorkerRetryMs = 300;

Gtk::Label* MakeNote(const Glib::ustring& text, const char* cssClass) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->add_css_class(cssClass);
  label->set_xalign(0);
  label->set_wrap(true);
  label->set_ellipsize(Pango::EllipsizeMode::NONE);
  return label;
}

Gtk::Label* MakeMono(const Glib::ustring& text) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->add_css_class("monospace");
  label->set_xalign(0);
  return label;
}

// "Caption   value": the caption muted, the value monospace.
Gtk::Box* MakeValueRow(const Glib::ustring& caption, Gtk::Widget& value) {
  auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  auto* label = Gtk::make_managed<Gtk::Label>(caption);
  label->add_css_class("ur-caption");
  label->set_xalign(0);
  label->set_size_request(110, -1);
  row->append(*label);
  value.set_hexpand(true);
  row->append(value);
  return row;
}

}  // namespace

Top200SignSheet::Top200SignSheet(SdkHost& host) : host_(host) {
  set_title(T_("top200_sign_binding", "Sign fleet binding"));
  set_default_size(kSheetMinWidth, -1);

  auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
  column->set_margin(24);
  column->set_size_request(kSheetMinWidth, -1);

  // 1. title and intro
  auto* title = MakeNote(T_("top200_sign_binding", "Sign fleet binding"), "ur-value");
  title->add_css_class("title-3");
  column->append(*title);
  column->append(*MakeNote(
      T_("top200_sign_intro",
         "Your fleet operator builds this device's binding on the Top 200 page at ur.io. Paste "
         "it here to sign it with this device's client key. Nothing is sent: you copy the "
         "signed payload back to the page yourself."),
      "ur-key"));

  // 2. this device: the client id and client key, each copyable in full
  column->append(*kit::MakeDivider());
  column->append(*MakeNote(T_("top200_sign_this_device", "This device"), "ur-row-note"));
  const Glib::ustring copyHint = T_("copy_to_clipboard", "Copy to Clipboard");
  clientIdValue_ = MakeMono("…");
  column->append(*MakeValueRow(T_("client_id", "Client ID"),
                               *MakeCopyTextButton(*clientIdValue_, copyHint, [this] {
                                 if (!clientId_.empty()) get_clipboard()->set_text(clientId_);
                               })));
  clientKeyValue_ = MakeMono("…");
  column->append(*MakeValueRow(T_("top200_sign_client_key", "Client key"),
                               *MakeCopyTextButton(*clientKeyValue_, copyHint, [this] {
                                 if (!clientKey_.empty()) get_clipboard()->set_text(clientKey_);
                               })));
  column->append(*MakeNote(
      T_("top200_sign_member_hint",
         "Your fleet manifest lists this device by its client ID and client key. Copy its "
         "member entry and send it to your fleet operator."),
      "ur-caption"));
  copyMember_ = Gtk::make_managed<Gtk::Button>(T_("top200_sign_copy_member", "Copy member entry"));
  copyMember_->add_css_class("ur-btn-secondary");
  copyMember_->set_halign(Gtk::Align::START);
  copyMember_->set_sensitive(false);
  copyMember_->signal_clicked().connect([this] {
    const std::string entry = top200::MemberEntry(clientId_, clientKey_);
    if (entry.empty()) return;
    get_clipboard()->set_text(entry);
    FlashCopied(*copyMember_, T_("top200_sign_copy_member", "Copy member entry"),
                memberCopiedReset_);
  });
  column->append(*copyMember_);
  deviceError_ = MakeNote({}, "ur-caption");
  deviceError_->add_css_class("ur-danger-text");
  deviceError_->set_visible(false);
  column->append(*deviceError_);

  // 3. the binding field and Paste
  column->append(*kit::MakeDivider());
  auto* fieldLine = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  auto* fieldLabel = MakeNote(T_("top200_sign_binding_label", "Binding to sign"), "ur-row-note");
  fieldLabel->set_hexpand(true);
  fieldLabel->set_valign(Gtk::Align::CENTER);
  fieldLine->append(*fieldLabel);
  auto* paste = Gtk::make_managed<Gtk::Button>(T_("top200_sign_paste", "Paste"));
  paste->add_css_class("ur-btn-secondary");
  paste->signal_clicked().connect([this] { PasteFromClipboard(); });
  fieldLine->append(*paste);
  column->append(*fieldLine);
  text_ = Gtk::make_managed<Gtk::TextView>();
  text_->set_wrap_mode(Gtk::WrapMode::CHAR);
  text_->set_monospace(true);
  text_->set_input_hints(Gtk::InputHints::NO_SPELLCHECK | Gtk::InputHints::NO_EMOJI);
  text_->add_css_class("ur-card-bordered");
  kit::SetAccessibleLabel(*text_, T_("top200_sign_binding_label", "Binding to sign"));
  text_->get_buffer()->signal_changed().connect([this] {
    if (settingText_) return;
    OnTextChanged();
  });
  auto* textScroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  textScroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  textScroll->set_min_content_height(96);
  textScroll->set_child(*text_);
  column->append(*textScroll);

  // 7. errors, inline under the field
  errorLine_ = MakeNote({}, "ur-caption");
  errorLine_->add_css_class("ur-danger-text");
  errorLine_->set_visible(false);
  column->append(*errorLine_);

  // 4. the review: on screen before Sign is enabled
  reviewBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  reviewBox_->append(*MakeNote(
      T_("top200_sign_review",
         "Check that the fleet ID, hotkey and epochs match the Top 200 page before you sign."),
      "ur-key"));
  fleetIdValue_ = MakeMono({});
  fleetIdValue_->set_selectable(true);
  reviewBox_->append(*MakeValueRow(T_("top200_sign_fleet_id", "Fleet ID"), *fleetIdValue_));
  // the hotkey in full, all 64 hex digits: it decides who gets this device's
  // spot, so the user compares every digit with the page
  hotkeyValue_ = MakeMono({});
  hotkeyValue_->set_selectable(true);
  hotkeyValue_->set_wrap(true);
  hotkeyValue_->set_wrap_mode(Pango::WrapMode::CHAR);
  reviewBox_->append(*MakeValueRow(T_("top200_sign_hotkey", "Hotkey"), *hotkeyValue_));
  chainValue_ = MakeNote({}, "ur-value");
  reviewBox_->append(*chainValue_);
  epochsValue_ = MakeNote({}, "ur-value");
  reviewBox_->append(*epochsValue_);
  reviewBox_->set_visible(false);
  column->append(*reviewBox_);

  // 5. the action
  auto* actionLine = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  signButton_ = Gtk::make_managed<Gtk::Button>(T_("top200_sign_action", "Sign with this device"));
  signButton_->add_css_class("suggested-action");
  signButton_->set_sensitive(false);
  signButton_->signal_clicked().connect([this] { OnSign(); });
  actionLine->append(*signButton_);
  spinner_ = Gtk::make_managed<Gtk::Spinner>();
  spinner_->set_visible(false);
  actionLine->append(*spinner_);
  column->append(*actionLine);

  // 6. the signed payload
  doneBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  doneBox_->append(*MakeNote(
      T_("top200_sign_done",
         "Signed. Copy the payload and paste it on the Top 200 page under Device signature."),
      "ur-key"));
  payloadValue_ = MakeMono({});
  payloadValue_->set_selectable(true);
  payloadValue_->set_wrap(true);
  payloadValue_->set_wrap_mode(Pango::WrapMode::CHAR);
  payloadValue_->add_css_class("ur-card-bordered");
  doneBox_->append(*payloadValue_);
  copyPayload_ = Gtk::make_managed<Gtk::Button>(T_("top200_sign_copy_payload", "Copy payload"));
  copyPayload_->add_css_class("suggested-action");
  copyPayload_->set_halign(Gtk::Align::START);
  copyPayload_->signal_clicked().connect([this] {
    if (payload_.empty()) return;
    get_clipboard()->set_text(payload_);
    FlashCopied(*copyPayload_, T_("top200_sign_copy_payload", "Copy payload"),
                payloadCopiedReset_);
  });
  doneBox_->append(*copyPayload_);
  doneBox_->set_visible(false);
  column->append(*doneBox_);

  auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroller->set_propagate_natural_height(true);
  scroller->set_max_content_height(kSheetMaxHeight);
  scroller->set_child(*column);

  auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  root->append(*scroller);
  auto* close = Gtk::make_managed<Gtk::Button>(T_("close", "Close"));
  close->set_halign(Gtk::Align::END);
  close->set_margin(16);
  close->signal_clicked().connect([this] { set_visible(false); });
  root->append(*close);
  set_child(*root);

  RequestIdentity();
}

Top200SignSheet::~Top200SignSheet() {
  *alive_ = false;
  memberCopiedReset_.disconnect();
  payloadCopiedReset_.disconnect();
  identityRetry_.disconnect();
  signRetry_.disconnect();
}

void Top200SignSheet::Prefill(const std::optional<std::string>& binding) {
  if (signing_) return;  // a sign in flight keeps its text
  settingText_ = true;
  text_->get_buffer()->set_text(binding.value_or(std::string()));
  settingText_ = false;
  OnTextChanged();
  linkInvalid_ = !binding.has_value();
  Revalidate();
}

void Top200SignSheet::RequestIdentity() {
  auto alive = alive_;
  const bool started =
      host_.RequestSnFleetIdentity([this, alive](std::string clientId, std::string clientKey) {
        if (!*alive) return;
        OnIdentity(clientId, clientKey);
      });
  if (started) return;
  identityRetry_.disconnect();
  identityRetry_ = Glib::signal_timeout().connect(
      [this]() -> bool {
        RequestIdentity();
        return false;
      },
      kWorkerRetryMs);
}

void Top200SignSheet::OnIdentity(const std::string& clientId, const std::string& clientKey) {
  identityLoaded_ = true;
  clientId_ = clientId;
  clientKey_ = clientKey;
  clientIdValue_->set_text(clientId_.empty() ? Glib::ustring("-") : Glib::ustring(clientId_));
  clientIdValue_->set_tooltip_text(clientId_);
  clientKeyValue_->set_text(clientKey_.empty() ? Glib::ustring("-")
                                               : Glib::ustring(top200::ShortKey(clientKey_)));
  clientKeyValue_->set_tooltip_text(clientKey_);
  copyMember_->set_sensitive(!top200::MemberEntry(clientId_, clientKey_).empty());
  if (clientKey_.empty()) {
    deviceError_->set_text(T_("top200_sign_error_no_key",
                              "This device's client key is not available right now. Start "
                              "URnetwork on this device, then try again."));
    deviceError_->set_visible(true);
  } else {
    deviceError_->set_visible(false);
  }
  Revalidate();
}

void Top200SignSheet::PasteFromClipboard() {
  auto clipboard = get_clipboard();
  auto alive = alive_;
  clipboard->read_text_async([this, alive, clipboard](Glib::RefPtr<Gio::AsyncResult>& result) {
    Glib::ustring text;
    try {
      text = clipboard->read_text_finish(result);
    } catch (const Glib::Error& e) {
      g_warning("top200 sign: clipboard read failed: %s", e.what());
      return;
    }
    if (!*alive || signing_) return;
    text_->get_buffer()->set_text(text);  // OnTextChanged runs from the buffer
  });
}

void Top200SignSheet::OnTextChanged() {
  // an edit clears the signed payload and any link error
  linkInvalid_ = false;
  payload_.clear();
  payloadValue_->set_text({});
  doneBox_->set_visible(false);
  Revalidate();
}

void Top200SignSheet::Revalidate() {
  const std::string text = text_->get_buffer()->get_text().raw();
  check_.reset();
  reviewBox_->set_visible(false);
  errorLine_->set_visible(false);
  if (linkInvalid_) {
    ShowError(T_("top200_sign_error_invalid",
                 "This is not a fleet binding. Copy the whole binding from the Top 200 page."));
    UpdateSignButton();
    return;
  }
  if (top200::TrimJsonWhitespace(text).empty() || !identityLoaded_) {
    UpdateSignButton();
    return;
  }
  check_ = top200::ValidateBinding(text, clientId_, clientKey_);
  switch (check_->error) {
    case top200::BindingError::Invalid:
      ShowError(T_("top200_sign_error_invalid",
                   "This is not a fleet binding. Copy the whole binding from the Top 200 page."));
      break;
    case top200::BindingError::NoKey:
      ShowError(T_("top200_sign_error_no_key",
                   "This device's client key is not available right now. Start URnetwork on "
                   "this device, then try again."));
      break;
    case top200::BindingError::OtherDevice:
      ShowError(T_("top200_sign_error_other_device",
                   "This binding is for another device. A device signs only the binding with "
                   "its own client ID and client key."));
      break;
    case top200::BindingError::None: {
      const top200::Binding& binding = *check_->binding;
      fleetIdValue_->set_text(top200::ShortKey(binding.fleetId));
      fleetIdValue_->set_tooltip_text(binding.fleetId);
      hotkeyValue_->set_text(binding.hotkey);
      chainValue_->set_text(
          Format(T_("top200_sign_chain", "Chain {0} · subnet {1}"), binding.chainId, binding.netuid));
      epochsValue_->set_text(Format(T_("top200_sign_epochs", "Valid from epoch {0} to {1}"),
                                    binding.validFromEpoch, binding.validToEpoch));
      reviewBox_->set_visible(true);
      break;
    }
  }
  UpdateSignButton();
}

void Top200SignSheet::ShowError(const Glib::ustring& message) {
  errorLine_->set_text(message);
  errorLine_->set_visible(true);
}

void Top200SignSheet::UpdateSignButton() {
  // only a reviewed binding of this device, never while a sign is out
  const bool reviewed = check_ && check_->error == top200::BindingError::None &&
                        reviewBox_->get_visible();
  const top200::SignButtonState state =
      top200::SignButton(reviewed, identityLoaded_, !clientKey_.empty(), signing_);
  signButton_->set_sensitive(state.sensitive);
  spinner_->set_visible(state.busy);
  if (state.busy) {
    spinner_->start();
  } else {
    spinner_->stop();
  }
}

void Top200SignSheet::OnSign() {
  if (signing_) return;
  // checked again at the tap: the device and the text must still agree
  Revalidate();
  if (!check_ || check_->error != top200::BindingError::None) return;
  signing_ = true;
  signingText_ = check_->trimmed;
  // the text the device signs stays the text on screen until it answers
  text_->set_editable(false);
  errorLine_->set_visible(false);
  UpdateSignButton();
  StartSign();
}

void Top200SignSheet::StartSign() {
  const std::string trimmed = signingText_;
  auto alive = alive_;
  const bool started = host_.RequestSignSnFleetBinding(
      trimmed, [this, alive, trimmed](std::string signature, std::string error) {
        if (!*alive) return;
        signing_ = false;
        text_->set_editable(true);
        // the field changed meanwhile: this reply is for text no longer shown
        if (!check_ || check_->trimmed != trimmed || signingText_ != trimmed) {
          UpdateSignButton();
          return;
        }
        const top200::SignOutcome outcome = top200::FinishSign(trimmed, signature, error);
        if (!outcome.ok) {
          ShowError(Format(T_("top200_sign_error_failed", "The binding could not be signed: {}"),
                           outcome.detail));
          UpdateSignButton();
          return;
        }
        payload_ = outcome.payload;
        payloadValue_->set_text(payload_);
        doneBox_->set_visible(true);
        UpdateSignButton();
      });
  if (started) return;
  // the worker is busy: the tap stays taken (spinner on, field locked) and
  // the sign is asked again, as RequestIdentity does
  signRetry_.disconnect();
  signRetry_ = Glib::signal_timeout().connect(
      [this]() -> bool {
        StartSign();
        return false;
      },
      kWorkerRetryMs);
}

void Top200SignSheet::FlashCopied(Gtk::Button& button, const Glib::ustring& label,
                                  sigc::connection& reset) {
  button.set_label(T_("copied", "Copied!"));
  reset.disconnect();
  reset = Glib::signal_timeout().connect(
      [&button, label]() -> bool {
        button.set_label(label);
        return false;
      },
      kCopiedResetMs);
}

}  // namespace urnw
