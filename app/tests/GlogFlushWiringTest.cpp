// Where the daemon flushes the SDK's glog buffers (urnet::flushGlog): only on
// the flusher's own thread (daemon/GlogFlusher.cpp), once a second while a
// device runs and once soon after every bring-up and stop, which ask for it
// without waiting. The calls need the SDK, so this reads the daemon's sources
// with the line comments blanked, so prose cannot satisfy it.
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
std::string ReadFlushSource(const std::string& relative) {
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

std::string FlushBody(const std::string& source, const std::string& signature) {
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

}  // namespace

// One thread flushes: every second while active, at once when asked, and
// nowhere else. It is started before the main loop runs.
UR_TEST(GlogFlushWiring_OneThreadFlushesWhileActiveOrAsked) {
  const std::string flusher = ReadFlushSource("daemon/GlogFlusher.cpp");
  const std::string run = FlushBody(flusher, "void Run() {");
  UR_EXPECT_TRUE(InOrder(run, {"for (;;)", "state.wake.wait_for(lock, kActiveInterval,",
                               "if (requested || state.active.load()) urnet::flushGlog();"}));
  UR_EXPECT_TRUE(InOrder(FlushBody(flusher, "void Start() {"),
                         {"std::thread(&Run).detach();"}));
  UR_EXPECT_TRUE(flusher.find("std::chrono::seconds kActiveInterval{1};") != std::string::npos);
  // Asking never waits on the flush.
  const std::string request = FlushBody(flusher, "void Request() {");
  UR_EXPECT_TRUE(request.find("flushGlog") == std::string::npos);
  UR_EXPECT_TRUE(request.find("wait") == std::string::npos);
  const std::string main = ReadFlushSource("daemon/main.cpp");
  UR_EXPECT_TRUE(InOrder(main, {"int main(", "urnw::glogflush::Start();", "g_main_loop_run(loop);"}));
  UR_EXPECT_TRUE(main.find("flushGlog") == std::string::npos);
}

// The bring-up's and the stop's own lines are asked for once the work is done,
// never flushed on the caller's thread (the main loop, for a stop), and the
// reaper keeps the every-second flush to the time a device runs.
UR_TEST(GlogFlushWiring_StartsAndStopsAskAndTheReaperSaysWhenActive) {
  const std::string host = ReadFlushSource("daemon/TunnelHost.cpp");
  UR_EXPECT_TRUE(host.find("flushGlog") == std::string::npos);
  const std::string stop = FlushBody(host, "void TunnelHost::Stop(");
  UR_EXPECT_TRUE(InOrder(stop, {"std::scoped_lock lock(opMutex_);", "StopInternalLocked(reason);",
                                "}", "glogflush::Request();"}));
  const std::string start = FlushBody(host, "void TunnelHost::RunStart(");
  UR_EXPECT_TRUE(InOrder(start, {"busy_.store(false);", "glogflush::Request();"}));
  const std::string request = FlushBody(host, "ctl::StatusReply TunnelHost::Start(");
  UR_EXPECT_TRUE(InOrder(request, {"busy_.store(true);", "glogflush::SetActive(true);"}));
  const std::string reap = FlushBody(host, "void TunnelHost::Reap() {");
  UR_EXPECT_TRUE(InOrder(reap, {"if (!lock.owns_lock()) return;",
                                "glogflush::SetActive(device_.has_value() || "
                                "providerDevice_.has_value() ||",
                                "uploadDevice_.has_value());"}));
}
