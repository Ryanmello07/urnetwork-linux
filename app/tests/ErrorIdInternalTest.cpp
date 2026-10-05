// The C ABI's internal error id (URNET_ERROR_ID_INTERNAL, "internal_error"):
// what an sdk error-id function -- setVlessSettings, validateVlessSettings,
// setControlDohUrls, validateControlDohUrl -- answers when the call could not
// run. Its one source in this app is the SDK header. The presentation headers
// need no copy, because they read every id they do not know, this one
// included, as "Something went wrong."; SdkHost, which includes the header,
// answers it for a validate that throws. The app used to keep two hand-written
// mirrors of it (vless::kErrorInternal and extender::kSdkErrorIdInternal), and
// this keeps a second copy from coming back, under those names or any other.
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

std::string ReadErrorIdSource(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool HasText(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

// No source spells the id out or keeps a mirror of it: the value lives in the
// SDK header alone, so a new SDK can never leave a stale copy behind.
UR_TEST(errorIdInternalNoSourceKeepsACopy) {
  const std::filesystem::path src(UR_SRC_DIR);
  if (!std::filesystem::is_directory(src)) {
    UR_FAIL("UR_SRC_DIR is not a directory");
    return;
  }
  int scanned = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
    if (!entry.is_regular_file()) continue;
    const std::string ext = entry.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
    ++scanned;
    const std::string text = ReadErrorIdSource(entry.path());
    const std::string name = std::filesystem::relative(entry.path(), src).string();
    UR_EXPECT_TRUE_MSG(name + " spells out \"internal_error\"",
                       !HasText(text, "\"internal_error\""));
    for (const char* mirror : {"kErrorInternal", "kSdkErrorIdInternal"}) {
      UR_EXPECT_TRUE_MSG(name + " keeps " + mirror, !HasText(text, mirror));
    }
  }
  UR_EXPECT_TRUE_MSG("the scan saw the sources", scanned > 10);
}

// The one place the app answers the id itself, a validate that throws, takes
// it from the SDK header it includes.
UR_TEST(errorIdInternalTheHostAnswersTheHeadersId) {
  const std::string host =
      ReadErrorIdSource(std::filesystem::path(UR_SRC_DIR) / "SdkHost.cpp");
  UR_EXPECT_TRUE(HasText(host, "#include <urnetwork_sdk.h>"));
  UR_EXPECT_TRUE(HasText(host, "return URNET_ERROR_ID_INTERNAL;"));
}
