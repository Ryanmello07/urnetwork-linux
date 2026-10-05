// The always-visible, optional referral code field at sign-up (support inbox
// 1698), as the Windows sign-up shows it above Continue. It used to sit behind
// a flat "Add referral code" toggle with an Apply button, and invitees missed
// it. Pure (no GTK, no SDK) so tests/ReferralCodeFieldTest.cpp pins what the
// typed text means; ReferralCodeBox is the widget.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace urnw {

// What the server has said about the code in the field.
enum class ReferralCodeVerdict {
  Unchecked,    // typed, not asked about yet (or the field is empty)
  Checking,
  Valid,
  Invalid,
  Capped,       // valid, but the code has reached its referral cap
  CheckFailed,  // the check did not answer (no response, a refused call)
};

// The pause after typing before the code is checked (as on Windows).
inline constexpr unsigned kReferralCheckDelayMs = 400;

// The code as the server reads it: trimmed, upper case (the server
// upper-cases codes).
inline std::string NormalizeReferralCode(const std::string& input) {
  const char* space = " \t\r\n\f\v";
  const auto first = input.find_first_not_of(space);
  if (first == std::string::npos) return std::string();
  const auto last = input.find_last_not_of(space);
  std::string code = input.substr(first, last - first + 1);
  for (char& c : code) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return code;
}

// The verdict for a check's answer; `ok` is false when the check did not
// answer.
inline ReferralCodeVerdict ReferralCodeVerdictOf(bool ok, bool valid, bool capped) {
  if (!ok) return ReferralCodeVerdict::CheckFailed;
  if (capped) return ReferralCodeVerdict::Capped;
  return valid ? ReferralCodeVerdict::Valid : ReferralCodeVerdict::Invalid;
}

// The code the create call carries ("" for none): the normalized code, unless
// the server already said it is invalid or used up. A code that is still
// unchecked (Continue right after typing) or whose check did not answer goes
// along: the server checks it again on create and ignores a bad one, so a
// typed code is never dropped without a word.
inline std::string ReferralCodeForCreate(const std::string& input, ReferralCodeVerdict verdict) {
  if (verdict == ReferralCodeVerdict::Invalid || verdict == ReferralCodeVerdict::Capped) {
    return std::string();
  }
  return NormalizeReferralCode(input);
}

// The Referrals page's referral network row: once the lookup answered that
// no network is linked, the row is the "Add referral code" action itself
// instead of "None" (a friend who missed the field at sign-up adds it there).
inline bool ReferralNetworkOffersAddCode(bool answeredNone, const std::string& networkName) {
  return answeredNone && networkName.empty();
}

// The field's checks. Every change of the code is an edit; a check carries
// the edit it was asked for, and an answer for an older edit is dropped.
class ReferralCodeEntry {
 public:
  // The text changed. Returns whether a check should be scheduled: the code
  // is a different, non-empty one.
  bool Edit(const std::string& text) {
    std::string code = NormalizeReferralCode(text);
    if (code == code_) return false;
    code_ = std::move(code);
    ++edit_;
    verdict_ = ReferralCodeVerdict::Unchecked;
    return !code_.empty();
  }

  // A check goes out for the current code: the edit it answers, or nothing
  // when there is no code or a check for it is already out.
  std::optional<uint64_t> BeginCheck() {
    if (code_.empty() || verdict_ == ReferralCodeVerdict::Checking) return std::nullopt;
    verdict_ = ReferralCodeVerdict::Checking;
    return edit_;
  }

  // A check answered. Returns false, changing nothing, for an answer about an
  // older edit.
  bool Answer(uint64_t edit, bool ok, bool valid, bool capped) {
    if (edit != edit_) return false;
    verdict_ = ReferralCodeVerdictOf(ok, valid, capped);
    return true;
  }

  // A fresh form: no code, nothing pending (answers still out are dropped).
  void Reset() {
    code_.clear();
    ++edit_;
    verdict_ = ReferralCodeVerdict::Unchecked;
  }

  const std::string& Code() const { return code_; }
  ReferralCodeVerdict Verdict() const { return verdict_; }
  // The code the create call carries ("" for none).
  std::string CreateCode() const { return ReferralCodeForCreate(code_, verdict_); }

 private:
  std::string code_;
  uint64_t edit_ = 0;
  ReferralCodeVerdict verdict_ = ReferralCodeVerdict::Unchecked;
};

}  // namespace urnw
