// The connect inspector's per-connection quick actions (Block or Allow this
// host, Bypass the tunnel or Always tunnel), and the same pair on an activity
// row's menu: what each button reads, what it is labelled, and what a press
// does.
//
// The state comes from the live overrides list, never from the block action:
// an action is a snapshot of the decision as it was made and does not change
// when a rule lands afterwards, which is exactly when the user reaches for
// these buttons. A button is on while a host rule of its kind covers the
// connection, and a press removes that rule; otherwise a press creates the
// rule of the inverse polarity (a blocked connection gets an allowing rule,
// a tunnelled one a blocking rule; a bypassed connection gets an always-tunnel
// rule, a tunnelled one a bypass rule). The label names what the press leaves
// behind and follows the rule in force over the stale verdict.
//
// No GTK, no SDK: ConnectPage feeds it the overrides and the decision and runs
// the effect through SdkHost. Ported from the Windows inspector
// (urnetwork/windows 514a1ee, ade8deb). Unit-tested in
// tests/QuickActionTest.cpp.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace urnw::quick_action {

// One host-keyed override on the shared block-action-overrides list, with
// its kind and value. App-keyed overrides carry no hosts and never match a
// connection, so they are left out.
struct HostRule {
  std::string overrideId;
  std::vector<std::string> hosts;
  bool hasBlockOverride = false;
  bool block = false;  // with hasBlockOverride: true blocks, false allows
  bool hasRouteOverride = false;
  bool routeLocal = false;  // with hasRouteOverride: true bypasses the tunnel
};

// What the quick actions read of one routing decision.
struct Facts {
  std::vector<std::string> hosts;
  std::vector<std::string> ips;
  std::vector<std::string> matchedHosts;
  std::vector<std::string> matchedIps;
  std::string overrideId;  // the override that decided it, if any
  bool hasBlockOverride = false;
  bool hasRouteOverride = false;
  bool block = false;
  bool local = false;
};

enum class Kind { Block, Route };

struct State {
  bool enabled = false;  // there is something to rule on, or a rule to remove
  bool active = false;   // a rule of this kind is in force: a press removes it
  std::string overrideId;  // the rule a press removes, while active
  // the rule's value while active (block: true blocks; route: true bypasses),
  // which the label reads instead of the stale verdict
  bool polarity = false;
  std::vector<std::string> hosts;  // what a press would rule on
};

// The values a press rules on: the matched hosts, the hosts, the matched
// addresses, then the addresses, in the order the split-rule editor offers
// the same decision's values.
inline std::vector<std::string> RuleHosts(const Facts& facts) {
  std::vector<std::string> hosts = facts.matchedHosts;
  hosts.insert(hosts.end(), facts.hosts.begin(), facts.hosts.end());
  hosts.insert(hosts.end(), facts.matchedIps.begin(), facts.matchedIps.end());
  hosts.insert(hosts.end(), facts.ips.begin(), facts.ips.end());
  return hosts;
}

// A rule covers the connection when it names one of the connection's values
// exactly, or when it is the override that decided the connection, which
// also catches the SDK's suffix matching (a rule for the parent domain names
// the connection without spelling any of its hosts). The decision carries one
// override id and a block override wins that slot, so for the route kind the
// id answers only when no block override shared the decision.
inline State StateFor(const Facts& facts, const std::vector<HostRule>& rules, Kind kind) {
  State state;
  state.hosts = RuleHosts(facts);
  state.enabled = !state.hosts.empty();
  const bool idNamesKind = kind == Kind::Block
                               ? facts.hasBlockOverride
                               : facts.hasRouteOverride && !facts.hasBlockOverride;
  for (const HostRule& rule : rules) {
    if (kind == Kind::Block ? !rule.hasBlockOverride : !rule.hasRouteOverride) continue;
    const bool covers = std::any_of(rule.hosts.begin(), rule.hosts.end(), [&](const auto& host) {
      return std::find(state.hosts.begin(), state.hosts.end(), host) != state.hosts.end();
    });
    const bool decided = idNamesKind && !facts.overrideId.empty() &&
                         rule.overrideId == facts.overrideId;
    if (!covers && !decided) continue;
    state.active = true;
    state.enabled = true;
    state.overrideId = rule.overrideId;
    state.polarity = kind == Kind::Block ? rule.block : rule.routeLocal;
    break;
  }
  return state;
}

enum class Label { BlockHost, AllowHost, BypassTunnel, AlwaysTunnel };

// What the press leaves behind: the rule in force answers while active (its
// removal undoes it), else the verdict (the press inverts it).
inline Label LabelFor(const State& state, Kind kind, const Facts& facts) {
  if (kind == Kind::Block) {
    return (state.active ? state.polarity : facts.block) ? Label::AllowHost : Label::BlockHost;
  }
  return (state.active ? state.polarity : facts.local) ? Label::AlwaysTunnel
                                                       : Label::BypassTunnel;
}

enum class Effect {
  None,          // nothing to rule on
  RemoveRule,    // remove State::overrideId
  CreateBlock,   // BlockOverride{Block=true} over State::hosts
  CreateAllow,   // BlockOverride{Block=false}
  CreateBypass,  // RouteOverride{Local=true}
  CreateTunnel,  // RouteOverride{Local=false}, Pin off
};

inline Effect EffectFor(const State& state, Kind kind, const Facts& facts) {
  if (!state.enabled) return Effect::None;
  if (state.active) return Effect::RemoveRule;
  if (state.hosts.empty()) return Effect::None;
  if (kind == Kind::Block) return facts.block ? Effect::CreateAllow : Effect::CreateBlock;
  return facts.local ? Effect::CreateTunnel : Effect::CreateBypass;
}

// A host's group row rules on the host itself, with the group's precedence
// verdict and the deciding override of its newest decision.
inline Facts GroupFacts(const std::string& host, bool blocked, bool bypassed,
                        const Facts& newest) {
  Facts facts;
  if (!host.empty()) facts.hosts.push_back(host);
  facts.block = blocked;
  facts.local = !blocked && bypassed;
  facts.overrideId = newest.overrideId;
  facts.hasBlockOverride = newest.hasBlockOverride;
  facts.hasRouteOverride = newest.hasRouteOverride;
  return facts;
}

}  // namespace urnw::quick_action
