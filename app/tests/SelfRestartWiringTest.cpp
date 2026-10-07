// The call sites of the daemon's self-restart (daemon/SelfRestart.hpp): an
// SdkUnresponsive verdict lands the machine and then ends the process without
// one more call on the device, the next daemon publishes the stop before it
// serves anyone, and the unit restarts on the exit. TunnelHost and main.cpp
// need glib and the SDK, so this reads them with the line comments blanked,
// and the unit with its comment lines dropped, so prose cannot satisfy it.
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

// The source with every // comment blanked; string literals are kept.
std::string ReadRestartSource(const std::string& relative) {
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

std::string RestartBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool RestartHas(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Each needle occurs after the one before it.
bool InOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

// The [Service] directives, comments and blank lines dropped.
std::vector<std::string> ServiceSection() {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../packaging/urnetworkd.service");
  std::vector<std::string> lines;
  std::string line;
  bool inSection = false;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    if (line[0] == '[') {
      inSection = line == "[Service]";
      continue;
    }
    if (inSection) lines.push_back(line);
  }
  return lines;
}

}  // namespace

// Only the verdict that proves the SDK wedged restarts, and only once the
// machine is landed: after the floor's landing, before the device half.
UR_TEST(SelfRestartWiring_OnlyAWedgedSdkRestartsAfterTheLanding) {
  const std::string host = ReadRestartSource("daemon/TunnelHost.cpp");
  const std::string check = RestartBody(host, "bool TunnelHost::CheckDeadTunnelLocked() {");
  UR_EXPECT_TRUE(InOrder(check, {"StopUnsafeSessionLocked(", "/*sdkWedged=*/tick.verdict.reason ==",
                                 "watchdog::DeadTunnelReason::SdkUnresponsive);"}));
  const std::string unsafe = RestartBody(host, "void TunnelHost::StopUnsafeSessionLocked(");
  UR_EXPECT_TRUE(InOrder(unsafe, {"RevertSessionMachineLocked();",
                                  "ApplyFilterLocked(FilterState::Armed",
                                  "ApplyFilterLocked(FilterState::Off",
                                  "if (sdkWedged) RestartForWedgedSdkLocked(reason, detail, code);",
                                  "StopInternalLocked(std::string());"}));
  // No other caller: every other teardown's device half runs as before.
  size_t calls = 0;
  for (size_t at = host.find("RestartForWedgedSdkLocked("); at != std::string::npos;
       at = host.find("RestartForWedgedSdkLocked(", at + 1)) {
    ++calls;
  }
  UR_EXPECT_EQ(2u, calls);  // the definition and the one call
}

// The restart writes the record, detaches the marker the cgroup would keep,
// and ends with _exit, making no call on the device or anything of the SDK.
UR_TEST(SelfRestartWiring_TheRestartLeavesTheRecordAndNeverCallsTheSdk) {
  const std::string host = ReadRestartSource("daemon/TunnelHost.cpp");
  const std::string restart = RestartBody(host, "void TunnelHost::RestartForWedgedSdkLocked(");
  UR_EXPECT_TRUE(InOrder(restart, {"WriteStopRecord(record, &writeError)",
                                   "egressMarker_.Detach();",
                                   "::_exit(selfrestart::kExitStatus);"}));
  UR_EXPECT_FALSE(RestartHas(restart, "device_"));
  UR_EXPECT_FALSE(RestartHas(restart, "urnet::"));
  UR_EXPECT_FALSE(RestartHas(restart, "support::"));
  UR_EXPECT_FALSE(RestartHas(restart, "StopInternalLocked("));
  const std::string write = RestartBody(host, "bool WriteStopRecord(");
  UR_EXPECT_TRUE(InOrder(write, {"selfrestart::EncodeStopRecord(record)", "::open(staged.c_str()",
                                 "::rename(staged.c_str(), path.c_str())"}));
  UR_EXPECT_FALSE(RestartHas(write, "urnet::"));
}

// The next daemon publishes the stop before its control socket serves anyone,
// after the armed floor it may sit behind is adopted, and reads it once.
UR_TEST(SelfRestartWiring_TheNextDaemonPublishesTheStopBeforeItServes) {
  const std::string main = ReadRestartSource("daemon/main.cpp");
  UR_EXPECT_TRUE(InOrder(main, {"int main(", "if (sweptArmedFloor) tunnel.AdoptArmedFloor();",
                                "tunnel.RestoreStopRecord();", "server.Start()"}));
  const std::string host = ReadRestartSource("daemon/TunnelHost.cpp");
  const std::string restore = RestartBody(host, "void TunnelHost::RestoreStopRecord() {");
  UR_EXPECT_TRUE(InOrder(restore, {"::unlink(selfrestart::kStopRecordPath);",
                                   "selfrestart::DecodeStopRecord(text, WatchdogMillis())",
                                   "status_.tunnel_state = ctl::TunnelState::Error;",
                                   "status_.stop_reason = record->stopReason;",
                                   "status_.error = record->error;",
                                   "status_.error_code = record->code;"}));
}

// The unit restarts on the exit: on-failure, and no directive that would make
// status 75 a success or keep the unit down.
UR_TEST(SelfRestartWiring_TheUnitRestartsOnTheExit) {
  const std::vector<std::string> service = ServiceSection();
  UR_EXPECT_TRUE(!service.empty());
  bool onFailure = false;
  for (const std::string& line : service) {
    if (line == "Restart=on-failure") onFailure = true;
    UR_EXPECT_TRUE(line.rfind("RestartPreventExitStatus=", 0) != 0);
    UR_EXPECT_TRUE(line.rfind("SuccessExitStatus=", 0) != 0);
  }
  UR_EXPECT_TRUE(onFailure);
}
