// The legacy guest conversion (GuestConversion.hpp, UPGRADE.md A4/D8): a guest
// adds a login method to its OWN network and verifies it, keeping its plan and
// balance. Driven against a fake session that answers synchronously.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
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
  std::function<void(std::string)> pendingAdd;
  // what each code send answers
  VerifySendNotice sendNotice;

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

size_t CountSends(const std::vector<std::string>& calls) {
  size_t count = 0;
  for (const auto& call : calls) {
    if (call.rfind("sendCode ", 0) == 0) ++count;
  }
  return count;
}

// An added email or phone is added only once its code is verified: until
// then the flow is not Done, whatever the code send answered, so the Account
// page's AccountAddAuthSheet (which reports added on Done) cannot report it.
UR_TEST(addedSignInIsNotDoneUntilVerified) {
  FakeSession session;
  GuestConversion conversion(session, [] { return int64_t{0}; });
  conversion.SubmitSignIn("user@example.com", "correct horse battery");
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterCode);
  UR_EXPECT_EQ(size_t{1}, CountSends(session.calls));
  session.verifyError = "Invalid code.";
  conversion.SubmitCode("000000");
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterCode);
  session.verifyError.clear();
  conversion.SubmitCode("123456");
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::Done);
}

// After a rate-limited send, Resend sends nothing until the server's retry
// time has passed on the injected clock, and the notice counts the minutes
// down, then clears.
UR_TEST(resendWaitsOutTheRateLimit) {
  int64_t now = 1000;
  FakeSession session;
  session.sendNotice = urnw::DecideVerifySendNotice(false, urnw::kVerifySendErrorCodeRateLimited,
                                                    "rate limited", 120);
  GuestConversion conversion(session, [&now] { return now; });
  conversion.SubmitSignIn("user@example.com", "correct horse battery");
  session.AnswerAdd();
  UR_EXPECT_EQ(size_t{1}, CountSends(session.calls));
  UR_EXPECT_TRUE(conversion.CoolingDown());
  UR_EXPECT_FALSE(conversion.CanResend());
  UR_EXPECT_TRUE(conversion.ShownNotice().has_value());
  UR_EXPECT_TRUE(conversion.ShownNotice()->kind == VerifySendNoticeKind::RateLimited);
  UR_EXPECT_EQ(int64_t{2}, conversion.ShownNotice()->minutes);

  conversion.Resend();  // during the cooldown: refused
  UR_EXPECT_EQ(size_t{1}, CountSends(session.calls));

  now = 1061;
  UR_EXPECT_EQ(int64_t{1}, conversion.ShownNotice()->minutes);
  conversion.Resend();
  UR_EXPECT_EQ(size_t{1}, CountSends(session.calls));

  now = 1120;  // the retry time
  UR_EXPECT_FALSE(conversion.CoolingDown());
  UR_EXPECT_TRUE(conversion.CanResend());
  UR_EXPECT_FALSE(conversion.ShownNotice().has_value());
  session.sendNotice = VerifySendNotice{};
  conversion.Resend();
  UR_EXPECT_EQ(size_t{2}, CountSends(session.calls));
  UR_EXPECT_TRUE(conversion.ShownNotice()->kind == VerifySendNoticeKind::Sent);
}

// A failed send or a server message is shown as is, and Resend stays
// available (no cooldown); only an answer with no error says a code was sent.
UR_TEST(sendErrorsAreShownAndResendStaysAvailable) {
  FakeSession session;
  session.sendNotice =
      urnw::DecideVerifySendNotice(false, urnw::kVerifySendErrorCodeSendFailed, "", 0);
  GuestConversion conversion(session, [] { return int64_t{0}; });
  conversion.SubmitSignIn("user@example.com", "correct horse battery");
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.ShownNotice()->kind == VerifySendNoticeKind::SendFailed);
  UR_EXPECT_TRUE(conversion.CanResend());

  session.sendNotice = urnw::DecideVerifySendNotice(false, "user_auth_invalid", "Invalid phone.", 0);
  conversion.Resend();
  UR_EXPECT_TRUE(conversion.ShownNotice()->kind == VerifySendNoticeKind::ServerMessage);
  UR_EXPECT_TRUE(conversion.ShownNotice()->message == "Invalid phone.");

  session.sendNotice = urnw::DecideVerifySendNotice(true, "", "", 0);  // the request failed
  conversion.Resend();
  UR_EXPECT_TRUE(conversion.ShownNotice()->kind == VerifySendNoticeKind::SendFailed);
  UR_EXPECT_EQ(size_t{3}, CountSends(session.calls));
  UR_EXPECT_TRUE(conversion.Step() == GuestConversionStep::EnterCode);
}

// Reopening the sheet drops a running cooldown with the rest of the state.
UR_TEST(resetClearsTheCooldown) {
  int64_t now = 0;
  FakeSession session;
  session.sendNotice = urnw::DecideVerifySendNotice(false, urnw::kVerifySendErrorCodeRateLimited,
                                                    "", 600);
  GuestConversion conversion(session, [&now] { return now; });
  conversion.SubmitSignIn("user@example.com", "correct horse battery");
  session.AnswerAdd();
  UR_EXPECT_TRUE(conversion.CoolingDown());
  conversion.Reset();
  UR_EXPECT_FALSE(conversion.CoolingDown());
  UR_EXPECT_FALSE(conversion.ShownNotice().has_value());
}

}  // namespace
