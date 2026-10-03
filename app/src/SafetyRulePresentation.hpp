// How a split-rules activity row reads the block action's Reason: whether the
// URnetwork safety rules decided it (the "Safety rule" chip) and whether to
// offer "Route locally" through the existing local split-rule editor. No GTK,
// no SDK: SplitRulesSheet feeds it the action's fields and draws what comes
// back. Unit-tested in tests/SafetyRulePresentationTest.cpp.
//
// The reason strings are the SDK's urnet::BlockActionReason* constants
// (sdk device.go), repeated here so the header stays SDK-free for the test
// runner; IsSecurity and RouteLocalOverridable mirror the Go BlockAction
// methods of the same names, which do not cross the c abi (data crosses as
// json, methods do not).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {
namespace safety_rule {

inline constexpr const char* kReasonSecurityEncrypted = "security-encrypted";
inline constexpr const char* kReasonSecurityBittorrent = "security-bittorrent";
inline constexpr const char* kReasonSecurityPort = "security-port";
inline constexpr const char* kReasonSecurityIp = "security-ip";
inline constexpr const char* kReasonSecuritySmtp = "security-smtp";
inline constexpr const char* kReasonSecurity = "security";

// the URnetwork safety rules decided the action (sdk BlockAction.IsSecurity)
inline bool IsSecurity(const std::string& reason) {
  return reason == kReasonSecurityEncrypted || reason == kReasonSecurityBittorrent ||
         reason == kReasonSecurityPort || reason == kReasonSecurityIp ||
         reason == kReasonSecuritySmtp || reason == kReasonSecurity;
}

// a route-local rule can make the blocked traffic work outside the tunnel
// (sdk BlockAction.RouteLocalOverridable). BitTorrent and non-public
// destinations never qualify.
inline bool RouteLocalOverridable(const std::string& reason) {
  return reason == kReasonSecurityEncrypted || reason == kReasonSecurityPort;
}

// any trace of a user override on the action: the deciding override id, or a
// block or route override
inline bool OverrideApplied(const std::string& overrideId, bool hasBlockOverride,
                            bool hasRouteOverride) {
  return !overrideId.empty() || hasBlockOverride || hasRouteOverride;
}

struct ActivityRow {
  bool safetyRule = false;       // show the "Safety rule" chip and its detail
  bool offerRouteLocal = false;  // show "Route locally" (opens the local rule editor)
};

// The offer is only for a row the safety rules BLOCKED (kill switch on): with
// the kill switch off the same traffic is already routed locally, and a row an
// override decided already edits that rule on tap.
inline ActivityRow PresentActivityRow(const std::string& reason, bool block,
                                      bool overrideApplied) {
  ActivityRow row;
  row.safetyRule = IsSecurity(reason);
  row.offerRouteLocal = row.safetyRule && block && !overrideApplied &&
                        RouteLocalOverridable(reason);
  return row;
}

}  // namespace safety_rule
}  // namespace urnw
