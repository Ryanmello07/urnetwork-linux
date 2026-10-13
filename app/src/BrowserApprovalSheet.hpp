// The browser sign-in sheet: a sign-in the person approves on the web
// manager's page, where they are already signed in (UrIoApproval.hpp, contract
// section 5), with the paste of a one-time auth code beside it for when the
// browser cannot be used. Built only where the approval is offered
// (MainWindow::ApprovalOffered); everywhere else the Auth code tile opens the
// auth code dialog, as it always has.
//
// Pages:
//   Start      "Open ur.io" with the host it goes to beside it, and the paste
//              field.
//   Waiting    the browser was sent to the page and the app waits for its
//              answer; "Open ur.io" again re-opens the same request while the
//              page would still accept it.
//   SigningIn  the page's answer is being redeemed, or the person's yes is
//              being registered.
//   Confirm    "Sign in" to the network the answer would join, by its name,
//              before anything is registered or stored.
//   TimedOut   the attempt's five minutes are up.
//   Failed     the answer could not be used, or the registration was refused.
//
// What the sheet holds, and does not:
//   * The attempt is MainWindow's (the controller below), not the sheet's. A
//     hide to the tray leaves the sheet, a separate window, as it is.
//   * No link, no code and no pasted text is kept: the request is built,
//     opened and wiped in one call, the code is handed to the host and wiped,
//     and the field is emptied whenever its page is left. The sheet has no
//     string member at all. The credential the redeem brings back is held by
//     SdkHost until the person confirms, and discarded if the sheet closes on
//     the Confirm page, or while the redeem was out.
//   * There is no Copy link (contract section 5, rule 6: Linux does not offer
//     it) and no check code.
//
// Everything runs on the GTK main thread; the SDK's answers are marshalled
// here with PostToMain and dropped once the sheet is gone (life_).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <gtkmm.h>

#include "SdkHost.hpp"
#include "UrIoApproval.hpp"

namespace urnw {

class BrowserApprovalSheet : public Gtk::Window {
 public:
  enum class Page { Start, Waiting, SigningIn, Confirm, TimedOut, Failed };

  // What the sheet asks of the window that owns the attempt.
  struct Controller {
    // the click: the live attempt read back, or a new one; nullopt when none starts
    std::function<std::optional<approval::Slot::View>()> begin;
    // the page's address for an attempt (a secret until it is opened)
    std::function<std::string(const approval::Slot::View&)> requestUrl;
    // the attempt ends (the person closed the sheet, or the browser did not open)
    std::function<void(approval::ClearReason)> cancel;
    // the paste: the window's one sign-in with a code
    std::function<void(std::string)> signInWithCode;
    // the confirmed sign-in succeeded
    std::function<void()> signedIn;
  };

  BrowserApprovalSheet(Gtk::Window& parent, SdkHost& host, Controller controller);
  ~BrowserApprovalSheet() override;

  // False once the sheet has closed: whatever is handed to it then is dropped,
  // and the window opens a fresh sheet for an answer instead.
  bool Open() const { return !closed_; }
  // The page's approved answer, already opened and consumed by the gate: redeem
  // it, then ask the person to confirm which network it would join. Takes the
  // code over and wipes it.
  void BeginRedeem(std::string code);
  // The attempt's five minutes are up (only a sheet that is waiting says so).
  void ShowTimedOut();
  // The person cancelled in the browser: back to the start, with nothing said.
  void ShowDenied();

 private:
  void BuildUi();
  Gtk::Widget* BuildStart();
  Gtk::Widget* BuildWaiting();
  Gtk::Widget* BuildSigningIn();
  Gtk::Widget* BuildConfirm();
  Gtk::Widget* BuildMessage();
  // A page's own row of buttons, at its foot.
  Gtk::Box* MakeButtonRow();

  void ShowPage(Page page);
  void ShowStartNote(const Glib::ustring& message);
  void ShowFailed(const Glib::ustring& message);
  void ShowConfirm(const std::string& networkName);

  // The Start and Waiting pages' "Open ur.io": BeginApproval, the request, the
  // browser.
  void StartApproval();
  void OnRedeemed(SdkHost::ApprovedNetwork answer);
  // The Confirm page's Sign in, and nothing else.
  void Confirm();
  void OnConfirmed(const AuthResult& result);
  void SubmitPastedCode();
  // The sheet hid, by any way: runs once.
  void OnClosed();
  // The one way the sheet hides itself.
  void Finish();

  SdkHost& host_;
  Controller controller_;
  Page page_ = Page::Start;
  // the sheet has closed: every answer still on its way is dropped
  bool closed_ = false;
  // the person said yes on the Confirm page: the credential has left the hold,
  // and the sheet cannot be closed while it registers
  bool confirmed_ = false;
  // the asynchronous answers hold a weak_ptr to it, and drop when it is gone
  std::shared_ptr<int> life_ = std::make_shared<int>(0);
  sigc::connection hidden_;

  Gtk::Stack* pages_ = nullptr;
  Gtk::Button* actionButton_ = nullptr;  // Start: Open ur.io
  Gtk::Label* startNote_ = nullptr;      // Start: why the approval did not start
  Gtk::Entry* pasteEntry_ = nullptr;     // Start: the auth code, masked
  Gtk::Button* pasteSignIn_ = nullptr;   // Start: Sign in with the pasted code
  Gtk::Button* againButton_ = nullptr;   // Waiting: Open ur.io again
  Gtk::Button* waitingCancel_ = nullptr;
  Gtk::Button* signingInCancel_ = nullptr;  // SigningIn: only before a yes
  Gtk::Label* networkText_ = nullptr;       // Confirm: the network's sanitized name
  Gtk::Button* confirmCancel_ = nullptr;
  Gtk::Button* confirmSignIn_ = nullptr;
  Gtk::Label* messageText_ = nullptr;  // TimedOut and Failed
  Gtk::Button* tryAgain_ = nullptr;
};

}  // namespace urnw
