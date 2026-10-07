// The seedphrase sign-in's box holds the account's credential: it asks the
// input method for no spellcheck (autocorrect would rewrite BIP-39 words), no
// learning of what is typed, and no emoji, and keeps the words visible. The
// box needs GTK, so this reads MainWindow.cpp with the comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <sstream>
#include <string>

#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadSeedphraseSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  bool inString = false;
  for (size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (inString) {
      if (c == '\\') {
        ++at;
      } else if (c == '"' || c == '\n') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
      while (at < text.size() && text[at] != '\n') text[at++] = ' ';
    }
  }
  return text;
}

}  // namespace

UR_TEST(SeedphraseInputWiring_TheBoxKeepsThePhraseFromTheInputMethod) {
  const std::string window = ReadSeedphraseSource("MainWindow.cpp");
  const size_t start = window.find("void MainWindow::BuildSeedphraseStep() {");
  UR_EXPECT_TRUE(start != std::string::npos);
  const std::string step =
      start == std::string::npos ? std::string()
                                 : window.substr(start, window.find("\n}\n", start) - start);
  const size_t made = step.find("seedphraseView_ = Gtk::make_managed<Gtk::TextView>();");
  const size_t hints = step.find(
      "seedphraseView_->set_input_hints(Gtk::InputHints::NO_SPELLCHECK | Gtk::InputHints::PRIVATE |");
  UR_EXPECT_TRUE(made != std::string::npos);
  UR_EXPECT_TRUE(hints != std::string::npos && made < hints);
  UR_EXPECT_TRUE(step.find("Gtk::InputHints::NO_EMOJI);") != std::string::npos);
  // the words must stay readable: never a password purpose
  UR_EXPECT_TRUE(step.find("set_input_purpose") == std::string::npos);
}
