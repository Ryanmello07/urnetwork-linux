// SPDX-License-Identifier: MPL-2.0
#include "BrowserApprovalSheet.hpp"

#include <utility>

#include <gio/gio.h>

#include "Config.hpp"
#include "DisplayText.hpp"
#include "Formatters.hpp"
#include "I18n.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

// Every string here is a key the localization store tags linux, with the
// store's English beside it. There is no check code and no string for one.
Glib::ustring TitleText() { return T_("adv_browser_sign_in_title", "Browser sign-in"); }

// The action ("Continue in browser" in the contract): the approval page, in the
// browser.
Glib::ustring ActionText() { return T_("adv_open_ur_io", "Open ur.io"); }

Glib::ustring BodyText() {
  return T_("adv_browser_sign_in_body",
            "Sign in at ur.io in your browser — your password manager and existing Google or "
            "Apple sessions work there. Then paste a one-time auth code from any signed-in "
            "device (Account → Create auth code).");
}

// Where the browser goes, as plain text from the build constant: never
// anything a link carried.
Gtk::Label* MakeHostLabel() {
  auto* host = Gtk::make_managed<Gtk::Label>(std::string(config::kApprovalHost));
  host->add_css_class("dim-label");
  host->add_css_class("caption");
  return host;
}

}  // namespace

BrowserApprovalSheet::BrowserApprovalSheet(Gtk::Window& parent, SdkHost& host,
                                           Controller controller)
    : host_(host), controller_(std::move(controller)) {
  EnsureDrawerCss();
  set_title(TitleText());
  set_transient_for(parent);
  set_modal(true);
  set_default_size(420, -1);
  set_resizable(false);
  set_hide_on_close(true);
  // Escape asks to close, as the window's close button does, so a sheet that
  // is registering the person's yes refuses it the same way (AddEscapeToClose
  // would hide the sheet past that refusal).
  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) -> bool {
        if (keyval != GDK_KEY_Escape) return false;
        close();
        return true;
      },
      false);
  add_controller(keys);
  // Closing is refused only while a confirmed sign-in registers: the
  // credential has left the hold, and nothing is left to cancel.
  signal_close_request().connect(
      [this]() -> bool { return page_ == Page::SigningIn && confirmed_; }, false);
  hidden_ = signal_hide().connect([this] { OnClosed(); });
  BuildUi();
  ShowPage(Page::Start);
}

BrowserApprovalSheet::~BrowserApprovalSheet() {
  // a sheet destroyed with the window (the app quits) says nothing on its way
  // out: the window's ShutdownApproval has ended the attempt
  hidden_.disconnect();
}

void BrowserApprovalSheet::BuildUi() {
  auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  root->set_margin(24);
  set_child(*root);
  pages_ = Gtk::make_managed<Gtk::Stack>();
  pages_->set_vhomogeneous(false);
  pages_->add(*BuildStart(), "start");
  pages_->add(*BuildWaiting(), "waiting");
  pages_->add(*BuildSigningIn(), "signing-in");
  pages_->add(*BuildConfirm(), "confirm");
  pages_->add(*BuildMessage(), "message");
  root->append(*pages_);
}

Gtk::Box* BrowserApprovalSheet::MakeButtonRow() {
  auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  row->set_margin_top(16);
  row->set_halign(Gtk::Align::END);
  return row;
}

Gtk::Widget* BrowserApprovalSheet::BuildStart() {
  auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);

  // the one action of the page, and where it goes beside it
  auto* action = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  actionButton_ = Gtk::make_managed<Gtk::Button>(ActionText());
  actionButton_->add_css_class("suggested-action");
  actionButton_->signal_clicked().connect([this] { StartApproval(); });
  action->append(*actionButton_);
  auto* host = MakeHostLabel();
  host->set_valign(Gtk::Align::CENTER);
  action->append(*host);
  page->append(*action);

  startNote_ = Gtk::make_managed<Gtk::Label>();
  startNote_->add_css_class("ur-error-text");
  startNote_->set_wrap(true);
  startNote_->set_xalign(0);
  startNote_->set_visible(false);
  page->append(*startNote_);

  auto* either = Gtk::make_managed<Gtk::Label>(T_("or", "or"));
  either->add_css_class("dim-label");
  page->append(*either);

  auto* body = Gtk::make_managed<Gtk::Label>(BodyText());
  body->add_css_class("dim-label");
  body->set_wrap(true);
  body->set_xalign(0);
  page->append(*body);

  // The field holds a credential: masked, kept out of the input method's
  // learning as a password is, and emptied whenever the page is left.
  pasteEntry_ = Gtk::make_managed<Gtk::Entry>();
  pasteEntry_->add_css_class("ur-input");
  pasteEntry_->set_visibility(false);
  pasteEntry_->set_input_purpose(Gtk::InputPurpose::PASSWORD);
  pasteEntry_->set_placeholder_text(T_("auth_code", "Auth code"));
  pasteEntry_->signal_changed().connect(
      [this] { pasteSignIn_->set_sensitive(!TrimWhitespace(pasteEntry_->get_text()).empty()); });
  pasteEntry_->signal_activate().connect([this] { SubmitPastedCode(); });
  page->append(*pasteEntry_);

  auto* buttons = MakeButtonRow();
  auto* cancel = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  cancel->signal_clicked().connect([this] { close(); });
  buttons->append(*cancel);
  pasteSignIn_ = Gtk::make_managed<Gtk::Button>(T_("sign_in", "Sign in"));
  pasteSignIn_->set_sensitive(false);
  pasteSignIn_->signal_clicked().connect([this] { SubmitPastedCode(); });
  buttons->append(*pasteSignIn_);
  page->append(*buttons);
  return page;
}

Gtk::Widget* BrowserApprovalSheet::BuildWaiting() {
  auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  auto* spinner = Gtk::make_managed<Gtk::Spinner>();
  spinner->set_size_request(36, 36);
  spinner->set_halign(Gtk::Align::CENTER);
  spinner->start();
  page->append(*spinner);
  auto* title = Gtk::make_managed<Gtk::Label>(T_("waiting_for_approval", "Waiting for approval"));
  title->add_css_class("title-3");
  title->set_wrap(true);
  title->set_justify(Gtk::Justification::CENTER);
  page->append(*title);
  page->append(*MakeHostLabel());

  auto* buttons = MakeButtonRow();
  // the same request again while the page would still accept it (BeginApproval)
  againButton_ = Gtk::make_managed<Gtk::Button>(ActionText());
  againButton_->signal_clicked().connect([this] { StartApproval(); });
  buttons->append(*againButton_);
  waitingCancel_ = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  waitingCancel_->signal_clicked().connect([this] { close(); });
  buttons->append(*waitingCancel_);
  page->append(*buttons);
  return page;
}

Gtk::Widget* BrowserApprovalSheet::BuildSigningIn() {
  auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  auto* spinner = Gtk::make_managed<Gtk::Spinner>();
  spinner->set_size_request(36, 36);
  spinner->set_halign(Gtk::Align::CENTER);
  spinner->set_margin_top(12);
  spinner->start();
  const Glib::ustring label = T_("sign_in", "Sign in");
  gtk_accessible_update_property(GTK_ACCESSIBLE(spinner->gobj()), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                 label.c_str(), -1);
  page->append(*spinner);

  auto* buttons = MakeButtonRow();
  signingInCancel_ = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  signingInCancel_->signal_clicked().connect([this] { close(); });
  buttons->append(*signingInCancel_);
  page->append(*buttons);
  return page;
}

Gtk::Widget* BrowserApprovalSheet::BuildConfirm() {
  auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  auto* caption = Gtk::make_managed<Gtk::Label>(T_("network_name_label", "Network name"));
  caption->add_css_class("dim-label");
  caption->add_css_class("caption");
  caption->set_xalign(0);
  page->append(*caption);
  // the name of the network the answer would sign this computer in to,
  // sanitized by OnRedeemed
  networkText_ = Gtk::make_managed<Gtk::Label>();
  networkText_->add_css_class("title-2");
  networkText_->set_wrap(true);
  networkText_->set_xalign(0);
  networkText_->set_selectable(false);
  page->append(*networkText_);

  auto* buttons = MakeButtonRow();
  confirmCancel_ = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  confirmCancel_->signal_clicked().connect([this] { close(); });
  buttons->append(*confirmCancel_);
  confirmSignIn_ = Gtk::make_managed<Gtk::Button>(T_("sign_in", "Sign in"));
  confirmSignIn_->add_css_class("suggested-action");
  confirmSignIn_->signal_clicked().connect([this] {
    if (page_ == Page::Confirm) Confirm();
  });
  buttons->append(*confirmSignIn_);
  page->append(*buttons);
  return page;
}

Gtk::Widget* BrowserApprovalSheet::BuildMessage() {
  auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  messageText_ = Gtk::make_managed<Gtk::Label>();
  messageText_->set_wrap(true);
  messageText_->set_xalign(0);
  page->append(*messageText_);

  auto* buttons = MakeButtonRow();
  auto* cancel = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  cancel->signal_clicked().connect([this] { close(); });
  buttons->append(*cancel);
  // back to the start: the next attempt is the person's click
  tryAgain_ = Gtk::make_managed<Gtk::Button>(T_("try_again", "Try again"));
  tryAgain_->add_css_class("suggested-action");
  tryAgain_->signal_clicked().connect([this] { ShowPage(Page::Start); });
  buttons->append(*tryAgain_);
  page->append(*buttons);
  return page;
}

void BrowserApprovalSheet::ShowPage(Page page) {
  // the paste field holds a credential: what a page leaves behind is emptied
  if (page != Page::Start) pasteEntry_->set_text("");
  page_ = page;
  switch (page) {
    case Page::Start:
      startNote_->set_visible(false);
      set_title(TitleText());
      unset_default_widget();
      pages_->set_visible_child("start");
      break;
    case Page::Waiting:
      set_title(TitleText());
      unset_default_widget();
      pages_->set_visible_child("waiting");
      break;
    case Page::SigningIn:
      // once the person has said yes there is nothing left to cancel
      signingInCancel_->set_visible(!confirmed_);
      set_title(TitleText());
      unset_default_widget();
      pages_->set_visible_child("signing-in");
      break;
    case Page::Confirm:
      // no default: a stray Enter is not the person's yes
      set_title(T_("sign_in", "Sign in"));
      unset_default_widget();
      pages_->set_visible_child("confirm");
      confirmCancel_->grab_focus();
      break;
    case Page::TimedOut:
    case Page::Failed:
      set_title(TitleText());
      pages_->set_visible_child("message");
      set_default_widget(*tryAgain_);
      tryAgain_->grab_focus();
      break;
  }
}

void BrowserApprovalSheet::ShowStartNote(const Glib::ustring& message) {
  startNote_->set_text(message);
  startNote_->set_visible(true);
}

void BrowserApprovalSheet::ShowFailed(const Glib::ustring& message) {
  messageText_->remove_css_class("dim-label");
  messageText_->add_css_class("ur-error-text");
  messageText_->set_text(message);
  ShowPage(Page::Failed);
}

void BrowserApprovalSheet::ShowConfirm(const std::string& networkName) {
  networkText_->set_text(networkName);
  ShowPage(Page::Confirm);
}

void BrowserApprovalSheet::ShowTimedOut() {
  // only a sheet that is waiting is told: anything else is on a page of its own
  if (closed_ || page_ != Page::Waiting) return;
  messageText_->remove_css_class("ur-error-text");
  messageText_->add_css_class("dim-label");
  messageText_->set_text(T_("sso_sign_in_timed_out", "Sign-in timed out. Please try again."));
  ShowPage(Page::TimedOut);
}

void BrowserApprovalSheet::ShowDenied() {
  // the person cancelled on the page: nothing was shared and nothing is signed
  // in, so the sheet is back where it began
  if (closed_ || (page_ != Page::Waiting && page_ != Page::Start)) return;
  ShowPage(Page::Start);
}

void BrowserApprovalSheet::StartApproval() {
  if (closed_) return;
  // A second click reads the live attempt back while the page would still
  // accept its request; otherwise this starts one, and supersedes whatever
  // waited for the browser or a wallet. The View is a secret until the request
  // it builds has been opened, so it is wiped as soon as the request exists.
  std::optional<approval::Slot::View> view = controller_.begin ? controller_.begin() : std::nullopt;
  std::string url = view && controller_.requestUrl ? controller_.requestUrl(*view) : std::string();
  if (view) {
    approval::Wipe(view->state);
    approval::Wipe(view->publicKey);
  }
  if (url.empty()) {
    // an attempt that cannot be opened is not left alive
    if (view && controller_.cancel) controller_.cancel(approval::ClearReason::Cancel);
    g_warning("browser sign-in sheet: the approval could not be started; the paste flow stays");
    ShowPage(Page::Start);
    ShowStartNote(T_("something_went_wrong", "Something went wrong."));
    return;
  }
  // The launcher's error is not logged: its text may quote the request.
  GError* error = nullptr;
  GdkAppLaunchContext* context = gdk_display_get_app_launch_context(get_display()->gobj());
  const bool launched =
      g_app_info_launch_default_for_uri(url.c_str(), G_APP_LAUNCH_CONTEXT(context), &error);
  g_object_unref(context);
  g_clear_error(&error);
  approval::Wipe(url);
  if (launched) {
    ShowPage(Page::Waiting);
    g_message("browser sign-in sheet: the browser was opened on the approval page");
    return;
  }
  // No Copy link on Linux (the clipboard keeps no secret out of its history):
  // the attempt ends, and the paste field is what is left.
  if (controller_.cancel) controller_.cancel(approval::ClearReason::Cancel);
  g_warning("browser sign-in sheet: the browser could not be opened; the attempt ended");
  ShowPage(Page::Start);
  ShowStartNote(T_("sso_browser_open_failed", "Could not open the browser. Please try again."));
}

void BrowserApprovalSheet::BeginRedeem(std::string code) {
  if (!Open()) {
    approval::Wipe(code);
    return;
  }
  confirmed_ = false;
  ShowPage(Page::SigningIn);
  // An SDK thread answers, and the sheet is touched on the main thread only. An
  // answer that finds the sheet gone has nothing the person said yes to: the
  // network it redeemed is dropped.
  std::weak_ptr<int> life = life_;
  SdkHost* host = &host_;
  host_.RedeemApprovedCode(code, [this, life, host](SdkHost::ApprovedNetwork answer) {
    PostToMain([this, life, host, answer = std::move(answer)]() mutable {
      if (!life.expired()) {
        OnRedeemed(std::move(answer));
      } else if (answer.ok) {
        host->DiscardApprovedNetwork();
      }
    });
  });
  approval::Wipe(code);
}

void BrowserApprovalSheet::OnRedeemed(SdkHost::ApprovedNetwork answer) {
  if (closed_) {
    // closed while the code was out: nothing here has the person's yes
    if (answer.ok) host_.DiscardApprovedNetwork();
    return;
  }
  if (!answer.ok) {
    // never retried: a spent or refused code is spent, and the server answers
    // an invalid, an expired and a used one the same way
    g_warning("browser sign-in sheet: the approval could not be redeemed");
    ShowFailed(answer.failure == SdkHost::ApprovedNetwork::Failure::Transport
                   ? T_("something_went_wrong", "Something went wrong.")
                   : T_("auth_code_error",
                        "Error authenticating code. Please try again or generate a new code."));
    return;
  }
  // The name is the network's own, as the credential carries it: filtered like
  // every external name the window shows. A name with nothing left to show is
  // nothing the person can say yes to.
  const std::string name = SanitizeExternalDisplayText(answer.networkName);
  if (name.empty()) {
    host_.DiscardApprovedNetwork();
    g_warning("browser sign-in sheet: the approval named no network the person could recognise");
    ShowFailed(T_("auth_code_error",
                  "Error authenticating code. Please try again or generate a new code."));
    return;
  }
  ShowConfirm(name);
}

void BrowserApprovalSheet::Confirm() {
  confirmed_ = true;
  ShowPage(Page::SigningIn);
  std::weak_ptr<int> life = life_;
  host_.ConfirmApprovedNetwork([this, life](AuthResult result) {
    PostToMain([this, life, result = std::move(result)] {
      if (!life.expired()) OnConfirmed(result);
    });
  });
}

void BrowserApprovalSheet::OnConfirmed(const AuthResult& result) {
  if (closed_) return;  // the sign-in goes on without the sheet, and is told as any sign-in is
  if (result.ok) {
    // the auth state has swapped the window for Home; the tunnel starts as
    // after every sign-in
    if (controller_.signedIn) controller_.signedIn();
    Finish();
    return;
  }
  // the server's own text, as every other sign-in failure shows it; the code is
  // spent, so the person starts again
  g_warning("browser sign-in sheet: the device could not be registered");
  ShowFailed(result.error.empty() ? T_("something_went_wrong", "Something went wrong.")
                                  : Glib::ustring(result.error));
}

void BrowserApprovalSheet::SubmitPastedCode() {
  std::string code = TrimWhitespace(pasteEntry_->get_text());
  pasteEntry_->set_text("");
  if (code.empty()) return;
  // the sheet goes first, as the auth code dialog does: the sign-in reports on
  // the sign-in page
  Finish();
  if (controller_.signInWithCode) controller_.signInWithCode(std::move(code));
}

void BrowserApprovalSheet::OnClosed() {
  if (closed_) return;
  closed_ = true;
  pasteEntry_->set_text("");
  // a network redeemed for a confirm that never came is not signed in to
  if (page_ == Page::Confirm && !confirmed_) {
    host_.DiscardApprovedNetwork();
    g_message("browser sign-in sheet: closed without a yes; the redeemed network was dropped");
  }
  // the person closing the sheet cancels the attempt behind it
  if (controller_.cancel) controller_.cancel(approval::ClearReason::Cancel);
}

void BrowserApprovalSheet::Finish() {
  if (!closed_) set_visible(false);
}

}  // namespace urnw
