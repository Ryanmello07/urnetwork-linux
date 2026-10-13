// Executable spec for Export logs (src/LogExport.hpp): the app's log copied to
// where the person asked, with every urnetwork: link in it reduced, and never the
// log itself emptied, changed or made unreadable by the copy. It is the Windows
// app's tools/log-export-tests.cpp, case for case; each case writes its files in a
// scratch directory of its own under the system's temporary directory, which it
// removes when it ends, so the last case makes the log and the directory it reads
// for itself. Then the header itself is read: it writes only the destination, and
// only after it has looked at the source.
//
// Every value is made up and obviously so (SECRETSTATE, SECRETKEY); none is a real state, key or
// code.
//
// SPDX-License-Identifier: MPL-2.0
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <unistd.h>

#include "LogExport.hpp"
#include "TestHarness.hpp"
#include "WiringSource.hpp"

// The Windows spec's Check, as this harness records it.
#define Check(cond, what) UR_EXPECT_TRUE_MSG((what), (cond))

namespace fs = std::filesystem;
using urnw::logexport::CopyWithoutLinks;

namespace {

fs::path gDir;

// The case's scratch directory, made when the case starts and removed with all it holds when it
// ends (the Windows spec takes one from its command line and removes its files at the end).
struct Scratch {
  Scratch() {
    gDir = fs::temp_directory_path() / ("urnw-log-export-" + std::to_string(::getpid()));
    std::error_code ignored;
    fs::remove_all(gDir, ignored);
    fs::create_directories(gDir, ignored);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
  ~Scratch() {
    std::error_code ignored;
    fs::remove_all(gDir, ignored);
  }
};

void Write(const fs::path& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const std::string kLink =
    "urnetwork://auth/?state=SECRETSTATE&epk=SECRETKEY&n=SECRETNONCE&d=SECRETBOX";
const std::string kTag = "urnetwork://auth [redacted " + std::to_string(kLink.size()) + " chars]";
const std::string kExe = R"("C:\Program Files\URnetwork\URnetwork.exe")";

// The copy of `log` made through the function, or nothing at all when it said no.
bool Copy(const std::string& log, std::string& copied) {
  const fs::path source = gDir / "urnetwork-app.log";
  const fs::path destination = gDir / "exported.log";
  Write(source, log);
  fs::remove(destination);
  const bool ok = CopyWithoutLinks(source, destination);
  copied = ok ? Read(destination) : std::string();
  return ok;
}

UR_TEST(LogExport_TheCopyIsTheLogWithoutItsLinks) {
  const Scratch scratch;
  std::string copied;
  const std::string stamp = "2026-10-07 23:36:01 INFO  startup:   command line     : ";
  const std::string log = stamp + kExe + " " + kLink + "\r\nnext line\r\n" + stamp + kExe + " \"" +
                          kLink + "\"\r\nlast\r\n";
  const std::string want = stamp + kExe + " " + kTag + "\r\nnext line\r\n" + stamp + kExe + " \"" +
                           kTag + "\"\r\nlast\r\n";
  Check(Copy(log, copied) && copied == want,
        "copy: every link of every line is its host and a length, and every other byte is kept");
  Check(copied.find("SECRET") == std::string::npos, "copy: no value survives");
  Check(Copy(log + "no final line break", copied) && copied == want + "no final line break",
        "copy: a last line that ends without a line break is not given one");
  Check(Copy(log, copied) && copied.size() > 2 && copied.compare(copied.size() - 2, 2, "\r\n") == 0,
        "copy: a last line that ends with a line break keeps it, carriage return included");
  Check(Copy("", copied) && copied.empty(), "copy: an empty log is an empty file");
  Check(Copy("\n", copied) && copied == "\n" && Copy("\r\n\r\n", copied) && copied == "\r\n\r\n",
        "copy: blank lines are kept");
  Check(Copy("one\rtwo\rthree", copied) && copied == "one\rtwo\rthree",
        "copy: a lone carriage return is not a line break, and is kept");
  const std::string withNul = std::string("a\0b ", 4) + kLink + std::string(" \0c\n", 4);
  Check(
      Copy(withNul, copied) && copied == std::string("a\0b ", 4) + kTag + std::string(" \0c\n", 4),
      "copy: a NUL in a line is kept, before a link and after it");
  Check(Copy("caf\xC3\xA9 \xE2\x82\xAC\r\n", copied) && copied == "caf\xC3\xA9 \xE2\x82\xAC\r\n",
        "copy: UTF-8 outside a link is kept");
  // what the app redacted at the source is not redacted a second time
  const std::string already = stamp + kExe + " " + kTag + "\r\n";
  Check(Copy(already, copied) && copied == already,
        "copy: a line redacted when it was written is copied as it is");
}

UR_TEST(LogExport_ALongLogIsCopiedInOnePass) {
  const Scratch scratch;
  std::string log, want;
  for (int i = 0; i < 4000; ++i) {
    const std::string number = "line " + std::to_string(i);
    if (i % 5 == 0) {
      log += number + " " + kLink + "\r\n";
      want += number + " " + kTag + "\r\n";
    } else {
      log += number + " nothing to see here\r\n";
      want += number + " nothing to see here\r\n";
    }
  }
  std::string copied;
  Check(Copy(log, copied) && copied == want,
        "long: a log of 4000 lines, a link on one in five, is copied exactly");
  Check(copied == urnw::authlink::RedactLogText(log),
        "long: and it is exactly what redacting the whole text gives");
}

UR_TEST(LogExport_TheLogItselfIsNeverEmptied) {
  const Scratch scratch;
  const fs::path source = gDir / "urnetwork-app.log";
  const std::string log = "header\r\n" + kLink + "\r\ntrailer\r\n";
  Write(source, log);
  // saving over the log itself, spelled four ways: the picker suggests the log's own name
  Check(!CopyWithoutLinks(source, source), "same file: saving over the log is refused");
  Check(Read(source) == log, "same file: and the log is as it was, not emptied");
  Check(!CopyWithoutLinks(source, gDir / "." / "urnetwork-app.log"),
        "same file: the same file by a path with a dot in it is refused");
  Check(Read(source) == log, "same file: and untouched");
  fs::create_directory(gDir / "sub");
  Check(!CopyWithoutLinks(source, gDir / "sub" / ".." / "urnetwork-app.log"),
        "same file: by a path that goes down and up is refused");
  Check(Read(source) == log, "same file: and untouched again");
  std::error_code linked;
  fs::create_hard_link(source, gDir / "hardlink.log", linked);
  if (!linked) {
    Check(!CopyWithoutLinks(source, gDir / "hardlink.log"),
          "same file: a hard link to the log is the log, and is refused");
    Check(Read(source) == log, "same file: and untouched once more");
    fs::remove(gDir / "hardlink.log");
  }
  // and in the other direction: the log is the destination of nothing it was read from
  const fs::path elsewhere = gDir / "elsewhere.log";
  Write(elsewhere,
        "an earlier export that is longer than what will replace it, and is replaced whole");
  Check(CopyWithoutLinks(source, elsewhere) &&
            Read(elsewhere) == "header\r\n" + kTag + "\r\ntrailer\r\n",
        "overwrite: an earlier export is replaced whole, not mixed with what it held");
  Check(Read(source) == log, "overwrite: and the log is still as it was");
}

UR_TEST(LogExport_AnExistingExportSurvivesWhatCannotBeCopied) {
  const Scratch scratch;
  // what the cases before it left in the Windows spec's one directory: a log, and a directory
  Write(gDir / "urnetwork-app.log", "header\r\n" + kLink + "\r\ntrailer\r\n");
  fs::create_directory(gDir / "sub");
  const fs::path destination = gDir / "earlier.log";
  Write(destination, "the earlier export");
  Check(!CopyWithoutLinks(gDir / "no-such-log.log", destination),
        "missing: a log that is not there cannot be copied");
  Check(Read(destination) == "the earlier export",
        "missing: and an export that was already there is as it was, not emptied");
  Check(!CopyWithoutLinks(gDir / "urnetwork-app.log", gDir / "no-such-directory" / "out.log"),
        "missing: a destination in a directory that is not there cannot be written");
  Check(!fs::exists(gDir / "no-such-directory"), "missing: and nothing is made of it");
  // a directory is no log and no destination: refused, and nothing is made of the copy
  Check(!CopyWithoutLinks(gDir / "sub", gDir / "out2.log") && !fs::exists(gDir / "out2.log"),
        "directory: a directory is not copied as a log, and nothing is made of the copy");
  const std::string log = Read(gDir / "urnetwork-app.log");
  Check(!log.empty() && !CopyWithoutLinks(gDir / "urnetwork-app.log", gDir / "sub") &&
            fs::is_directory(gDir / "sub") && Read(gDir / "urnetwork-app.log") == log,
        "directory: nor is a directory a place to save an export, and the log is as it was");
}

}  // namespace

// The header writes nothing but the destination, and opens it only after the source has been found
// to be a file, opened, and found not to be the destination.
UR_TEST(LogExport_TheHeaderWritesOnlyTheDestinationAndThatLast) {
  const std::string code = urnw::testing::wiring::ReadCode("LogExport.hpp");
  for (const char* include : {"#include <filesystem>", "#include <fstream>", "#include <string>",
                              "#include <system_error>", "#include \"AuthLink.hpp\""}) {
    Check(urnw::testing::wiring::Contains(code, include),
          std::string("header: LogExport.hpp has ") + include);
  }
  Check(urnw::testing::wiring::CountOf(code, "#include") == 5, "header: and no other include");
  Check(urnw::testing::wiring::CountOf(code, "std::ofstream") == 1,
        "header: one file is opened for writing");
  for (const char* forbidden :
       {"remove(", "rename(", "copy_file(", "std::ios::app", "std::fstream", "<glib", "g_file"}) {
    Check(!urnw::testing::wiring::Contains(code, forbidden),
          std::string("header: LogExport.hpp has ") + forbidden);
  }
  const std::string body =
      urnw::testing::wiring::FunctionBody(code, "inline bool CopyWithoutLinks(");
  Check(urnw::testing::wiring::Before(
            body, "if (!std::filesystem::is_regular_file(source, unknown)) return false;",
            "std::ifstream in(source, std::ios::binary);"),
        "header: the source is found to be a file before it is opened");
  Check(urnw::testing::wiring::Before(body, "if (!in) return false;", "std::ofstream"),
        "header: and opened before anything is opened for writing");
  Check(urnw::testing::wiring::Before(
            body, "if (std::filesystem::equivalent(source, destination, unknown)) return false;",
            "std::ofstream"),
        "header: and found not to be the destination");
  Check(urnw::testing::wiring::Contains(body, "out << authlink::RedactLogText(line);"),
        "header: every line goes through RedactLogText");
}

// Settings' Export logs copies through it, and through nothing else.
UR_TEST(LogExport_SettingsExportsThroughIt) {
  const std::string page = urnw::testing::wiring::ReadCode("SettingsPage.cpp");
  const std::string save =
      urnw::testing::wiring::FunctionBody(page, "void SettingsPage::SaveLogsToFile()");
  Check(!save.empty(), "settings: SaveLogsToFile");
  Check(urnw::testing::wiring::Contains(
            save,
            "char* path = g_file_get_path(target);\n"
            "        const bool ok = path != nullptr ? "
            "logexport::CopyWithoutLinks(owned->source, path)\n"
            "                                        : "
            "CopyWithoutLinksThroughGio(owned->source, target);\n"
            "        g_free(path);"),
        "settings: the export is CopyWithoutLinks to the chosen file's path, or through GIO");
  Check(!urnw::testing::wiring::Contains(save, "g_file_copy"),
        "settings: and no plain copy of the log is left");
  // a place with no local path (a gvfs share without its fuse bridge) gets the same copy
  // without links, written to a private temporary file first, and only that goes through GIO
  const std::string gio = urnw::testing::wiring::FunctionBody(
      page, "bool CopyWithoutLinksThroughGio(const std::string& source, GFile* target) {");
  Check(!gio.empty(), "settings: CopyWithoutLinksThroughGio");
  const char* const staging[] = {
      "g_file_open_tmp(\"urnetwork-log-XXXXXX\", &staged, nullptr);",
      "if (fd < 0) return false;",
      "bool ok = logexport::CopyWithoutLinks(source, staged);",
      "if (ok) {",
      "GFile* from = g_file_new_for_path(staged);",
      "ok = g_file_copy(from, target,",
      "g_unlink(staged);"};
  for (size_t i = 0; i + 1 < std::size(staging); ++i) {
    Check(urnw::testing::wiring::Before(gio, staging[i], staging[i + 1]),
          std::string("settings: through GIO, ") + staging[i] + " comes before " + staging[i + 1]);
  }
  Check(urnw::testing::wiring::Contains(gio, "G_FILE_COPY_OVERWRITE"),
        "settings: an earlier export is replaced");
  Check(urnw::testing::wiring::CountOf(page, "g_file_copy(") == 1,
        "settings: the staged copy is the one file GIO copies");
  Check(urnw::testing::wiring::CountOf(page, "g_file_new_for_path(") == 1,
        "settings: and the log itself is never handed to GIO");
  Check(urnw::testing::wiring::Contains(
            save, "g_warning(\"settings: save logs failed: the log could not be copied\");"),
        "settings: a failure logs a closed line");
  Check(!urnw::testing::wiring::Contains(save, "error->message"), "settings: and no error text");
  Check(urnw::testing::wiring::Contains(page, "#include \"LogExport.hpp\""),
        "settings: includes LogExport.hpp");
}
