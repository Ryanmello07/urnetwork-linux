// The verify page must not say a verification code was sent when the server
// did not send one.
//
// The server reports a code it did not send as AuthVerifySendError
// (verification_required.send_error on login with password and network
// create, AuthVerifySendResult.error on a resend that sets result_errors).
// The app ignored it and said "Check your email/phone for a verification
// code." (support inbox 664). The decision is DecideVerifySendNotice; SdkHost
// needs GTK and the SDK, so its wiring is checked by reading SdkHost.cpp.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "VerifySendNotice.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace urnw {
namespace {

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + name);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of the function whose definition starts with `signature`, or ""
// (braces balanced from the first '{' after the signature).
std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t at = source.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t open = source.find('{', at);
  if (open == std::string::npos) return std::string();
  int depth = 0;
  for (size_t i = open; i < source.size(); ++i) {
    if (source[i] == '{') ++depth;
    if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
  }
  return std::string();
}

UR_TEST(verifySendNoErrorIsSent) {
  const auto notice = DecideVerifySendNotice(false, "", "", 0);
  UR_EXPECT_TRUE(notice.kind == VerifySendNoticeKind::Sent);
}

UR_TEST(verifySendTransportErrorIsSendFailed) {
  // an older server answers a failed send with HTTP 502, a rate limit with 429
  const auto notice = DecideVerifySendNotice(true, "", "", 0);
  UR_EXPECT_TRUE(notice.kind == VerifySendNoticeKind::SendFailed);
}

UR_TEST(verifySendFailedIsNotSent) {
  const auto notice =
      DecideVerifySendNotice(false, "verify_send_failed", "Could not send the code.", 0);
  UR_EXPECT_TRUE(notice.kind == VerifySendNoticeKind::SendFailed);
}

UR_TEST(verifySendRateLimitedGivesMinutesRoundedUp) {
  const auto fiveMinutes =
      DecideVerifySendNotice(false, "verify_rate_limited", "Too many attempts.", 300);
  UR_EXPECT_TRUE(fiveMinutes.kind == VerifySendNoticeKind::RateLimited);
  UR_EXPECT_EQ(5, fiveMinutes.minutes);

  const auto partial = DecideVerifySendNotice(false, "verify_rate_limited", "", 61);
  UR_EXPECT_TRUE(partial.kind == VerifySendNoticeKind::RateLimited);
  UR_EXPECT_EQ(2, partial.minutes);

  const auto seconds = DecideVerifySendNotice(false, "verify_rate_limited", "", 1);
  UR_EXPECT_TRUE(seconds.kind == VerifySendNoticeKind::RateLimited);
  UR_EXPECT_EQ(1, seconds.minutes);
}

UR_TEST(verifySendRateLimitedWithoutRetryUsesServerMessage) {
  const auto withMessage =
      DecideVerifySendNotice(false, "verify_rate_limited", "Too many attempts.", 0);
  UR_EXPECT_TRUE(withMessage.kind == VerifySendNoticeKind::ServerMessage);
  UR_EXPECT_TRUE(withMessage.message == "Too many attempts.");

  const auto withoutMessage = DecideVerifySendNotice(false, "verify_rate_limited", "", 0);
  UR_EXPECT_TRUE(withoutMessage.kind == VerifySendNoticeKind::SendFailed);
}

UR_TEST(verifySendUnknownCodeUsesServerMessage) {
  const auto withMessage = DecideVerifySendNotice(false, "verify_other", "Try later.", 0);
  UR_EXPECT_TRUE(withMessage.kind == VerifySendNoticeKind::ServerMessage);
  UR_EXPECT_TRUE(withMessage.message == "Try later.");

  const auto withoutMessage = DecideVerifySendNotice(false, "verify_other", "", 0);
  UR_EXPECT_TRUE(withoutMessage.kind == VerifySendNoticeKind::SendFailed);
}

UR_TEST(sdkHostCarriesTheVerifySendError) {
  const std::string source = ReadSource("SdkHost.cpp");
  if (source.empty()) {
    UR_FAIL("could not read SdkHost.cpp");
    return;
  }
  const std::string resend = FunctionBody(source, "void SdkHost::ResendVerifyCode(");
  UR_EXPECT_TRUE_MSG("ResendVerifyCode does not set result_errors",
                     resend.find("args.result_errors = true;") != std::string::npos);
  UR_EXPECT_TRUE_MSG("ResendVerifyCode ignores result->error",
                     resend.find("result->error") != std::string::npos);
  for (const char* signature : {"void SdkHost::LoginWithPassword(",
                                "void SdkHost::HandleNetworkCreateResult("}) {
    const std::string body = FunctionBody(source, signature);
    UR_EXPECT_TRUE_MSG(std::string(signature) + " drops verification_required->send_error",
                       body.find("verification_required->send_error") != std::string::npos);
  }
}

}  // namespace
}  // namespace urnw
