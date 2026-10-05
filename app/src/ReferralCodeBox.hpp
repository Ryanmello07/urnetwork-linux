// The optional referral code field above Continue (support inbox 1698),
// shared by the create-network page and the instant account step: a caption,
// the entry, the error line and the gold "Referral Bonus applied" row. Typing
// checks the code after a pause (the Windows sign-up's bonus code box), Enter
// checks it at once, an answer for text that has changed since is dropped
// (ReferralCodeField.hpp), and the create call carries CreateCode().
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <memory>
#include <string>

#include <gtkmm.h>

#include "ReferralCodeField.hpp"
#include "SdkHost.hpp"

namespace urnw {

class ReferralCodeBox : public Gtk::Box {
 public:
  explicit ReferralCodeBox(SdkHost& host);
  ~ReferralCodeBox() override;

  // The code the create call carries ("" for none).
  std::string CreateCode() const { return entry_.CreateCode(); }
  // A fresh form: empty, nothing pending.
  void Reset();

  // Every edit of the code (the page clears its error line).
  std::function<void()> on_edit;

 private:
  void OnChanged();
  void CheckNow();
  void ApplyVerdict();

  SdkHost& host_;
  ReferralCodeEntry entry_;
  // the pending check after a typing pause
  sigc::connection checkTimer_;
  // SDK completions arrive after the widget may be gone
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  Gtk::Entry* code_ = nullptr;
  Gtk::Label* supporting_ = nullptr;
  Gtk::Box* appliedRow_ = nullptr;
};

}  // namespace urnw
