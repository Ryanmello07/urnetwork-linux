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

// Records every call; answers are queued and delivered on demand.
class FakeSession : public urnw::GuestConversionSession {
 public:
  std::vector<std::string> calls;
  std::string addError;
  std::string verifyError;
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
    done(VerifySendNotice{});
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

}  // namespace
