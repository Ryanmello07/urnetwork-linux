// The order of every tunnel teardown in the daemon (docs/linux_agent_help.md
// 6.5, phase 1 before phase 2): the machine (DNS, routes, policy rules) and
// then the firewall's landing, before any call on the device, each of which
// takes the device's state lock and stops the teardown where it stands when
// the SDK has wedged. TunnelHost needs glib and the SDK, so this reads its
// source with the line comments blanked, so prose cannot satisfy it.
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
std::string ReadTeardownSource(const std::string& relative) {
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

std::string TeardownBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
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

// The landings every involuntary teardown chooses between, then the device.
const std::vector<std::string> kLandThenDevice = {
    "RevertSessionMachineLocked();", "ApplyFilterLocked(FilterState::Armed",
    "ApplyFilterLocked(FilterState::Off", "StopInternalLocked(std::string());"};

}  // namespace

// The machine half never calls the device.
UR_TEST(TeardownOrder_TheMachineHalfNeverWaitsOnTheDevice) {
  const std::string host = ReadTeardownSource("daemon/TunnelHost.cpp");
  const std::string revert = TeardownBody(host, "void TunnelHost::RevertSessionMachineLocked() {");
  UR_EXPECT_TRUE(!revert.empty());
  UR_EXPECT_TRUE(InOrder(revert, {"sessionGeneration_.fetch_add(1);", "tunnel_->RevertDns();",
                                  "ioLoop_->close();", "tunnel_.reset();"}));
  UR_EXPECT_TRUE(revert.find("device_->") == std::string::npos);
}

// A stop somebody asked for lifts the policy before the device's calls.
UR_TEST(TeardownOrder_AnExplicitStopLiftsThePolicyBeforeTheDevice) {
  const std::string host = ReadTeardownSource("daemon/TunnelHost.cpp");
  const std::string stop = TeardownBody(host, "void TunnelHost::StopInternalLocked(");
  UR_EXPECT_TRUE(InOrder(stop, {"RevertSessionMachineLocked();",
                                "ApplyFilterLocked(FilterState::Off, &ignored);",
                                "device_->setTunnelStarted(false);", "device_->close();",
                                "ReleaseDeviceLocked(device_);", "egressMarker_.Detach();"}));
  // Nothing on the device ahead of the machine.
  const size_t revertAt = stop.find("RevertSessionMachineLocked();");
  UR_EXPECT_TRUE(revertAt != std::string::npos && stop.find("device_->") > revertAt);
}

// The protective teardown, the IoLoop's death and a failed start land the
// floor after the machine and before the device, and publish after both.
UR_TEST(TeardownOrder_EveryLandingComesBeforeTheDevice) {
  const std::string host = ReadTeardownSource("daemon/TunnelHost.cpp");
  std::vector<std::string> unsafe = kLandThenDevice;
  unsafe.push_back("status_.tunnel_state = ctl::TunnelState::Error;");
  UR_EXPECT_TRUE(
      InOrder(TeardownBody(host, "void TunnelHost::StopUnsafeSessionLocked("), unsafe));

  std::vector<std::string> ioLoop = kLandThenDevice;
  ioLoop.push_back("status_.stop_reason = \"io_loop\";");
  UR_EXPECT_TRUE(InOrder(TeardownBody(host, "void TunnelHost::Reap() {"), ioLoop));

  const std::string start = TeardownBody(host, "void TunnelHost::RunStart(");
  const size_t failed = start.find("} catch (const std::exception& e) {");
  UR_EXPECT_TRUE(failed != std::string::npos);
  std::vector<std::string> failedStart = kLandThenDevice;
  failedStart.push_back("status_.stop_reason = \"start_failed\";");
  UR_EXPECT_TRUE(failed != std::string::npos && InOrder(start.substr(failed), failedStart));
}
