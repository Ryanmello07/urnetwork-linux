// Fixed IP keeps one exit for the session (connect stickyExit: a Fixed IP
// window has no standing spare and is never drained on the hourly lifetime),
// so both Fixed IP rows - the connect page's and the connect drawer's - say so
// in a note under the title (fixed_ip_subtitle, from the localizations store).
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

constexpr const char* kKey = "fixed_ip_subtitle";
constexpr const char* kEnglish =
    "Keeps one exit for the session; changes only if that provider goes offline.";

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

UR_TEST(fixedIpRowsShowTheSubtitle) {
  // the English fallback is the store's copy, so a missing catalog reads right
  const std::string call = std::string("T_(\"") + kKey + "\",";
  for (const char* file : {"src/ConnectPage.cpp", "src/ConnectDrawer.cpp"}) {
    const std::string source = ReadAppFile(file);
    UR_EXPECT_TRUE_MSG(file, Has(source, call));
    UR_EXPECT_TRUE_MSG(file, Has(source, std::string("\"") + kEnglish + "\""));
  }
  // the page's row passes it as the toggle row's note
  const std::string page = ReadAppFile("src/ConnectPage.cpp");
  const size_t fixed = page.find("fixedIpToggle_ = addToggleRow(T_(\"fixed_ip\", \"Fixed IP\")");
  UR_EXPECT_TRUE(fixed != std::string::npos);
  UR_EXPECT_TRUE(page.find(call, fixed) != std::string::npos &&
                 page.find(call, fixed) < page.find("anonToggle_ = addToggleRow(", fixed));
}

UR_TEST(fixedIpSubtitleIsInTheCatalogAndTranslated) {
  const std::string pot = ReadAppFile("po/urnetwork.pot");
  UR_EXPECT_TRUE(Has(pot, std::string("msgctxt \"") + kKey + "\""));
  UR_EXPECT_TRUE(PoTranslation(ReadAppFile("po/en.po"), kKey) == kEnglish);
  for (const char* locale : {"ar", "cs", "de", "es", "fr", "ja", "ko", "pt_BR", "ru", "uk", "zh_CN", "zh_HK"}) {
    const std::string value = PoTranslation(ReadAppFile(std::string("po/") + locale + ".po"), kKey);
    UR_EXPECT_TRUE_MSG(locale, !value.empty() && value != kEnglish);
  }
}
