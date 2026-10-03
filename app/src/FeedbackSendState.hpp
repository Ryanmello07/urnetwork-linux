// The feedback form's Send button while a send is out (SupportPage), decided
// pure so tests/FeedbackSendStateTest.cpp pins it without GTK.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw::feedback {

// What the Send button shows.
struct SendButton {
  // false while the request is out, so one press is one report
  bool sensitive;
  // gettext msgctxt of the label (and the accessible name) and its English
  const char* labelKey;
  const char* labelEnglish;
};

// The button only went insensitive while the request was out and kept
// reading "Send", so a slow send looked like a dead button. It reads
// "Sending…" until the answer lands; either answer puts "Send" back (on a
// failure the text stays, so Send is the retry).
inline SendButton SendButtonFor(bool sending) {
  if (sending) return {false, "feedback_sending", "Sending…"};
  return {true, "send", "Send"};
}

}  // namespace urnw::feedback
