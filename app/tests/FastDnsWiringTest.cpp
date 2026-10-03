// Every surface that shows the host-network dns fallback (sdk DnsResolverSettings.EnableFallback)
// names it "Fast DNS on connect" through one translated key. The fallback is now an opt-in, off
// by default, and the store retired local_dns_fallback: a surface still asking for that key
// would show the old "Local DNS fallback" label, untranslated, with no word about the local
// network. The surfaces need GTK and the SDK, so this reads their sources and the catalog.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "FastDnsCopy.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadFastDnsSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

UR_TEST(FastDnsSurfacesUseTheOptInKey) {
  for (const char* file : {"DnsSheet.cpp", "ConnectPage.cpp", "ConnectDrawer.cpp"}) {
    const std::string source = ReadFastDnsSource(file);
    UR_EXPECT_TRUE(!source.empty());
    UR_EXPECT_TRUE(source.find("\"local_dns_fallback") == std::string::npos);
    UR_EXPECT_TRUE(source.find("kFastDnsLabel") != std::string::npos);
  }
}

UR_TEST(FastDnsToggleDrivesEnableFallbackWithItsDescription) {
  const std::string source = ReadFastDnsSource("DnsSheet.cpp");
  const size_t label = source.find("T_(kFastDnsLabel.key, kFastDnsLabel.english)");
  UR_EXPECT_TRUE(label != std::string::npos);
  if (label == std::string::npos) return;
  // the switch row built with the label maps to EnableFallback, and the footer under it
  // is the local-network description
  const size_t end = source.find(';', label);
  UR_EXPECT_TRUE(source.substr(label, end - label).find("EnableFallback") != std::string::npos);
  const size_t description = source.find("T_(kFastDnsDescription.key", end);
  UR_EXPECT_TRUE(description != std::string::npos && description - end < 400);
}

UR_TEST(FastDnsCatalogCarriesTheOptInKey) {
  const std::string catalog = ReadFastDnsSource("../po/en.po");
  UR_EXPECT_TRUE(catalog.find("msgctxt \"fast_dns_on_connect\"") != std::string::npos);
  UR_EXPECT_TRUE(catalog.find("msgctxt \"fast_dns_on_connect_description\"") !=
                 std::string::npos);
  UR_EXPECT_TRUE(catalog.find("msgctxt \"local_dns_fallback\"") == std::string::npos);
}

// T_ looks the copy up by msgctxt and msgid, so the English fallback in FastDnsCopy.hpp must be
// the catalog's msgid exactly or every locale shows the stale English.
UR_TEST(FastDnsEnglishMatchesTheCatalogMsgid) {
  const std::string catalog = ReadFastDnsSource("../po/en.po");
  for (const urnw::FastDnsCopy& copy : {urnw::kFastDnsLabel, urnw::kFastDnsDescription}) {
    const std::string entry = std::string("msgctxt \"") + copy.key + "\"\nmsgid \"" +
                              copy.english + "\"\n";
    UR_EXPECT_TRUE(catalog.find(entry) != std::string::npos);
  }
}

}  // namespace
