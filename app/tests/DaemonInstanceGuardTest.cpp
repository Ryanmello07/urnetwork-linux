// One urnetworkd per machine (daemon/InstanceGuard.hpp): the decision for the
// daemon's start, --revert and the unit's ExecStopPost, the socket probe's
// reading, and the call sites in daemon/main.cpp, which need glib and the SDK
// and are read as source with the line comments blanked.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "daemon/InstanceGuard.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace instance = urnw::instance;

namespace {

instance::Probe ProbeOf(bool lockHeld, bool socketAnswers) {
  instance::Probe probe;
  probe.lockHeld = lockHeld;
  probe.socketAnswers = socketAnswers;
  return probe;
}

// The source with every // comment blanked; string literals are kept.
std::string ReadGuardSource(const std::string& relative) {
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

UR_TEST(DaemonInstanceGuard_NothingLiveProceedsInEveryMode) {
  const instance::Probe none = ProbeOf(false, false);
  for (bool force : {false, true}) {
    UR_EXPECT_TRUE(instance::Decide(instance::Mode::Serve, none, force) ==
                   instance::Verdict::Proceed);
    UR_EXPECT_TRUE(instance::Decide(instance::Mode::Revert, none, force) ==
                   instance::Verdict::Proceed);
    UR_EXPECT_TRUE(instance::Decide(instance::Mode::RevertUnlessArmed, none, force) ==
                   instance::Verdict::Proceed);
  }
}

// Either probe alone is a live daemon: the lock for every daemon from this one
// on, the socket for one that predates the lock.
UR_TEST(DaemonInstanceGuard_ALiveDaemonRefusesASecondAndTheExitSweep) {
  for (const instance::Probe& live :
       {ProbeOf(true, false), ProbeOf(false, true), ProbeOf(true, true)}) {
    UR_EXPECT_TRUE(instance::OtherDaemonLive(live));
    for (bool force : {false, true}) {
      UR_EXPECT_TRUE(instance::Decide(instance::Mode::Serve, live, force) ==
                     instance::Verdict::Refuse);
      UR_EXPECT_TRUE(instance::Decide(instance::Mode::RevertUnlessArmed, live, force) ==
                     instance::Verdict::Refuse);
    }
  }
  UR_EXPECT_FALSE(instance::OtherDaemonLive(ProbeOf(false, false)));
}

UR_TEST(DaemonInstanceGuard_RevertUnderALiveDaemonNeedsForce) {
  for (const instance::Probe& live :
       {ProbeOf(true, false), ProbeOf(false, true), ProbeOf(true, true)}) {
    UR_EXPECT_TRUE(instance::Decide(instance::Mode::Revert, live, /*force=*/false) ==
                   instance::Verdict::Refuse);
    UR_EXPECT_TRUE(instance::Decide(instance::Mode::Revert, live, /*force=*/true) ==
                   instance::Verdict::ProceedWithWarning);
  }
}

// A listener answers, a full backlog included; a socket file a dead daemon left
// behind refuses, and no file is no daemon.
UR_TEST(DaemonInstanceGuard_SocketProbeReadsConnect) {
  UR_EXPECT_TRUE(instance::SocketAnswers(0, 0));
  UR_EXPECT_TRUE(instance::SocketAnswers(-1, EAGAIN));
  UR_EXPECT_TRUE(instance::SocketAnswers(-1, EINPROGRESS));
  UR_EXPECT_FALSE(instance::SocketAnswers(-1, ECONNREFUSED));
  UR_EXPECT_FALSE(instance::SocketAnswers(-1, ENOENT));
  UR_EXPECT_FALSE(instance::SocketAnswers(-1, EACCES));
}

UR_TEST(DaemonInstanceGuard_LockLivesBesideTheSocketOnTmpfs) {
  UR_EXPECT_TRUE(std::string(instance::kInstanceLockPath).rfind("/run/urnetwork/", 0) == 0);
}

// The daemon decides before anything it does to the machine: the location
// override's clear, the startup sweep, the control socket's bind.
UR_TEST(DaemonInstanceGuard_ServeDecidesBeforeTheSweep) {
  const std::string main = ReadGuardSource("daemon/main.cpp");
  UR_EXPECT_TRUE(InOrder(main, {"int main(", "ProbeOtherDaemon(&instanceLockFd)",
                                "instance::Mode::Serve", "return 1;", "geoWriter.Clear()",
                                "SweepStaleState(/*preserveArmed=*/true)", "server.Start()"}));
}

// Both revert flags decide before their sweep, and the exit sweep beside a live
// daemon succeeds without sweeping.
UR_TEST(DaemonInstanceGuard_RevertDecidesBeforeTheSweep) {
  const std::string main = ReadGuardSource("daemon/main.cpp");
  UR_EXPECT_TRUE(InOrder(main, {"arg == \"--revert-unless-armed\"",
                                "ProbeOtherDaemon(&instanceLockFd)",
                                "instance::Mode::RevertUnlessArmed", "instance::Mode::Revert",
                                "verdict == urnw::instance::Verdict::Refuse && preserveArmed",
                                "return 0;", "verdict == urnw::instance::Verdict::Refuse",
                                "return 1;", "NetFilter::SweepStaleState(preserveArmed)"}));
  // The old socket-file check warned and swept anyway.
  UR_EXPECT_TRUE(main.find("::access(urnw::ControlServer::SocketPath().c_str(), F_OK)") ==
                 std::string::npos);
}
