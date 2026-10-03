// The sign-up / verify / password-reset stack pages (ports of the apple
// Authenticate/ views):
//
//   * CreateNetworkPage — CreateNetworkView/ViewModel: network name with
//     debounced availability through the SDK's shared
//     NetworkNameValidationViewController, password (12+ chars), the terms
//     switch, and an optional bonus referral code validated with
//     Api::validateReferralCode. Three modes share the form the way the mac
//     view does: password sign-up (email + password), wallet sign-up (the
//     wallet_auth captured from a Solana/Bittensor sign-in that had no
//     network — name + terms only), and the same for an SSO identity.
//   * VerifyPage — CreateNetworkVerifyView: the 6-digit code entry with
//     auto-submit, and resend with the 15s cooldown. Also the landing for the
//     password-login "needs verification" path that used to dead-end.
//   * ResetPasswordPage — ResetPasswordView: send the reset link, then a sent
//     confirmation.
//
// Pages navigate through std::function callbacks the MainWindow wires up; all
// SDK completions arrive on SDK threads and are marshalled with PostToMain.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <gtkmm.h>

#include "NetworkNameCheck.hpp"
#include "SdkHost.hpp"

namespace urnw {

class CreateNetworkPage : public Gtk::Box {
 public:
  enum class Mode {
    Password,      // full sign-up: email + network name + password
    Wallet,        // wallet sign-in with no network yet: name + terms only
    Sso,           // Google/Apple (the provider's web flow) with no network yet: name + terms only
  };

  explicit CreateNetworkPage(SdkHost& host);
  // the pending name check timer captures this page
  ~CreateNetworkPage() override { nameDebounce_.disconnect(); }

  // Reset the form for a fresh navigation. userAuth prefills the email field
  // (it stays editable — the login page may not have one yet).
  void Configure(Mode mode, const std::string& userAuth);
  // Focus the first field for the configured mode (windows EnterCreateStep
  // parity): the network name.
  void FocusFirstField();

  std::function<void()> on_success;                        // network ready -> start tunnel
  // verification required; notice says whether the code was sent
  std::function<void(std::string userAuth, VerifySendNotice notice)> on_verify;
  std::function<void()> on_back;

 private:
  void BuildUi();
  void OnNetworkNameChanged();
  void OnNameStateChanged(NetworkNameState state);
  void SetNameSupporting(const char* text, const char* cssClass);
  void OnValidateReferral();
  void UpdateFormValid();
  void OnContinue();
  void SetCreating(bool creating);

  SdkHost& host_;
  Mode mode_ = Mode::Password;
  bool creating_ = false;
  bool referralValid_ = false;
  bool referralCapped_ = false;
  bool validatingReferral_ = false;
  // the pending debounce or recheck timer of nameChecker_
  sigc::connection nameDebounce_;
  NetworkNameChecker nameChecker_;
  // invalidates in-flight SDK completions across Configure()
  std::shared_ptr<uint64_t> epoch_ = std::make_shared<uint64_t>(0);

  Gtk::Label* title_ = nullptr;
  Gtk::Entry* email_ = nullptr;
  Gtk::Label* emailCaption_ = nullptr;
  Gtk::Entry* networkName_ = nullptr;
  Gtk::Label* nameSupporting_ = nullptr;
  Gtk::PasswordEntry* password_ = nullptr;
  Gtk::Label* passwordCaption_ = nullptr;
  Gtk::Switch* termsSwitch_ = nullptr;
  // "Periodic product updates": the marketing opt-out at collection, on by
  // default (mmm/onboarding/PLAN.md). Off -> the create carries
  // product_updates=false and signup.optout_changed fires.
  Gtk::Switch* productUpdates_ = nullptr;
  Gtk::Button* referralToggle_ = nullptr;
  Gtk::Revealer* referralRevealer_ = nullptr;
  Gtk::Entry* referralEntry_ = nullptr;
  Gtk::Button* referralApply_ = nullptr;
  Gtk::Label* referralSupporting_ = nullptr;
  Gtk::Box* referralAppliedRow_ = nullptr;
  Gtk::Button* continueBtn_ = nullptr;
  Gtk::Spinner* spinner_ = nullptr;
  Gtk::Label* errorLabel_ = nullptr;
};

// Seconds on the monotonic clock, for ResendCooldown.
int64_t MonotonicSeconds();

// What a password reset UI says for a reset link that was not sent: the
// reset send error, the reset rate limit (plural on minutes), or the server's
// message. Empty for Sent.
std::string ResetSendNoticeText(const VerifySendNotice& notice);

class VerifyPage : public Gtk::Box {
 public:
  explicit VerifyPage(SdkHost& host);
  ~VerifyPage() override;

  void Configure(const std::string& userAuth);
  // The line under the resend row after a code was requested: "a code was
  // sent" in the informational voice, a code that was not sent in the error
  // voice.
  void ShowSendNotice(const VerifySendNotice& notice);

  std::function<void()> on_success;
  std::function<void()> on_back;

 private:
  void BuildUi();
  void Submit();
  void Resend();
  void SetSubmitting(bool submitting);
  void StartResendCooldown();
  void ShowNoticeText(const VerifySendNotice& notice);
  // while rate limited: Resend stays off and the notice counts down
  bool TickRateLimit();

  SdkHost& host_;
  std::string userAuth_;
  bool submitting_ = false;
  bool sending_ = false;
  sigc::connection resendCooldown_;
  ResendCooldown rateLimit_;
  sigc::connection rateLimitTick_;
  std::shared_ptr<uint64_t> epoch_ = std::make_shared<uint64_t>(0);

  Gtk::Label* title_ = nullptr;
  Gtk::Entry* code_ = nullptr;
  Gtk::Spinner* spinner_ = nullptr;
  Gtk::Label* errorLabel_ = nullptr;
  Gtk::Button* resendBtn_ = nullptr;
  Gtk::Label* resendStatus_ = nullptr;
};

class ResetPasswordPage : public Gtk::Box {
 public:
  explicit ResetPasswordPage(SdkHost& host);
  ~ResetPasswordPage() override;

  void Configure(const std::string& userAuth);

  std::function<void()> on_back;

 private:
  void BuildUi();
  void Send();
  // while rate limited: Send stays off and the notice counts down
  bool TickRateLimit();

  SdkHost& host_;
  bool sending_ = false;
  ResendCooldown rateLimit_;
  sigc::connection rateLimitTick_;
  std::shared_ptr<uint64_t> epoch_ = std::make_shared<uint64_t>(0);

  Gtk::Box* formBox_ = nullptr;
  Gtk::Box* sentBox_ = nullptr;
  Gtk::Entry* email_ = nullptr;
  Gtk::Button* sendBtn_ = nullptr;
  Gtk::Spinner* spinner_ = nullptr;
  Gtk::Label* errorLabel_ = nullptr;
  Gtk::Label* sentTo_ = nullptr;
};

}  // namespace urnw
