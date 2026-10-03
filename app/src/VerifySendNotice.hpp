// What the verify page says after the server was asked to send a
// verification code (login with password, network create, resend), and what
// the password reset UIs say after asking for a reset link (the server reports
// a link it did not send with the same AuthVerifySendError codes).
//
// The server reports a code it did not send as AuthVerifySendError
// (verification_required.send_error, or AuthVerifySendResult.error when the
// request sets result_errors): code "verify_rate_limited" with the seconds
// until a new code can be requested, or "verify_send_failed". Only an answer
// with no error and no transport error may say a code was sent.
//
// Header-only and free of GTK and the SDK so the unit tests build anywhere.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>

namespace urnw {

inline constexpr const char* kVerifySendErrorCodeSendFailed = "verify_send_failed";
inline constexpr const char* kVerifySendErrorCodeRateLimited = "verify_rate_limited";

enum class VerifySendNoticeKind {
  Sent,           // verification_code_sent
  RateLimited,    // verify_code_rate_limited, plural on minutes
  SendFailed,     // error_sending_verification_code
  ServerMessage,  // the server's message, unlocalized
};

struct VerifySendNotice {
  VerifySendNoticeKind kind = VerifySendNoticeKind::Sent;
  int64_t minutes = 0;            // RateLimited
  int64_t retryAfterSeconds = 0;  // RateLimited
  std::string message;            // ServerMessage
  // SendFailed because the request itself failed, not the server's answer
  bool transportError = false;
};

// transportError is any request error (including the HTTP 429 / 502 an older
// server or a request without result_errors answers with); code, message and
// retryAfterSeconds are the AuthVerifySendError fields, empty / zero when the
// server reported none.
inline VerifySendNotice DecideVerifySendNotice(bool transportError, const std::string& code,
                                               const std::string& message,
                                               int64_t retryAfterSeconds) {
  VerifySendNotice notice;
  if (transportError) {
    notice.kind = VerifySendNoticeKind::SendFailed;
    notice.transportError = true;
    return notice;
  }
  if (code.empty()) {
    return notice;  // sent
  }
  if (code == kVerifySendErrorCodeRateLimited && 0 < retryAfterSeconds) {
    notice.kind = VerifySendNoticeKind::RateLimited;
    notice.minutes = (retryAfterSeconds + 59) / 60;
    notice.retryAfterSeconds = retryAfterSeconds;
    return notice;
  }
  if (code != kVerifySendErrorCodeSendFailed && !message.empty()) {
    notice.kind = VerifySendNoticeKind::ServerMessage;
    notice.message = message;
    return notice;
  }
  notice.kind = VerifySendNoticeKind::SendFailed;
  return notice;
}

// After a rate limit, a new code (or reset link) cannot be requested until
// retry_after_seconds have passed: Resend / Send stays disabled until then and
// the rate-limit notice counts the minutes down. Times are seconds on a
// monotonic clock the caller supplies, so tests inject them.
class ResendCooldown {
 public:
  // Starts the cooldown for a rate-limited notice decided at `now`; any other
  // notice clears it.
  void Start(const VerifySendNotice& notice, int64_t now) {
    retryAt_ = notice.kind == VerifySendNoticeKind::RateLimited && 0 < notice.retryAfterSeconds
                   ? now + notice.retryAfterSeconds
                   : 0;
  }

  void Clear() { retryAt_ = 0; }

  int64_t RemainingSeconds(int64_t now) const {
    return now < retryAt_ ? retryAt_ - now : 0;
  }

  bool CanResend(int64_t now) const { return RemainingSeconds(now) == 0; }

  // The rate-limit notice for the time left at `now`, minutes rounded up and at
  // least one. Only meaningful while !CanResend(now).
  VerifySendNotice NoticeAt(int64_t now) const {
    const int64_t remaining = RemainingSeconds(now);
    VerifySendNotice notice;
    notice.kind = VerifySendNoticeKind::RateLimited;
    notice.retryAfterSeconds = remaining;
    notice.minutes = remaining < 60 ? 1 : (remaining + 59) / 60;
    return notice;
  }

 private:
  int64_t retryAt_ = 0;
};

}  // namespace urnw
