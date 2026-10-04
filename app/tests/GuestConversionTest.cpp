// The legacy guest conversion (GuestConversion.hpp, UPGRADE.md A4/D8): a guest
// adds a login method to its OWN network and verifies it, keeping its plan and
// balance. Driven against a fake session that answers synchronously.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <functional>
#include <string>
#include <vector>

#include "GuestConversion.hpp"

namespace {

using urnw::GuestConversion;
using urnw::GuestConversionStep;
using urnw::VerifySendNotice;
using urnw::VerifySendNoticeKind;

// Records every call; answers are queued and delivered on demand.
class FakeSession : public urnw::GuestConversionSession {
 public:
  std::vector<std::string> calls;
  std::string addError;
  std::string verifyError;
  // what every code send answers
  VerifySendNotice sendNotice;
  // the injected monotonic clock, in seconds
  int64_t now = 1000;
  std::function<void(std::string)> pendingAdd;

  void AddSignIn(const std::string& userAuth, const std::string& password,
                 std::function<void(std::string)> done) override {
    calls.push_back("addAuth " + userAuth + " " + password);
    pendingAdd = std::move(done);
  }
  void RefreshJwt() override { calls.push_back("refreshJwt"); }
  void RefreshBalance() override { calls.push_back("refreshBalance"); }
  void SendCode(const std::string& userAuth, std::function<void(VerifySendNotice)> done) override {
    calls.push_back("sendCode " + userAuth);
    done(sendNotice);
  }
  void VerifyCode(const std::string& userAuth, const std::string& code,
                  std::function<void(std::string)> done) override {
    calls.push_back("verify " + userAuth + " " + code);
    done(verifyError);
  }
  int64_t NowSeconds() override { return now; }
  void AnswerAdd() {
    auto done = std::move(pendingAdd);
    pendingAdd = nullptr;
    if (done) done(addError);
  }
};

UR_TEST(refreshedLegacyGuestIsStillAGuest) {
  // the refreshed jwt lost the claim; the server still reports no login method
  UR_EXPECT_TRUE(urnw::IsGuestNetwork(/*guestModeClaim=*/false, /*serverGuest=*/true));
  // a claim not yet re-signed still counts (older server: no `guest`)
  UR_EXPECT_TRUE(urnw::IsGuestNetwork(true, false));
  UR_EXPECT_FALSE(urnw::IsGuestNetwork(false, false));
}

UR_TEST(conversionAddsAndVerifiesOnTheSameNetwork) {
  FakeSession session;
  GuestConversion conversion(session);
  conversion.SubmitSignIn("  guest@example.com ", "correct horse battery");
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::AddingSignIn);
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterCode);
  conversion.SubmitCode(" 123456 ");
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::Done);
  const std::vector<std::string> expected = {
      "addAuth guest@example.com correct horse battery",
      // the network has a login method now: drop the guest state at once
      "refreshJwt",
      "refreshBalance",
      "sendCode guest@example.com",
      "verify guest@example.com 123456",
      "refreshBalance",
  };
  UR_EXPECT_TRUE(session.calls == expected);
}

UR_TEST(addFailureStaysOnTheSignInStep) {
  FakeSession session;
  GuestConversion conversion(session);
  session.addError = "User auth already exists.";
  conversion.SubmitSignIn("guest@example.com", "correct horse battery");
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterSignIn);
  UR_EXPECT_TRUE(conversion.Error() == "User auth already exists.");
  // nothing refreshed, no code sent
  UR_EXPECT_TRUE(session.calls.size() == 1);
}

UR_TEST(wrongCodeStaysOnTheCodeStep) {
  FakeSession session;
  GuestConversion conversion(session);
  conversion.SubmitSignIn("guest@example.com", "correct horse battery");
  session.AnswerAdd();
  session.verifyError = "Invalid code.";
  conversion.SubmitCode("000000");
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterCode);
  UR_EXPECT_TRUE(conversion.Error() == "Invalid code.");
}

UR_TEST(shortPasswordIsNotSubmitted) {
  FakeSession session;
  GuestConversion conversion(session);
  conversion.SubmitSignIn("guest@example.com", "short");
  conversion.SubmitSignIn("   ", "correct horse battery");
  UR_EXPECT_TRUE(session.calls.empty());
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterSignIn);
}

UR_TEST(answerAfterResetIsDropped) {
  FakeSession session;
  GuestConversion conversion(session);
  conversion.SubmitSignIn("guest@example.com", "correct horse battery");
  conversion.Reset();  // the sheet was closed and reopened
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterSignIn);
  UR_EXPECT_TRUE(session.calls.size() == 1);
}

VerifySendNotice RateLimited(int64_t retryAfterSeconds) {
  return urnw::DecideVerifySendNotice(false, "verify_rate_limited", "Too many attempts.",
                                   retryAfterSeconds);
}

size_t SendCount(const FakeSession& session) {
  size_t count = 0;
  for (const auto& call : session.calls) {
    if (call.rfind("sendCode ", 0) == 0) ++count;
  }
  return count;
}

// The login verify page holds Resend off for a rate limit's retry time and
// counts the minutes down; the conversion's code step did neither, so Resend
// asked again at once and the notice kept its first minute count.
UR_TEST(rateLimitHoldsResendUntilItsRetryTime) {
  FakeSession session;
  GuestConversion conversion(session);
  session.sendNotice = RateLimited(150);
  conversion.SubmitSignIn("guest@example.com", "correct horse battery");
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterCode);
  UR_EXPECT_EQ(size_t{1}, SendCount(session));
  conversion.Resend();
  UR_EXPECT_TRUE_MSG("Resend sends no second code during a rate limit", SendCount(session) == 1);
  UR_EXPECT_FALSE(conversion.CanResend());
  UR_EXPECT_TRUE(conversion.CoolingDown());
  auto notice = conversion.Notice();
  UR_EXPECT_TRUE(notice && notice->kind == VerifySendNoticeKind::RateLimited);
  UR_EXPECT_EQ(int64_t{3}, notice ? notice->minutes : 0);
  session.now += 100;
  notice = conversion.Notice();
  UR_EXPECT_EQ(int64_t{1}, notice ? notice->minutes : 0);  // counted down
  conversion.Resend();
  UR_EXPECT_EQ(size_t{1}, SendCount(session));
  session.now += 50;
  UR_EXPECT_TRUE(conversion.CanResend());
  UR_EXPECT_FALSE(conversion.CoolingDown());
  UR_EXPECT_FALSE(conversion.Notice().has_value());
  session.sendNotice = VerifySendNotice{};
  conversion.Resend();
  UR_EXPECT_EQ(size_t{2}, SendCount(session));
  notice = conversion.Notice();
  UR_EXPECT_TRUE(notice && notice->kind == VerifySendNoticeKind::Sent);
}

// A plain send failure keeps Resend usable, and reopening the sheet drops a
// running rate limit.
UR_TEST(sendFailureAndResetDoNotHoldResend) {
  FakeSession session;
  GuestConversion conversion(session);
  session.sendNotice = urnw::DecideVerifySendNotice(false, "verify_send_failed", "", 0);
  conversion.SubmitSignIn("guest@example.com", "correct horse battery");
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.CanResend());
  session.sendNotice = RateLimited(600);
  conversion.Resend();
  UR_EXPECT_FALSE(conversion.CanResend());
  conversion.Reset();
  UR_EXPECT_FALSE(conversion.CoolingDown());
  UR_EXPECT_FALSE(conversion.Notice().has_value());
}

}  // namespace
