// The kill switch exception disclosure must match what the tunnel installs.
// Tunnel::Open routes CaptureV6Prefixes (::/0 minus link-local, ULA and
// multicast) into the tun, so IPv6 is routed like IPv4 and SMTP on TCP port 25
// (connect ip_smtp_policy.go) is the one public-route exception. The popover
// read a key the localization store does not have (kill_switch_smtp_exception),
// so every locale showed the English text; it must read the store key that
// carries the corrected copy and its translations.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "TunnelPolicy.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

constexpr const char* kKey = "kill_switch_exception_smtp_detail";

std::string ReadAppFile(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Has(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// The msgstr of the entry with msgctxt `key`, or "" when the catalog has none.
std::string PoTranslation(const std::string& po, const std::string& key) {
  const size_t at = po.find("msgctxt \"" + key + "\"");
  if (at == std::string::npos) return std::string();
  const size_t str = po.find("msgstr \"", at);
  if (str == std::string::npos) return std::string();
  const size_t begin = str + 8;
  const size_t end = po.find("\"\n", begin);
  if (end == std::string::npos) return std::string();
  return po.substr(begin, end - begin);
}

}  // namespace

UR_TEST(killSwitchExceptionTunnelCapturesPublicIpv6) {
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("2606:4700:4700::1111"));
  UR_EXPECT_TRUE(urnw::CaptureV6Claims("2a00:1450:4001:80b::200e"));
  UR_EXPECT_FALSE(urnw::CaptureV6Claims("fe80::1"));
}

UR_TEST(killSwitchExceptionPopoverReadsTheStoreKey) {
  const std::string drawer = ReadAppFile("src/ConnectDrawer.cpp");
  UR_EXPECT_FALSE(drawer.empty());
  UR_EXPECT_FALSE(Has(drawer, "\"kill_switch_smtp_exception\""));
  UR_EXPECT_TRUE(Has(drawer, std::string("\"") + kKey + "\""));
  UR_EXPECT_FALSE(Has(drawer, "IPv6 is not routed through URnetwork"));
}

UR_TEST(killSwitchExceptionCopyIsInTheCatalogAndTranslated) {
  const std::string pot = ReadAppFile("po/urnetwork.pot");
  UR_EXPECT_TRUE(Has(pot, std::string("msgctxt \"") + kKey + "\""));
  const std::string english = PoTranslation(ReadAppFile("po/en.po"), kKey);
  UR_EXPECT_TRUE(Has(english, "IPv6 is routed through URnetwork like IPv4"));
  UR_EXPECT_TRUE(Has(english, "SMTP on TCP port 25 bypasses the VPN"));
  for (const char* locale : {"ar", "de", "es", "ru", "zh_CN"}) {
    UR_EXPECT_TRUE_MSG(locale,
                       !PoTranslation(ReadAppFile(std::string("po/") + locale + ".po"), kKey).empty());
  }
}
