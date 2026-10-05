// SPDX-License-Identifier: MPL-2.0
#include "ReferralCodeBox.hpp"

#include <glibmm/main.h>

#include "I18n.hpp"
#include "ReferralRoyalty.hpp"
#include "Ui.hpp"

namespace urnw {

ReferralCodeBox::ReferralCodeBox(SdkHost& host)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 8), host_(host) {
  auto* caption = Gtk::make_managed<Gtk::Label>(
      T_("referral_code_optional", "Referral code (optional)"));
  caption->add_css_class("ur-input-label");
  caption->set_xalign(0);
  caption->set_wrap(true);
  append(*caption);

  code_ = Gtk::make_managed<Gtk::Entry>();
  code_->add_css_class("ur-input");
  code_->set_placeholder_text(T_("enter_a_bonus_referral_code", "Enter a bonus referral code"));
  code_->set_hexpand(true);
  code_->signal_changed().connect([this] { OnChanged(); });
  code_->signal_activate().connect([this] { CheckNow(); });
  append(*code_);

  supporting_ = Gtk::make_managed<Gtk::Label>();
  supporting_->add_css_class("caption");
  supporting_->add_css_class("ur-error-text");
  supporting_->set_xalign(0);
  supporting_->set_wrap(true);
  supporting_->set_visible(false);
  append(*supporting_);

  // referral accepted: the gold king-frog line (referral royalty, matching
  // the ur.io referral panel and the android/apple gold chips)
  appliedRow_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  auto* appliedLabel = Gtk::make_managed<Gtk::Label>();
  appliedLabel->set_markup("<span foreground='" + HexForMarkup(kReferralGoldLight) + "'>" +
                           Glib::Markup::escape_text(
                               T_("referral_bonus_applied_2", "Referral Bonus applied")) +
                           "</span>");
  appliedLabel->add_css_class("caption");
  appliedRow_->append(*appliedLabel);
  appliedRow_->set_visible(false);
  append(*appliedRow_);
}

ReferralCodeBox::~ReferralCodeBox() {
  *alive_ = false;
  checkTimer_.disconnect();
}

void ReferralCodeBox::Reset() {
  checkTimer_.disconnect();
  entry_.Reset();
  code_->set_text("");  // an unchanged empty code: no edit, no check
  ApplyVerdict();
}

void ReferralCodeBox::OnChanged() {
  if (on_edit) on_edit();
  checkTimer_.disconnect();
  const bool check = entry_.Edit(code_->get_text());
  ApplyVerdict();
  if (check) {
    checkTimer_ = Glib::signal_timeout().connect(
        [this]() -> bool {
          CheckNow();
          return false;  // one shot
        },
        kReferralCheckDelayMs);
  }
}

void ReferralCodeBox::CheckNow() {
  checkTimer_.disconnect();
  const auto edit = entry_.BeginCheck();
  if (!edit) return;
  ApplyVerdict();
  auto alive = alive_;
  const uint64_t asked = *edit;
  host_.ValidateReferralCode(entry_.Code(), [this, alive, asked](bool ok, bool valid, bool capped) {
    PostToMain([this, alive, asked, ok, valid, capped] {
      if (!*alive) return;
      // the text changed while the check was out: its answer is stale
      if (entry_.Answer(asked, ok, valid, capped)) ApplyVerdict();
    });
  });
}

void ReferralCodeBox::ApplyVerdict() {
  const char* error = nullptr;
  switch (entry_.Verdict()) {
    case ReferralCodeVerdict::Invalid:
      error = T_("invalid_referral_code", "This code is not valid");
      break;
    case ReferralCodeVerdict::Capped:
      error = T_("referral_code_capped", "This code has been used up");
      break;
    case ReferralCodeVerdict::CheckFailed:
      // the check itself failed (no response, or the api refused the call):
      // say so instead of calling a code the server never judged
      error = T_("something_went_wrong", "Something went wrong.");
      break;
    case ReferralCodeVerdict::Unchecked:
    case ReferralCodeVerdict::Checking:
    case ReferralCodeVerdict::Valid:
      break;
  }
  supporting_->set_text(error ? error : "");
  supporting_->set_visible(error != nullptr);
  appliedRow_->set_visible(entry_.Verdict() == ReferralCodeVerdict::Valid);
}

}  // namespace urnw
