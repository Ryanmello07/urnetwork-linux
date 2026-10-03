// The referral code read behind the referral panel (ReferralPanel.hpp), decided
// pure so tests/ReferralCodeStateTest.cpp can pin it without GTK or the SDK.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <utility>

namespace urnw {

// What the referral panel shows where the code goes.
enum class ReferralCodeView {
  Loading,      // no answer yet: nothing under the label
  Code,         // the code pill and share
  Unavailable,  // answered without a code (a failed read): the error and Try again
};

// The last code GET /account/referral-code returned and whether any answer has
// arrived since the read was (re)started. A failed background poll keeps a
// code already shown; with no code the panel says the read failed instead of
// leaving an empty space under "Your referral code".
class ReferralCodeFetch {
 public:
  // Logout: nothing read for the next network yet.
  void Reset() {
    code_.clear();
    answered_ = false;
  }
  // The user asked to read again: back to Loading until the answer lands.
  void Retry() { answered_ = false; }
  // An empty code is a read that answered without one.
  void Succeed(std::string code) {
    code_ = std::move(code);
    answered_ = true;
  }
  // The last reading stands. Returns whether the panel must repaint: a
  // failed read changes what a panel with no code shows.
  bool Fail() {
    answered_ = true;
    return true;
  }

  const std::string& Code() const { return code_; }

  ReferralCodeView View() const {
    if (!code_.empty()) return ReferralCodeView::Code;
    return answered_ ? ReferralCodeView::Unavailable : ReferralCodeView::Loading;
  }

 private:
  std::string code_;
  bool answered_ = false;
};

}  // namespace urnw
