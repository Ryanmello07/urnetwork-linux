// The daemon's whole-file reads (daemon/TextFile.hpp): a read that fails after
// the open answers empty instead of throwing. A tester's daemon on a Wi-Fi
// laptop aborted every reaper tick reading /sys/class/net/wlan0/speed, which
// its driver answers with EINVAL: libstdc++'s filebuf threw
// std::ios_base::failure from underflow, past istreambuf_iterator, out of a
// glib callback. A directory takes the same path here (it opens, and its first
// read fails with EISDIR), so the throw is reproduced without a driver. The
// daemon's call sites need glib and the SDK, so they are read as source.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>

#include "WiringSource.hpp"
#include "daemon/TextFile.hpp"

namespace fs = std::filesystem;
using namespace urnw::testing::wiring;

namespace {

// A fresh directory of the test's own, removed with everything in it when the
// test ends; empty when none could be made.
struct Scratch {
  std::string dir;
  Scratch() {
    std::string pattern = (fs::temp_directory_path() / "urnw-text-file-XXXXXX").string();
    if (::mkdtemp(pattern.data()) != nullptr) dir = pattern;
  }
  ~Scratch() {
    std::error_code ignored;
    if (!dir.empty()) fs::remove_all(dir, ignored);
  }
};

void Write(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

}  // namespace

// The libstdc++ behaviour the fix is about: the old reader's exact expression
// throws when the read fails after the open succeeded. libc++'s underflow
// answers eof there instead, so the expectation is libstdc++'s alone.
UR_TEST(DaemonTextFile_AFailedReadThrowsThroughIstreambufIterator) {
  Scratch scratch;
  UR_EXPECT_TRUE_MSG("a scratch directory is made", !scratch.dir.empty());
  std::ifstream in(scratch.dir);
  UR_EXPECT_TRUE_MSG("a directory opens as a stream", static_cast<bool>(in));
#if defined(__GLIBCXX__)
  bool threw = false;
  try {
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    (void)text;
  } catch (const std::ios_base::failure&) {
    threw = true;
  }
  UR_EXPECT_TRUE_MSG("a failed read throws std::ios_base::failure past istreambuf_iterator",
                     threw);
#endif
}

UR_TEST(DaemonTextFile_AFailedReadAnswersEmpty) {
  Scratch scratch;
  UR_EXPECT_TRUE_MSG("a scratch directory is made", !scratch.dir.empty());
  bool threw = false;
  std::string text = "unset";
  try {
    text = urnw::textfile::ReadAll(scratch.dir);
  } catch (...) {
    threw = true;
  }
  UR_EXPECT_TRUE_MSG("ReadAll never throws", !threw);
  UR_EXPECT_TRUE_MSG("a failed read is an empty text", text.empty());
}

UR_TEST(DaemonTextFile_AFileReadsWholeAndAMissingOneIsEmpty) {
  Scratch scratch;
  UR_EXPECT_TRUE_MSG("a scratch directory is made", !scratch.dir.empty());
  const std::string content = "Iface\tDestination\nwlan0\t00000000\n";
  Write(scratch.dir + "/route", content);
  UR_EXPECT_TRUE(urnw::textfile::ReadAll(scratch.dir + "/route") == content);
  Write(scratch.dir + "/empty", "");
  UR_EXPECT_TRUE(urnw::textfile::ReadAll(scratch.dir + "/empty").empty());
  UR_EXPECT_TRUE(urnw::textfile::ReadAll(scratch.dir + "/missing").empty());
}

// Every whole-file read under src/daemon goes through TextFile.hpp but the key
// material's: no istreambuf_iterator in any other source there, and the
// readers the reaper tick, the stop record, the network country and the
// support lines use delegate to ReadAll.
UR_TEST(DaemonTextFile_EveryWholeFileReadGoesThroughIt) {
  const std::string tunnel = ReadCode("daemon/TunnelHost.cpp");
  const std::string keyReader = FunctionBody(tunnel, "std::vector<uint8_t> ReadFileBytes(");
  UR_EXPECT_TRUE_MSG("TunnelHost.cpp has its key-material reader", !keyReader.empty());
  std::error_code error;
  int sources = 0;
  for (const auto& entry : fs::directory_iterator(std::string(UR_SRC_DIR) + "/daemon", error)) {
    const std::string name = entry.path().filename().string();
    const std::string ext = entry.path().extension().string();
    if ((ext != ".cpp" && ext != ".hpp") || name == "TextFile.hpp") continue;
    std::string code = ReadCode("daemon/" + name);
    UR_EXPECT_TRUE_MSG(name + " is read", !code.empty());
    if (name == "TunnelHost.cpp") code.erase(code.find(keyReader), keyReader.size());
    UR_EXPECT_TRUE_MSG(name + " reads no file through istreambuf_iterator",
                       !Contains(code, "istreambuf_iterator"));
    ++sources;
  }
  UR_EXPECT_TRUE_MSG("src/daemon is listed", !error && sources >= 10);
  UR_EXPECT_TRUE(Contains(FunctionBody(tunnel, "std::string ReadTextFile("),
                          "return textfile::ReadAll(path);"));
  UR_EXPECT_TRUE(Contains(FunctionBody(tunnel, "void TunnelHost::RestoreStopRecord()"),
                          "textfile::ReadAll(selfrestart::kStopRecordPath)"));
  for (const char* file : {"daemon/NetworkCountryWatcher.cpp", "daemon/SupportDiagnostics.cpp"}) {
    UR_EXPECT_TRUE_MSG(std::string(file) + " delegates ReadProcText",
                       Contains(ReadCode(file), "return textfile::ReadAll(path);"));
  }
  UR_EXPECT_EQ(std::size_t{1}, CountOf(ReadCode("daemon/TextFile.hpp"),
                                       "catch (const std::exception&)"));
}

// The key material keeps the reader that throws: a seed, certificate or key
// that is there but fails its read must fail the start, not pass for a
// missing file that NewDeviceLocked would answer with a new identity.
UR_TEST(DaemonTextFile_TheKeyMaterialReadStillThrows) {
  const std::string tunnel = ReadCode("daemon/TunnelHost.cpp");
  const std::string keyReader = FunctionBody(tunnel, "std::vector<uint8_t> ReadFileBytes(");
  UR_EXPECT_TRUE(Contains(keyReader, "std::istreambuf_iterator<char>(f)"));
  UR_EXPECT_TRUE(!Contains(keyReader, "textfile::"));
  UR_EXPECT_TRUE(!Contains(keyReader, "catch"));
  const std::string has = FunctionBody(tunnel, "bool TunnelHost::HasStoredKeyMaterial() const");
  UR_EXPECT_TRUE(Contains(has, "ReadFileBytes(storageRoot_ + \"/\" + kClientKeySeedFile)"));
  const std::string load = FunctionBody(tunnel, "TunnelHost::LoadKeyMaterial() const");
  UR_EXPECT_EQ(std::size_t{3}, CountOf(load, "ReadFileBytes("));
}
