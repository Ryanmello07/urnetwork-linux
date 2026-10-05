// Whether this device provides, connected or not, for each provide control
// mode — the one lifecycle decision the GUI and the daemon both apply to the
// PROVIDER-ONLY device (support inbox 1521, P008: providers get no traffic).
//
// THE DEFECT THIS ANSWERS. Since the daemon split the provider is the daemon's
// DeviceLocal, and that device was built only inside a tunnel session
// (TunnelHost::RunStart). Disconnect sends stop_tunnel (it has to: the capture
// routes would otherwise blackhole the machine, linux 1dbec0d fix 3), and
// stop_tunnel destroys the DeviceLocal, so on Linux the provider stopped in
// EVERY mode the moment the user disconnected — and never started at all after
// a launch without auto-connect. A user who picked "Always" earned only while
// connected and was told nothing. The single-process app before the split did
// not do this: its Disconnect left the device running.
//
// WHAT THE OTHER PLATFORMS DO. Android and Apple keep their packet tunnel, and
// with it the DeviceLocal, running whenever providing is enabled
// (VpnServiceStartPolicy.kt vpnServiceRequired, VPNReconciliationSupport.swift
// VPNDesiredState.shouldRun: provideEnabled || connectEnabled || !routeLocal),
// and the sdk decides the tier. Linux keeps the DeviceLocal without the tunnel:
// while disconnected the daemon runs a provider-only device with no tun, no
// capture routes, no DNS change, no nftables change and no device RPC listener,
// so providing never changes how this machine's own traffic is routed.
//
// THE SDK SEMANTICS, mirrored and never reinterpreted
// (DeviceLocal.applyProvideControlModeWithLock, sdk/device_local.go):
//   never   -> no providing
//   always  -> public
//   network -> network (the user's own devices), connected or not
//   auto    -> public while connected, network while not
//   anything else -> no providing (the sdk's conservative default)
// "manual" defers to an explicitly set provide mode that lives in the device,
// which does not exist while disconnected; this app never offers it (the
// Connect page and onboarding offer auto/always/network/never), so it is
// treated like any other unknown mode and gets no provider-only device.
//
// Pure and header-only (C++17, no GTK, glib or SDK): tests/ProvideLifecycleTest.cpp
// runs it on any host, ControlProtocol.hpp validates start_provider with it and
// TunnelHost retires the provider-only device with it.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace urnw::provide {

enum class ControlMode { Never, Always, Network, Auto, Unknown };

constexpr ControlMode ControlModeFrom(std::string_view mode) {
  if (mode == "never") return ControlMode::Never;
  if (mode == "always") return ControlMode::Always;
  if (mode == "network") return ControlMode::Network;
  if (mode == "auto") return ControlMode::Auto;
  return ControlMode::Unknown;
}

// For logs: the mode's own name, never the raw string a client sent.
constexpr const char* ToString(ControlMode mode) {
  switch (mode) {
    case ControlMode::Never: return "never";
    case ControlMode::Always: return "always";
    case ControlMode::Network: return "network";
    case ControlMode::Auto: return "auto";
    case ControlMode::Unknown: break;
  }
  return "unknown";
}

// The live provide tier, with the sdk's values (urnet::ProvideModeNone,
// ProvideModeNetwork, ProvideModePublic), so a tier and a status field compare
// directly.
enum class Tier : int64_t { None = 0, Network = 1, Public = 3 };

// The tier the sdk applies for `mode`. `connected` is the sdk's own test: a
// destination is set on a tunnel session.
constexpr Tier SdkTierFor(ControlMode mode, bool connected) {
  switch (mode) {
    case ControlMode::Always:
      return Tier::Public;
    case ControlMode::Network:
      return Tier::Network;
    case ControlMode::Auto:
      return connected ? Tier::Public : Tier::Network;
    case ControlMode::Never:
    case ControlMode::Unknown:
      break;
  }
  return Tier::None;
}

// THE LIFECYCLE DECISION: does this device provide? Connected, the tunnel
// session's DeviceLocal is the provider. Disconnected, a provider-only device
// runs exactly when the answer is true.
constexpr bool ProviderRuns(ControlMode mode, bool connected) {
  return SdkTierFor(mode, connected) != Tier::None;
}

// What the daemon's `status` says, as the GUI's disconnected step reads it.
// providerControlMode points into the status it was read from.
struct DaemonProviderFacts {
  // A status answered at all. Without one nothing changes.
  bool answered = false;
  // The status was cut down for another user: their session, not ours.
  bool redacted = false;
  // A tunnel session is starting, up or stopping: its DeviceLocal provides.
  bool tunnelSession = false;
  // The kill-switch floor holds this machine blocked after an unexpected drop.
  bool killSwitchArmed = false;
  bool providerRunning = false;
  // A control client owns the session (status owner_connected).
  bool ownerConnected = false;
  std::string_view providerControlMode;
};

enum class DisconnectedStep {
  None,
  // start_provider. Also how the GUI adopts a provider whose owner went away
  // (a GUI restart): the daemon keeps a device built from the same request.
  Start,
  // set_provide: the running provider follows the newly chosen mode.
  Apply,
  // set_provide with a mode that does not provide: the daemon retires the
  // provider-only device.
  Stop,
};

// The step the GUI takes while it has no tunnel session. `settingsChanged`:
// the provider transport policy was just edited, which only a new device
// picks up (the daemon rebuilds when the request differs).
constexpr DisconnectedStep DisconnectedProviderStep(std::string_view controlMode,
                                                    const DaemonProviderFacts& facts,
                                                    bool settingsChanged = false) {
  if (!facts.answered || facts.redacted || facts.tunnelSession) return DisconnectedStep::None;
  if (!ProviderRuns(ControlModeFrom(controlMode), /*connected=*/false)) {
    return facts.providerRunning ? DisconnectedStep::Stop : DisconnectedStep::None;
  }
  // The armed floor stays the only thing in force until the user reconnects or
  // lifts it; providing resumes then. The daemon refuses a start here as well.
  if (facts.killSwitchArmed) return DisconnectedStep::None;
  if (!facts.providerRunning || !facts.ownerConnected || settingsChanged) {
    return DisconnectedStep::Start;
  }
  if (facts.providerControlMode != controlMode) return DisconnectedStep::Apply;
  return DisconnectedStep::None;
}

// Pacing for a provider step the daemon refused or could not run, so the
// 5 s health poll cannot turn a failure into a construction loop: 5 s, then
// doubling to 5 minutes. A user action starts over (NoteSuccess).
class ProviderStepBackoff {
 public:
  static constexpr int64_t kFirstDelayMillis = 5 * 1000;
  static constexpr int64_t kMaxDelayMillis = 5 * 60 * 1000;

  bool Allows(int64_t nowMillis) const { return nowMillis >= notBeforeMillis_; }

  void NoteFailure(int64_t nowMillis) {
    delayMillis_ =
        delayMillis_ == 0 ? kFirstDelayMillis : std::min(delayMillis_ * 2, kMaxDelayMillis);
    notBeforeMillis_ = nowMillis + delayMillis_;
  }

  void NoteSuccess() {
    delayMillis_ = 0;
    notBeforeMillis_ = 0;
  }

  int64_t DelayMillis() const { return delayMillis_; }

 private:
  int64_t delayMillis_ = 0;
  int64_t notBeforeMillis_ = 0;
};

}  // namespace urnw::provide
