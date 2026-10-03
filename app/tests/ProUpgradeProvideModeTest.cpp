// Upgrading to Pro never changes the provide control mode: the wiring.
// ProUpgradeReactionTest proves the decision; this proves MainWindow uses it.
//
// The free -> Pro detection once reset provide mode to Never, so a paying user
// silently stopped earning (in-app feedback theme earnings-not-updating).
// MainWindow and SdkHost need GTK and the SDK, so the sources are read as
// text, the same way UpdateWiringTest and TunnelPolicyTest do.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The statement block guarded by the first `if (` containing `condition`, or "".
std::string GuardedBlock(const std::string& source, const std::string& condition) {
  const size_t at = source.find(condition);
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

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

UR_TEST(proUpgradeRoutesThroughTheReaction) {
  const std::string window = ReadSource("MainWindow.cpp");
  UR_EXPECT_TRUE(!window.empty());
  // the upgrade detection still drives the celebration, through the tested
  // reaction (ProUpgradeReactionTest)
  UR_EXPECT_TRUE(Has(window, "DidDetectUpgradeToPro()"));
  UR_EXPECT_TRUE(Has(GuardedBlock(window, "DidDetectUpgradeToPro()"), "ReactToProUpgrade("));
  size_t at = 0;
  while ((at = window.find("DidDetectUpgradeToPro()", at)) != std::string::npos) {
    const std::string block = GuardedBlock(window.substr(at), "DidDetectUpgradeToPro()");
    UR_EXPECT_TRUE(!Has(block, "ProvideToNever"));
    UR_EXPECT_TRUE(!Has(block, "\"never\""));
    at += 1;
  }
}

UR_TEST(sdkHostHasNoProvideResetForTheUpgrade) {
  const std::string host = ReadSource("SdkHost.cpp");
  const std::string header = ReadSource("SdkHost.hpp");
  UR_EXPECT_TRUE(!host.empty() && !header.empty());
  UR_EXPECT_TRUE(!Has(host, "ResetProvideToNever"));
  UR_EXPECT_TRUE(!Has(header, "ResetProvideToNever"));
}
