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
  Loading,      // no code: nothing under the label
  Code,         // the code pill and share
  Unavailable,  // not shown yet
};

// The last code GET /account/referral-code returned. A failed read keeps the
// last value; the 30s poll retries.
class ReferralCodeFetch {
 public:
  // Logout: nothing read for the next network yet.
  void Reset() { code_.clear(); }
  // Nothing reads again on demand yet.
  void Retry() {}
  void Succeed(std::string code) { code_ = std::move(code); }
  // The last value stands. Returns whether the panel must repaint.
  bool Fail() { return false; }

  const std::string& Code() const { return code_; }

  ReferralCodeView View() const {
    return code_.empty() ? ReferralCodeView::Loading : ReferralCodeView::Code;
  }

 private:
  std::string code_;
};

}  // namespace urnw
