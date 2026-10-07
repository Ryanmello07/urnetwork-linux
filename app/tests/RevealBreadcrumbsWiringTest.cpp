// The signed-out reveal's breadcrumbs (MainWindow::RunSignedOutReveal and
// SettleReveal): a reveal that goes wrong still leaves a working window, so
// whether it armed, started or was cut short is written to the log, in the
// Windows client's words so one support grep matches both. The window needs
// GTK, so this reads its source.
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

std::string ReadRevealSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it; empty when it is gone.
std::string RevealBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Every needle occurs, in this order.
bool RevealInOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// Not armed, armed and started, each once per show.
UR_TEST(RevealBreadcrumbsWiring_TheRevealSaysWhetherItRan) {
  const std::string window = ReadRevealSource("MainWindow.cpp");
  UR_EXPECT_TRUE(RevealInOrder(
      RevealBody(window, "void MainWindow::RunSignedOutReveal()"),
      {"if (!ShouldAnimate()) {", "g_message(\"reveal: not armed (animations off in GTK)\");",
       "return;", "SettleReveal();", "g_message(\"reveal: armed (signed-out table)\");",
       "ArmHeroBloom(*heroBin_);", "StartHeroBloom(*heroBin_);",
       "revealStartedUs_ = g_get_monotonic_time();", "RiseIn(*getStartedBin_",
       "g_message(\"reveal: started\");"}));
}

// A settle that lands before the last rise says it cut the reveal short, and
// only then; every settle forgets the reveal.
UR_TEST(RevealBreadcrumbsWiring_ACutShortRevealIsLogged) {
  const std::string window = ReadRevealSource("MainWindow.cpp");
  UR_EXPECT_TRUE(RevealInOrder(
      RevealBody(window, "void MainWindow::SettleReveal()"),
      {"const int64_t revealMs = 360 + motion::kSlowMs;",
       "if (revealStartedUs_ != 0 && g_get_monotonic_time() - revealStartedUs_ < revealMs * 1000) {",
       "g_message(\"reveal: cancel-to-final while armed (hidden or superseded mid-bloom)\");",
       "revealStartedUs_ = 0;", "bin->settle();"}));
}
