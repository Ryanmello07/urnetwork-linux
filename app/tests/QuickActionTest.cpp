// The connect inspector's quick actions (QuickAction.hpp): what each button
// reads from the live overrides, its label, and what a press does.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "QuickAction.hpp"

using urnw::quick_action::Effect;
using urnw::quick_action::EffectFor;
using urnw::quick_action::Facts;
using urnw::quick_action::GroupFacts;
using urnw::quick_action::HostRule;
using urnw::quick_action::Kind;
using urnw::quick_action::Label;
using urnw::quick_action::LabelFor;
using urnw::quick_action::RuleHosts;
using urnw::quick_action::State;
using urnw::quick_action::StateFor;

namespace {

// A tunnelled decision for cdn.example.net, matched by nothing.
Facts Tunnelled() {
  Facts facts;
  facts.hosts = {"cdn.example.net"};
  facts.ips = {"192.0.2.10"};
  return facts;
}

HostRule BlockRule(const std::string& id, std::vector<std::string> hosts, bool block) {
  HostRule rule;
  rule.overrideId = id;
  rule.hosts = std::move(hosts);
  rule.hasBlockOverride = true;
  rule.block = block;
  return rule;
}

HostRule RouteRule(const std::string& id, std::vector<std::string> hosts, bool local) {
  HostRule rule;
  rule.overrideId = id;
  rule.hosts = std::move(hosts);
  rule.hasRouteOverride = true;
  rule.routeLocal = local;
  return rule;
}

}  // namespace

// A press rules on every value of the decision, in the split-rule editor's
// order: matched hosts, hosts, matched addresses, addresses.
UR_TEST(QuickAction_ThePressRulesOnEveryValueInTheEditorsOrder) {
  Facts facts;
  facts.hosts = {"cdn.example.net"};
  facts.ips = {"192.0.2.10"};
  facts.matchedHosts = {"example.net"};
  facts.matchedIps = {"2001:db8::7"};
  const std::vector<std::string> want = {"example.net", "cdn.example.net", "2001:db8::7",
                                         "192.0.2.10"};
  UR_EXPECT_TRUE(RuleHosts(facts) == want);
  const State state = StateFor(facts, {}, Kind::Block);
  UR_EXPECT_TRUE(state.enabled);
  UR_EXPECT_FALSE(state.active);
  UR_EXPECT_TRUE(state.hosts == want);
}

// With no rule in force each button creates the inverse of the verdict.
UR_TEST(QuickAction_WithNoRuleAPressInvertsTheVerdict) {
  const Facts tunnelled = Tunnelled();
  const State block = StateFor(tunnelled, {}, Kind::Block);
  const State route = StateFor(tunnelled, {}, Kind::Route);
  UR_EXPECT_TRUE(EffectFor(block, Kind::Block, tunnelled) == Effect::CreateBlock);
  UR_EXPECT_TRUE(LabelFor(block, Kind::Block, tunnelled) == Label::BlockHost);
  UR_EXPECT_TRUE(EffectFor(route, Kind::Route, tunnelled) == Effect::CreateBypass);
  UR_EXPECT_TRUE(LabelFor(route, Kind::Route, tunnelled) == Label::BypassTunnel);

  Facts blocked = Tunnelled();
  blocked.block = true;
  const State allow = StateFor(blocked, {}, Kind::Block);
  UR_EXPECT_TRUE(EffectFor(allow, Kind::Block, blocked) == Effect::CreateAllow);
  UR_EXPECT_TRUE(LabelFor(allow, Kind::Block, blocked) == Label::AllowHost);

  Facts bypassed = Tunnelled();
  bypassed.local = true;
  const State tunnel = StateFor(bypassed, {}, Kind::Route);
  UR_EXPECT_TRUE(EffectFor(tunnel, Kind::Route, bypassed) == Effect::CreateTunnel);
  UR_EXPECT_TRUE(LabelFor(tunnel, Kind::Route, bypassed) == Label::AlwaysTunnel);
}

// A rule naming one of the decision's values is in force: the button is on,
// carries the rule's id and value, and a press removes it.
UR_TEST(QuickAction_ACoveringRuleIsActive) {
  const Facts facts = Tunnelled();
  const State state =
      StateFor(facts, {BlockRule("rule-1", {"other.example"}, true),
                       BlockRule("rule-2", {"192.0.2.10"}, true)},
               Kind::Block);
  UR_EXPECT_TRUE(state.active);
  UR_EXPECT_TRUE(state.enabled);
  UR_EXPECT_TRUE(state.overrideId == "rule-2");
  UR_EXPECT_TRUE(state.polarity);
  UR_EXPECT_TRUE(EffectFor(state, Kind::Block, facts) == Effect::RemoveRule);
  // a rule of the other kind does not light this button
  UR_EXPECT_FALSE(StateFor(facts, {RouteRule("rule-3", {"cdn.example.net"}, true)}, Kind::Block)
                      .active);
  UR_EXPECT_TRUE(StateFor(facts, {RouteRule("rule-3", {"cdn.example.net"}, true)}, Kind::Route)
                     .active);
}

// The label follows the rule in force, not the stale verdict: a blocked
// decision under a fresh allowing rule reads "Block this host", and a
// tunnelled one under a fresh blocking rule "Allow this host".
UR_TEST(QuickAction_TheLabelFollowsTheRuleOverTheVerdict) {
  Facts blocked = Tunnelled();
  blocked.block = true;
  const State allowing = StateFor(blocked, {BlockRule("r", {"cdn.example.net"}, false)}, Kind::Block);
  UR_EXPECT_TRUE(allowing.active);
  UR_EXPECT_TRUE(LabelFor(allowing, Kind::Block, blocked) == Label::BlockHost);
  const Facts tunnelled = Tunnelled();
  const State blocking =
      StateFor(tunnelled, {BlockRule("r", {"cdn.example.net"}, true)}, Kind::Block);
  UR_EXPECT_TRUE(LabelFor(blocking, Kind::Block, tunnelled) == Label::AllowHost);
  const State bypassing =
      StateFor(tunnelled, {RouteRule("r", {"cdn.example.net"}, true)}, Kind::Route);
  UR_EXPECT_TRUE(LabelFor(bypassing, Kind::Route, tunnelled) == Label::AlwaysTunnel);
}

// The deciding override's id covers a decision no rule names exactly (the
// SDK's suffix matching); for the route kind only when no block override
// took the decision's one id slot.
UR_TEST(QuickAction_TheDecidingIdCoversASuffixMatch) {
  Facts facts = Tunnelled();
  facts.overrideId = "parent-rule";
  facts.hasBlockOverride = true;
  facts.block = true;
  const std::vector<HostRule> rules = {BlockRule("parent-rule", {"example.net"}, true)};
  const State block = StateFor(facts, rules, Kind::Block);
  UR_EXPECT_TRUE(block.active);
  UR_EXPECT_TRUE(block.overrideId == "parent-rule");

  Facts routed = Tunnelled();
  routed.overrideId = "route-rule";
  routed.hasRouteOverride = true;
  routed.local = true;
  const std::vector<HostRule> routes = {RouteRule("route-rule", {"example.net"}, true)};
  UR_EXPECT_TRUE(StateFor(routed, routes, Kind::Route).active);
  // the same id is not the route rule's once a block override shared it
  routed.hasBlockOverride = true;
  UR_EXPECT_FALSE(StateFor(routed, routes, Kind::Route).active);
  // and an id names only rules of the button's own kind
  Facts onlyRoute = Tunnelled();
  onlyRoute.overrideId = "route-rule";
  onlyRoute.hasRouteOverride = true;
  UR_EXPECT_FALSE(StateFor(onlyRoute, {BlockRule("route-rule", {"example.net"}, true)}, Kind::Block)
                      .active);
}

// A decision with nothing to rule on offers nothing.
UR_TEST(QuickAction_ADecisionWithNoValuesIsDisabled) {
  const Facts empty;
  const State state = StateFor(empty, {}, Kind::Block);
  UR_EXPECT_FALSE(state.enabled);
  UR_EXPECT_TRUE(EffectFor(state, Kind::Block, empty) == Effect::None);
  UR_EXPECT_TRUE(EffectFor(StateFor(empty, {}, Kind::Route), Kind::Route, empty) == Effect::None);
}

// A host's group row rules on the host, with the group's verdict and its
// newest decision's deciding override.
UR_TEST(QuickAction_AGroupRulesOnItsHost) {
  Facts newest = Tunnelled();
  newest.overrideId = "rule-9";
  newest.hasRouteOverride = true;
  const Facts group = GroupFacts("cdn.example.net", false, true, newest);
  UR_EXPECT_TRUE(group.hosts == std::vector<std::string>{"cdn.example.net"});
  UR_EXPECT_TRUE(group.ips.empty());
  UR_EXPECT_FALSE(group.block);
  UR_EXPECT_TRUE(group.local);
  UR_EXPECT_TRUE(group.overrideId == "rule-9");
  UR_EXPECT_TRUE(group.hasRouteOverride);
  const State route = StateFor(group, {}, Kind::Route);
  UR_EXPECT_TRUE(EffectFor(route, Kind::Route, group) == Effect::CreateTunnel);
  // blocked wins the group's verdict
  UR_EXPECT_FALSE(GroupFacts("cdn.example.net", true, true, newest).local);
  UR_EXPECT_FALSE(StateFor(GroupFacts("", false, false, newest), {}, Kind::Block).enabled);
}
