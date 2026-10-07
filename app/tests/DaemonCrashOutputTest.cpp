// Where the daemon's Go runtime fatal output goes (packaging/urnetworkd.service):
// the journal, with every goroutine's stack, then a core. Go reads GOTRACEBACK
// before main, so the unit is the only place it can come from; this reads the
// unit with its comment lines dropped, so prose cannot satisfy it.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

// The directives of `section` ("[Service]"), one per entry, comments and blank
// lines dropped.
std::vector<std::string> UnitSection(const std::string& section) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../packaging/urnetworkd.service");
  std::vector<std::string> lines;
  std::string line;
  bool inSection = false;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    if (line[0] == '[') {
      inSection = line == section;
      continue;
    }
    if (inSection) lines.push_back(line);
  }
  return lines;
}

bool HasDirective(const std::vector<std::string>& lines, const std::string& directive) {
  for (const std::string& line : lines) {
    if (line == directive) return true;
  }
  return false;
}

bool HasKey(const std::vector<std::string>& lines, const std::string& key) {
  for (const std::string& line : lines) {
    if (line.rfind(key + "=", 0) == 0) return true;
  }
  return false;
}

}  // namespace

UR_TEST(DaemonCrashOutput_TheUnitSetsGotracebackCrash) {
  const std::vector<std::string> service = UnitSection("[Service]");
  UR_EXPECT_TRUE(!service.empty());
  UR_EXPECT_TRUE(HasDirective(service, "Environment=GOTRACEBACK=crash"));
}

// fd 2 is the only place a Go fatal is written: sending it anywhere but the
// journal would lose the traceback the line above asks for.
UR_TEST(DaemonCrashOutput_StderrStaysInTheJournal) {
  const std::vector<std::string> service = UnitSection("[Service]");
  UR_EXPECT_TRUE(!service.empty());
  UR_EXPECT_FALSE(HasKey(service, "StandardError"));
}
