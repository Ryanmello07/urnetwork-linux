// The Seeker multiplier note must say what a verified token gives: 2x points
// AND 2x the free daily and referral data grants (server pro.yml
// seeker.data_multiplier, subsidy seeker_holder_multiplier). The retired
// seeker_points_only ("applies to points only") understated it, so no app
// source may show it again, and the earnings page must show the benefit.
//
// SPDX-License-Identifier: MPL-2.0
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

UR_TEST(seekerCopyNoSourceShowsThePointsOnlyNote) {
  int scanned = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(UR_SRC_DIR)) {
    const auto ext = entry.path().extension();
    if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".hpp")) continue;
    ++scanned;
    if (ReadFile(entry.path()).find("\"seeker_points_only\"") != std::string::npos) {
      UR_FAIL(entry.path().string() + " shows seeker_points_only");
    }
  }
  UR_EXPECT_TRUE(scanned > 0);
}

UR_TEST(seekerCopyEarningsPageShowsTheBenefit) {
  const std::string page = ReadFile(std::filesystem::path(UR_SRC_DIR) / "EarningsPage.cpp");
  UR_EXPECT_TRUE(page.find("T_(\"seeker_multiplier_benefit\"") != std::string::npos);
}

}  // namespace
