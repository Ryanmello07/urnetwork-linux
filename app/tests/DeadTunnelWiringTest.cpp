// The call sites of the dead-tunnel failsafe in the daemon (TunnelWatchdog.hpp):
// watched from the up edge, unwatched before any teardown, judged on the reaper
// after the guards that end a session for worse, ended through the protective
// teardown, and sampled without touching the session's device object. The
// verdict is pure and runs in TunnelWatchdogTest.cpp; TunnelHost and the
// sampler need glib and the SDK, so this reads their sources with the line
// comments blanked, so prose cannot satisfy a contract.
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
std::string ReadDeadTunnelSource(const std::string& relative) {
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

std::string DeadTunnelBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool DeadTunnelHas(const std::string& text, const std::string& needle) {
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

}  // namespace

// Watched from the block that publishes Up, and unwatched as the first act of
// every teardown, before the device is closed.
UR_TEST(DeadTunnelWiring_WatchedFromTheUpEdgeToTheTeardown) {
  const std::string host = ReadDeadTunnelSource("daemon/TunnelHost.cpp");
  const std::string start = DeadTunnelBody(host, "void TunnelHost::RunStart(");
  UR_EXPECT_TRUE(InOrder(start, {"status_.tunnel_state = ctl::TunnelState::Up;",
                                 "StartDeadTunnelWatchLocked();", "} catch ("}));
  const std::string stop = DeadTunnelBody(host, "void TunnelHost::StopInternalLocked(");
  UR_EXPECT_TRUE(InOrder(stop, {"StopDeadTunnelWatchLocked();", "RetireProviderDeviceLocked();",
                                "device_->close();", "status_.failsafe_armed = false;"}));
  const std::string unwatch = DeadTunnelBody(host, "void TunnelHost::StopDeadTunnelWatchLocked(");
  UR_EXPECT_TRUE(DeadTunnelHas(unwatch, "exitSampler_.Stop();"));
  // A new bring-up starts with no countdown.
  const std::string request = DeadTunnelBody(host, "ctl::StatusReply TunnelHost::Start(");
  UR_EXPECT_TRUE(DeadTunnelHas(request, "status_.failsafe_armed = false;"));
}

// Judged once the storm guard, the egress witness and a bring-up have had
// their say, and never while the io loop's own death waits to be explained.
UR_TEST(DeadTunnelWiring_TheReaperJudgesAfterTheGuards) {
  const std::string host = ReadDeadTunnelSource("daemon/TunnelHost.cpp");
  const std::string reap = DeadTunnelBody(host, "void TunnelHost::Reap() {");
  UR_EXPECT_TRUE(InOrder(reap, {"CheckTunnelStormLocked()", "CheckEgressWitnessLocked()",
                                "if (busy_.load()) return;", "if (CheckDeadTunnelLocked()) return;",
                                "const bool died = ioLoopDied_.load();"}));
  const std::string check = DeadTunnelBody(host, "bool TunnelHost::CheckDeadTunnelLocked() {");
  UR_EXPECT_TRUE(InOrder(check, {"ioLoopDied_.load()", "return false;"}));
}

// The tun's counters in the right directions, the sampler's readings, and a
// dead verdict ended through the protective teardown with the failsafe's
// reason and code: the armed floor or no table, the outcome checked, published
// last. Never StopInternalLocked with a reason, which lifts the floor.
UR_TEST(DeadTunnelWiring_ADeadVerdictEndsThroughTheProtectiveTeardown) {
  const std::string host = ReadDeadTunnelSource("daemon/TunnelHost.cpp");
  const std::string check = DeadTunnelBody(host, "bool TunnelHost::CheckDeadTunnelLocked() {");
  UR_EXPECT_TRUE(
      DeadTunnelHas(check, "inputs.outboundPackets = ReadIfaceCounter(iface, \"tx_packets\");"));
  UR_EXPECT_TRUE(
      DeadTunnelHas(check, "inputs.inboundPackets = ReadIfaceCounter(iface, \"rx_packets\");"));
  UR_EXPECT_TRUE(DeadTunnelHas(check, "exitSampler_.Read()"));
  UR_EXPECT_TRUE(InOrder(
      check, {"deadTunnelWatch_.Tick(inputs)", "status_.failsafe_armed =",
              "StopUnsafeSessionLocked(watchdog::StopReasonOf(tick.verdict.reason)",
              "ctl::kCodeTunnelDead", "return true;"}));
  UR_EXPECT_FALSE(DeadTunnelHas(check, "StopInternalLocked("));
  // The clock that runs through a suspend.
  const std::string clock = DeadTunnelBody(host, "int64_t WatchdogMillis() {");
  UR_EXPECT_TRUE(DeadTunnelHas(clock, "CLOCK_BOOTTIME"));
}

// The sampler holds the device's C handle, never the session's DeviceLocal,
// and the session never waits for it.
UR_TEST(DeadTunnelWiring_TheSamplerNeverTouchesTheDeviceObject) {
  const std::string sampler = ReadDeadTunnelSource("daemon/ExitSampler.cpp");
  UR_EXPECT_TRUE(!sampler.empty());
  UR_EXPECT_TRUE(DeadTunnelHas(sampler, "urnet_device_local_get_exits(channel->deviceHandle)"));
  UR_EXPECT_TRUE(DeadTunnelHas(sampler, "urnet_device_get_window_status(channel->deviceHandle)"));
  UR_EXPECT_FALSE(DeadTunnelHas(sampler, "DeviceLocal"));
  UR_EXPECT_TRUE(DeadTunnelHas(sampler, ".detach();"));
  UR_EXPECT_FALSE(DeadTunnelHas(sampler, ".join("));
  const std::string run = DeadTunnelBody(sampler, "void ExitSampler::Run(");
  // Published last: a completed sample, never an attempted one.
  UR_EXPECT_TRUE(InOrder(run, {"urnet_device_local_get_exits(", "channel->cancelled.load()",
                               "channel->provenCount.store(proven);",
                               "channel->lastSampleMillis.store(now);"}));
  const std::string host = ReadDeadTunnelSource("daemon/TunnelHost.cpp");
  const std::string watch = DeadTunnelBody(host, "void TunnelHost::StartDeadTunnelWatchLocked() {");
  UR_EXPECT_TRUE(
      DeadTunnelHas(watch, "if (!exitSampler_.Start(device_->handle(), &WatchdogMillis))"));
  // The up edge's level comes from the sampler's first sample: the bring-up
  // makes no call on the device for it.
  UR_EXPECT_FALSE(DeadTunnelHas(watch, "device_->get"));
  UR_EXPECT_TRUE(DeadTunnelHas(watch, "inputs.windowSampled = false;"));
  const std::string check = DeadTunnelBody(host, "bool TunnelHost::CheckDeadTunnelLocked() {");
  UR_EXPECT_TRUE(DeadTunnelHas(check, "inputs.windowSampled = sample.sampled;"));
}

// A network change reaches a watched session's device through the sampler,
// which tells the device before it samples, and never from the reaper on the
// main loop: the call takes the device's state lock, and a wedged device held
// there would stop the verdict that ends its session. The provider-only
// device, which nothing samples, is still told by the reaper.
UR_TEST(DeadTunnelWiring_NetworkChangesReachTheSessionThroughTheSampler) {
  const std::string host = ReadDeadTunnelSource("daemon/TunnelHost.cpp");
  const std::string reap = DeadTunnelBody(host, "void TunnelHost::Reap() {");
  UR_EXPECT_TRUE(InOrder(reap, {"networkQualityTracker_.Observe(",
                                "device_ && deadTunnelWatching_",
                                "exitSampler_.NoteNetworkChange(path);", "} else if (",
                                "liveDevice->networkChanged();",
                                "liveDevice->networkQualityChanged();"}));
  UR_EXPECT_FALSE(DeadTunnelHas(reap, "device_->networkChanged"));
  UR_EXPECT_FALSE(DeadTunnelHas(reap, "device_->networkQualityChanged"));
  const std::string sampler = ReadDeadTunnelSource("daemon/ExitSampler.cpp");
  const std::string run = DeadTunnelBody(sampler, "void ExitSampler::Run(");
  UR_EXPECT_TRUE(InOrder(run, {"urnet_device_local_network_changed(channel->deviceHandle);",
                               "urnet_device_local_network_quality_changed(channel->deviceHandle);",
                               "urnet_device_get_window_status(channel->deviceHandle)"}));
  // A kick wakes the sampler's wait.
  UR_EXPECT_TRUE(DeadTunnelHas(run, "channel->kick != Channel::Kick::None"));
}

// The countdown's end is logged for the reason it ended: never "carrying
// again" on the tick that ends the session, nor on a rebase.
UR_TEST(DeadTunnelWiring_TheCountdownsEndIsLoggedTruthfully) {
  const std::string host = ReadDeadTunnelSource("daemon/TunnelHost.cpp");
  const std::string check = DeadTunnelBody(host, "bool TunnelHost::CheckDeadTunnelLocked() {");
  UR_EXPECT_TRUE(InOrder(check, {"armedChanged && tick.froze",
                                 "armedChanged && tick.verdict.reason == "
                                 "watchdog::DeadTunnelReason::None",
                                 "the tunnel is carrying again"}));
  UR_EXPECT_FALSE(DeadTunnelHas(check, "} else if (armedChanged) {"));
}
