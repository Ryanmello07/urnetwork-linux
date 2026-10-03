// The feedback form's Send button (FeedbackSendState.hpp): while the request
// is out it is insensitive and reads "Sending…"; either answer puts back a
// sensitive "Send". The same cases as windows tools/feedback-send-tests.cpp.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "FeedbackSendState.hpp"

using urnw::feedback::SendButtonFor;

namespace {

std::string Mismatch(const std::string& expected, const std::string& actual) {
  return "expected \"" + expected + "\", got \"" + actual + "\"";
}

}  // namespace

#define EXPECT_LABEL(expected, actual) \
  UR_EXPECT_TRUE_MSG(Mismatch((expected), (actual)), std::string(expected) == std::string(actual))

UR_TEST(FeedbackSend_SendingReadsSendingAndIsInsensitive) {
  const auto button = SendButtonFor(true);
  EXPECT_LABEL("feedback_sending", button.labelKey);
  EXPECT_LABEL("Sending…", button.labelEnglish);
  UR_EXPECT_FALSE(button.sensitive);
}

UR_TEST(FeedbackSend_IdleAndAfterEitherAnswerReadsSend) {
  const auto button = SendButtonFor(false);
  EXPECT_LABEL("send", button.labelKey);
  EXPECT_LABEL("Send", button.labelEnglish);
  UR_EXPECT_TRUE(button.sensitive);
}
