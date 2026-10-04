// "Create an account" for a legacy guest network (GuestConversion.hpp): adds
// an email/phone + password sign-in to the CURRENT network and verifies it
// with the emailed code. The network, its plan and its balance stay; nothing
// signs out. Two pages in one window: the sign-in fields, then the code.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <gtkmm.h>

#include "GuestConversion.hpp"
#include "SdkHost.hpp"
#include "SubscriptionBalance.hpp"

namespace urnw {

// Shows the last code send's outcome under the code field ("" for none): a
// sent code muted, a failure or a counting-down rate limit as an error. Shared
// with Account > Login methods (AccountAddAuthSheet), which verifies the same way.
void ShowVerifySendNotice(Gtk::Label& label, const std::optional<VerifySendNotice>& notice);

// Re-renders once a second while `conversion` waits out a rate limit, so the
// minutes count down and Resend comes back on time.
void TickCooldown(sigc::connection& tick, const GuestConversion& conversion,
                  std::function<void()> render);

class GuestConversionSheet : public Gtk::Window {
 public:
  GuestConversionSheet(Gtk::Window& parent, SdkHost& host, SubscriptionBalanceStore& balance);
  ~GuestConversionSheet() override;

  // Back to the first page and present.
  void Open();

  // The sign-in was added and verified (the window shows the snackbar).
  std::function<void()> on_done;

 private:
  void Render();

  std::unique_ptr<GuestConversionSession> session_;
  std::unique_ptr<GuestConversion> conversion_;

  Gtk::Stack stack_;
  Gtk::Entry* auth_ = nullptr;
  Gtk::PasswordEntry* password_ = nullptr;
  Gtk::Label* signInError_ = nullptr;
  Gtk::Button* add_ = nullptr;
  Gtk::Entry* code_ = nullptr;
  Gtk::Label* codeError_ = nullptr;
  Gtk::Label* notice_ = nullptr;
  Gtk::Button* verify_ = nullptr;
  Gtk::Button* resend_ = nullptr;
  sigc::connection cooldownTick_;
};

}  // namespace urnw
