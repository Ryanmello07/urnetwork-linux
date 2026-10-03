// UPGRADE.md §4.4: the ur.io/checkout bridge url and its urnetwork://checkout
// hand-back are the SDK's envelope (urnet::buildCheckoutBridgeUrl,
// urnet::isCheckoutRedirect, urnet::parseCheckoutRedirect), not a hand-built
// copy that drifts from windows and the web. The sheet needs GTK, WebKit and
// the SDK, so it is read as text.
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

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

UR_TEST(checkoutBridgeEnvelopeComesFromTheSdk) {
  const std::string sheet = ReadSource("UpgradeSheet.cpp");
  UR_EXPECT_TRUE(Has(sheet, "urnet::buildCheckoutBridgeUrl(clientSecret)"));
  UR_EXPECT_TRUE(Has(sheet, "urnet::isCheckoutRedirect(uri)"));
  UR_EXPECT_TRUE(Has(sheet, "urnet::parseCheckoutRedirect(uri)"));
  // no hand-built bridge url or hand-parsed status
  UR_EXPECT_FALSE(Has(sheet, "\"https://ur.io/checkout\""));
  UR_EXPECT_FALSE(Has(sheet, "\"urnetwork://checkout\""));
  UR_EXPECT_FALSE(Has(sheet, "status->second == \"complete\""));
}

}  // namespace
