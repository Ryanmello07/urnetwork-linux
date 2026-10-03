// What the verify page says after the server was asked to send a
// verification code (login with password, network create, resend).
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
  int64_t minutes = 0;  // RateLimited
  std::string message;  // ServerMessage
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
    return notice;
  }
  if (code.empty()) {
    return notice;  // sent
  }
  if (code == kVerifySendErrorCodeRateLimited && 0 < retryAfterSeconds) {
    notice.kind = VerifySendNoticeKind::RateLimited;
    notice.minutes = (retryAfterSeconds + 59) / 60;
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

}  // namespace urnw
