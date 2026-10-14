// No catalog string may tell a provider to run a CLI command that no longer exists. Head
// binding moved from "provider bind-head" / "snclaim bind-head" to "provider fleet bind", and
// the Top 200 hint kept naming the old command in every locale (support inbox 7934).
// SPDX-License-Identifier: MPL-2.0
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

}  // namespace
}  // namespace urnw
