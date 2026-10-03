// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "SafetyRulePresentation.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

using namespace urnw::safety_rule;

namespace {

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + name);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

// Every security reason is a safety rule; the blocker, a user override and
// ordinary provider-routed traffic (no reason) are not.
UR_TEST(SafetyRule_SecurityReasons) {
  UR_EXPECT_TRUE(IsSecurity("security-encrypted"));
  UR_EXPECT_TRUE(IsSecurity("security-bittorrent"));
  UR_EXPECT_TRUE(IsSecurity("security-port"));
  UR_EXPECT_TRUE(IsSecurity("security-ip"));
  UR_EXPECT_TRUE(IsSecurity("security-smtp"));
  UR_EXPECT_TRUE(IsSecurity("security"));
  UR_EXPECT_FALSE(IsSecurity("blocker"));
  UR_EXPECT_FALSE(IsSecurity("override"));
  UR_EXPECT_FALSE(IsSecurity(""));
  UR_EXPECT_FALSE(IsSecurity("security-unknown-future"));
}

// Only the reasons that are always a plain drop can be routed locally
// (sdk BlockAction.RouteLocalOverridable): BitTorrent and non-public
// destinations never are.
UR_TEST(SafetyRule_RouteLocalOverridable) {
  UR_EXPECT_TRUE(RouteLocalOverridable("security-encrypted"));
  UR_EXPECT_TRUE(RouteLocalOverridable("security-port"));
  UR_EXPECT_FALSE(RouteLocalOverridable("security-bittorrent"));
  UR_EXPECT_FALSE(RouteLocalOverridable("security-ip"));
  UR_EXPECT_FALSE(RouteLocalOverridable("security-smtp"));
  UR_EXPECT_FALSE(RouteLocalOverridable("security"));
  UR_EXPECT_FALSE(RouteLocalOverridable("blocker"));
  UR_EXPECT_FALSE(RouteLocalOverridable("override"));
  UR_EXPECT_FALSE(RouteLocalOverridable(""));
}

// Any trace of an override (deciding id, block or route override) counts.
UR_TEST(SafetyRule_OverrideApplied) {
  UR_EXPECT_FALSE(OverrideApplied("", false, false));
  UR_EXPECT_TRUE(OverrideApplied("6f1c0a8e-0000-4000-8000-000000000001", false, false));
  UR_EXPECT_TRUE(OverrideApplied("", true, false));
  UR_EXPECT_TRUE(OverrideApplied("", false, true));
}

// The activity row: the chip for every safety-ruled row, the "Route locally"
// offer only for a blocked, overridable row no override decided.
UR_TEST(SafetyRule_ActivityRow) {
  auto row = PresentActivityRow("security-encrypted", true, false);
  UR_EXPECT_TRUE(row.safetyRule);
  UR_EXPECT_TRUE(row.offerRouteLocal);

  row = PresentActivityRow("security-port", true, false);
  UR_EXPECT_TRUE(row.safetyRule);
  UR_EXPECT_TRUE(row.offerRouteLocal);

  // an override already applies: the row edits that rule, no second offer
  row = PresentActivityRow("security-encrypted", true, true);
  UR_EXPECT_TRUE(row.safetyRule);
  UR_EXPECT_FALSE(row.offerRouteLocal);

  // kill switch off: the safety rule already routed it locally
  row = PresentActivityRow("security-encrypted", false, false);
  UR_EXPECT_TRUE(row.safetyRule);
  UR_EXPECT_FALSE(row.offerRouteLocal);

  // never overridable
  row = PresentActivityRow("security-bittorrent", true, false);
  UR_EXPECT_TRUE(row.safetyRule);
  UR_EXPECT_FALSE(row.offerRouteLocal);
  row = PresentActivityRow("security-ip", true, false);
  UR_EXPECT_TRUE(row.safetyRule);
  UR_EXPECT_FALSE(row.offerRouteLocal);

  // not a safety rule at all
  row = PresentActivityRow("blocker", true, false);
  UR_EXPECT_FALSE(row.safetyRule);
  UR_EXPECT_FALSE(row.offerRouteLocal);
  row = PresentActivityRow("", false, false);
  UR_EXPECT_FALSE(row.safetyRule);
  UR_EXPECT_FALSE(row.offerRouteLocal);
}

// A predicate with no call site is this project's most-repeated defect: the
// split rules sheet must carry the reason and render through the predicate,
// and the "Route locally" offer must reuse the existing editor path.
UR_TEST(SafetyRule_SplitRulesSheetWiring) {
  const std::string source = ReadSource("SplitRulesSheet.cpp");
  UR_EXPECT_FALSE(source.empty());
  UR_EXPECT_TRUE(source.find("item.reason = it->Reason") != std::string::npos);
  UR_EXPECT_TRUE(source.find("safety_rule::PresentActivityRow(") != std::string::npos);
  UR_EXPECT_TRUE(source.find("\"safety_rule_detail\"") != std::string::npos);
  const size_t offer = source.find("\"add_local_split_rule\"");
  UR_EXPECT_TRUE(offer != std::string::npos);
  if (offer != std::string::npos) {
    UR_EXPECT_TRUE(source.find("OpenEditorForAction(", offer) != std::string::npos);
  }
}

// The kill switch disclosure names the safety-rule bypass where the other
// kill switch exception is shown.
UR_TEST(SafetyRule_KillSwitchDisclosureWiring) {
  const std::string settings = ReadSource("SettingsPage.cpp");
  UR_EXPECT_TRUE(settings.find("\"kill_switch_exception_unrecognized_encrypted\"") !=
                 std::string::npos);
  const std::string drawer = ReadSource("ConnectDrawer.cpp");
  UR_EXPECT_TRUE(drawer.find("\"kill_switch_exception_unrecognized_encrypted\"") !=
                 std::string::npos);
}
