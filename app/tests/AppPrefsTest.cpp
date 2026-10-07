// app_prefs.json (PrefsFile.hpp): a file that is not a preferences object reads
// as {}, a value of another type reads as the fallback, a set keeps every other
// key, sets from many threads at once lose none, and no write leaves a
// temporary file behind or a half-written target. Run on a temporary
// directory; AppPrefs.hpp itself needs glib to find the real one, so its call
// into the helpers is read as source with the comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "PrefsFile.hpp"
#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

namespace fs = std::filesystem;

// A fresh directory, removed with everything in it when the case ends.
class PrefsDir {
 public:
  PrefsDir() {
    std::string pattern = (fs::temp_directory_path() / "urnw-prefs-XXXXXX").string();
    if (::mkdtemp(pattern.data()) != nullptr) dir_ = pattern;
  }
  ~PrefsDir() {
    std::error_code error;
    if (!dir_.empty()) fs::remove_all(dir_, error);
  }
  std::string File() const { return dir_ + "/app_prefs.json"; }
  bool Ok() const { return !dir_.empty(); }
  // Every entry in the directory but the target.
  int Strays() const {
    int strays = 0;
    for (const auto& entry : fs::directory_iterator(dir_)) {
      if (entry.path().filename() != "app_prefs.json") ++strays;
    }
    return strays;
  }
  void Write(const std::string& text) const {
    std::ofstream out(File(), std::ios::binary | std::ios::trunc);
    out << text;
  }

 private:
  std::string dir_;
};

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadPrefsSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  bool inString = false;
  for (size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (inString) {
      if (c == '\\') {
        ++at;
      } else if (c == '"' || c == '\n') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
      while (at < text.size() && text[at] != '\n') text[at++] = ' ';
    }
  }
  return text;
}

}  // namespace

UR_TEST(AppPrefs_AFileThatIsNoObjectReadsEmpty) {
  PrefsDir dir;
  UR_EXPECT_TRUE(dir.Ok());
  // missing
  UR_EXPECT_TRUE(urnw::prefs::ReadAllAt(dir.File()) == nlohmann::json::object());
  for (const char* text : {"", "{\"advanced_mode\": tr", "[1, 2]", "42", "\"x\"", "null"}) {
    dir.Write(text);
    UR_EXPECT_TRUE_MSG(text, urnw::prefs::ReadAllAt(dir.File()) == nlohmann::json::object());
  }
  // over the cap, though well formed
  dir.Write("{\"pad\": \"" + std::string(urnw::prefs::kMaxFileBytes, 'x') + "\"}");
  UR_EXPECT_TRUE(urnw::prefs::ReadAllAt(dir.File()) == nlohmann::json::object());
  dir.Write("{\"advanced_mode\": true}");
  UR_EXPECT_TRUE(urnw::prefs::ReadAllAt(dir.File()).value("advanced_mode", false));
}

UR_TEST(AppPrefs_AValueOfAnotherTypeReadsAsTheFallback) {
  const nlohmann::json all = {{"flag", "yes"}, {"count", true}, {"on", true}, {"n", 7}};
  UR_EXPECT_FALSE(urnw::prefs::ValueOr<bool>(all, "flag", false));
  UR_EXPECT_EQ(5, urnw::prefs::ValueOr<int64_t>(all, "count", 5));
  UR_EXPECT_TRUE(urnw::prefs::ValueOr<bool>(all, "on", false));
  UR_EXPECT_EQ(7, urnw::prefs::ValueOr<int64_t>(all, "n", 0));
  UR_EXPECT_EQ(3, urnw::prefs::ValueOr<int64_t>(all, "absent", 3));
}

// A set keeps the other keys, replaces the file whole (only the owner reads
// it), and leaves nothing beside it.
UR_TEST(AppPrefs_ASetKeepsEveryOtherKey) {
  PrefsDir dir;
  dir.Write("{\"connect_on_launch\": true}");
  UR_EXPECT_EQ(0, urnw::prefs::SetAt(dir.File(), "advanced_mode", true));
  UR_EXPECT_EQ(0, urnw::prefs::SetAt<int64_t>(dir.File(), "update_last_check_at", 1700000000));
  const nlohmann::json all = urnw::prefs::ReadAllAt(dir.File());
  UR_EXPECT_TRUE(all.value("connect_on_launch", false));
  UR_EXPECT_TRUE(all.value("advanced_mode", false));
  UR_EXPECT_EQ(1700000000, all.value("update_last_check_at", int64_t{0}));
  struct stat info {};
  UR_EXPECT_EQ(0, ::stat(dir.File().c_str(), &info));
  UR_EXPECT_EQ(0600, info.st_mode & 0777);
  UR_EXPECT_EQ(0, dir.Strays());
}

// The update checker's worker and the main loop set at the same time: every
// key lands, and the file reads whole throughout.
UR_TEST(AppPrefs_SetsFromManyThreadsLoseNoKey) {
  PrefsDir dir;
  constexpr int kThreads = 8;
  constexpr int kSetsEach = 25;
  std::vector<std::thread> writers;
  int failures[kThreads] = {};
  for (int t = 0; t < kThreads; ++t) {
    writers.emplace_back([&dir, &failures, t] {
      for (int i = 0; i < kSetsEach; ++i) {
        const std::string key = "k" + std::to_string(t) + "_" + std::to_string(i);
        if (urnw::prefs::SetAt<int64_t>(dir.File(), key.c_str(), t * 1000 + i) != 0) {
          ++failures[t];
        }
      }
    });
  }
  int emptyReads = 0;
  for (int i = 0; i < 200; ++i) {
    // a reader beside the writers never sees a half-written file
    std::error_code error;
    if (fs::exists(dir.File(), error) && urnw::prefs::ReadAllAt(dir.File()).empty()) ++emptyReads;
  }
  for (std::thread& writer : writers) writer.join();
  for (int t = 0; t < kThreads; ++t) UR_EXPECT_EQ(0, failures[t]);
  UR_EXPECT_EQ(0, emptyReads);
  const nlohmann::json all = urnw::prefs::ReadAllAt(dir.File());
  UR_EXPECT_EQ(kThreads * kSetsEach, static_cast<int>(all.size()));
  for (int t = 0; t < kThreads; ++t) {
    for (int i = 0; i < kSetsEach; ++i) {
      const std::string key = "k" + std::to_string(t) + "_" + std::to_string(i);
      UR_EXPECT_EQ(t * 1000 + i, all.value(key, int64_t{-1}));
    }
  }
  UR_EXPECT_EQ(0, dir.Strays());
}

// A write that cannot happen says so and leaves no temporary file.
UR_TEST(AppPrefs_AFailedWriteSaysSo) {
  PrefsDir dir;
  const std::string missing = dir.File() + ".d/app_prefs.json";
  UR_EXPECT_EQ(ENOENT, urnw::prefs::SetAt(missing, "advanced_mode", true));
  UR_EXPECT_EQ(0, dir.Strays());
}

// The app's own Get and Set go through these helpers, and nothing truncates
// the file in place any more.
UR_TEST(AppPrefsWiring_TheAppReadsAndWritesThroughTheHelpers) {
  const std::string prefs = ReadPrefsSource("AppPrefs.hpp");
  UR_EXPECT_TRUE(prefs.find("return ReadAllAt(PrefsPath());") != std::string::npos);
  UR_EXPECT_TRUE(prefs.find("return ValueOr(ReadAll(), key, fallback);") != std::string::npos);
  UR_EXPECT_TRUE(prefs.find("SetAt(path, key, value)") != std::string::npos);
  UR_EXPECT_TRUE(prefs.find("std::ios::trunc") == std::string::npos);
  UR_EXPECT_TRUE(prefs.find("ofstream") == std::string::npos);
}
