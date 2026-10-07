// Home's first entrance (MainWindow::ApplyAuthState): the first sign-in made
// in a visible window crossfades the login flow into Home on the page
// crossfade's 250 ms, once per window, as Windows' homeRevealed_ does; every
// other swap of the window's root stack is instant. The window needs GTK, so
// this reads its source.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadEntranceSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it; empty when it is gone.
std::string EntranceBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Every needle occurs, in this order.
bool EntranceInOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

size_t EntranceCount(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

}  // namespace

// Only a sign-in, the first in the window, while it is shown with animations
// on and Home not already up, crossfades; the sign-out branch passes NONE.
UR_TEST(HomeEntranceWiring_OnlyTheFirstSignInCrossfades) {
  const std::string window = ReadEntranceSource("MainWindow.cpp");
  const std::string apply = EntranceBody(window, "void MainWindow::ApplyAuthState(bool loggedIn)");
  UR_EXPECT_TRUE(EntranceInOrder(
      apply, {"const bool firstEntrance = loggedIn && !homeRevealed_ && windowVisible_ &&",
              "motion::ShouldAnimate() &&", "stack_.get_visible_child_name() != \"home\";",
              "if (loggedIn) homeRevealed_ = true;",
              "stack_.set_visible_child(loggedIn ? \"home\" : \"login\",",
              "firstEntrance ? Gtk::StackTransitionType::CROSSFADE",
              ": Gtk::StackTransitionType::NONE);"}));
  // the root stack crossfades nowhere else, on the page crossfade's duration
  UR_EXPECT_TRUE(EntranceCount(window, "StackTransitionType::CROSSFADE") == 1);
  UR_EXPECT_TRUE(window.find("stack_.set_transition_duration(motion::kBaseMs);") !=
                 std::string::npos);
  UR_EXPECT_TRUE(window.find("stack_.set_transition_type(") == std::string::npos);
}
