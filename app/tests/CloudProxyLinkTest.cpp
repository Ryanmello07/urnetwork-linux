// The app has no protocol switch, so Settings links to the ur.io cloud proxies
// page for WireGuard, SOCKS and HTTPS proxies. The link is the official page
// and carries no credential, and the Connections pane opens it. The pane needs
// GTK, so the wiring case reads its source.
//
// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <sstream>
#include <string>

#include "CloudProxyLink.hpp"
#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::cloudproxy::kProxiesUrl;

std::string ReadSettingsSource() {
  std::ifstream in(std::string(UR_SRC_DIR) + "/SettingsPage.cpp", std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

UR_TEST(cloudProxyLinkIsTheProxiesPageOnUrIo) {
  UR_EXPECT_TRUE(std::string(kProxiesUrl) == "https://ur.io/app/proxies");
  UR_EXPECT_TRUE(std::string(kProxiesUrl).rfind("https://ur.io/", 0) == 0);
}

// never a token or an auth code in the url: ur.io signs the user in itself
UR_TEST(cloudProxyLinkCarriesNoCredential) {
  const std::string url = kProxiesUrl;
  UR_EXPECT_TRUE(url.find('?') == std::string::npos);
  UR_EXPECT_TRUE(url.find('#') == std::string::npos);
  UR_EXPECT_TRUE(url.find('@') == std::string::npos);
}

UR_TEST(connectionsPaneOpensTheCloudProxyLink) {
  const std::string source = ReadSettingsSource();
  UR_EXPECT_TRUE(!source.empty());
  UR_EXPECT_TRUE(source.find("OpenLink(cloudproxy::kProxiesUrl)") != std::string::npos);
  UR_EXPECT_TRUE(source.find("T_(\"use_wireguard_socks_https_proxy\", "
                             "\"Use WireGuard / SOCKS / HTTPS proxy\")") != std::string::npos);
  UR_EXPECT_TRUE(source.find("T_(\"use_wireguard_socks_https_proxy_note\",") != std::string::npos);
}

}  // namespace
