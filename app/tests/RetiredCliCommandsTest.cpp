// No catalog string may tell a provider to run a CLI command that no longer exists. Head
// binding moved from "provider bind-head" / "snclaim bind-head" to "provider fleet bind", and
// the Top 200 hint kept naming the old command in every locale (support inbox 7934).
// SPDX-License-Identifier: MPL-2.0
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace urnw {
namespace {

UR_TEST(NoCatalogStringNamesARetiredCliCommand) {
  const std::filesystem::path po = std::filesystem::path(UR_SRC_DIR) / ".." / "po";
  int catalogs = 0;
  for (const auto& entry : std::filesystem::directory_iterator(po)) {
    const auto extension = entry.path().extension();
    if (extension != ".po" && extension != ".pot") continue;
    ++catalogs;
    std::ifstream in(entry.path(), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    UR_EXPECT_TRUE_MSG(entry.path().filename().string() + " names the retired bind-head command",
                       buffer.str().find("bind-head") == std::string::npos);
  }
  UR_EXPECT_TRUE_MSG("no catalogs found under " + po.string(), catalogs > 0);
}

// The msgstr of the entry with this msgctxt, its continuation lines joined; empty when absent.
std::string Translation(const std::string& catalog, const std::string& id) {
  const std::string context = "msgctxt \"" + id + "\"\n";
  const size_t at = catalog.find(context);
  if (at == std::string::npos) return "";
  const size_t msgstr = catalog.find("\nmsgstr \"", at);
  if (msgstr == std::string::npos) return "";
  const size_t end = catalog.find("\n\n", msgstr);
  return catalog.substr(msgstr, end == std::string::npos ? std::string::npos : end - msgstr);
}

// The Top 200 binding steps must name the sign-only "provider fleet sign" that prints the payload
// the page takes, and the hotkey hint the flags that make the hotkey signature, verbatim in every
// site locale (support inbox 7934).
UR_TEST(Top200BindingStepsNameFleetSign) {
  const std::filesystem::path po = std::filesystem::path(UR_SRC_DIR) / ".." / "po";
  const std::vector<std::pair<std::string, std::vector<std::string>>> required = {
      {"site_app_top200_step_bind", {"provider fleet sign", "~/.urnetwork/.provider.key"}},
      {"site_app_top200_hotkey_sign_hint", {"provider fleet sign", "--hotkey_seed_file", "--client_id"}},
  };
  for (const std::string locale : {"ar", "de", "en", "es", "ru", "zh_CN"}) {
    std::ifstream in(po / (locale + ".po"), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    for (const auto& [id, needles] : required) {
      const std::string value = Translation(buffer.str(), id);
      UR_EXPECT_TRUE_MSG(locale + ".po has no " + id, !value.empty());
      for (const auto& needle : needles) {
        UR_EXPECT_TRUE_MSG(locale + ".po " + id + " does not name " + needle,
                           value.find(needle) != std::string::npos);
      }
    }
  }
}

}  // namespace
}  // namespace urnw
