// The connect inspector's quick actions and the activity rows' menu run on
// GTK and the SDK, so this reads their sources: the host rule writers, the
// press that writes or removes a rule and its Undo, the inspector's buttons
// and the row menu that share it, and the toast slot that confirms it. The
// rules themselves are QuickActionTest.cpp's.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadQuickSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it.
std::string QuickBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool QuickMentions(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Every needle occurs, in this order.
bool QuickInSequence(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// The writers build the override the way the split-rule editor does, with a
// fresh id they hand back, and write nothing anywhere when there is nowhere
// to write.
UR_TEST(QuickActionWiring_TheHostWritesHostRules) {
  const std::string host = ReadQuickSource("SdkHost.cpp");
  UR_EXPECT_TRUE(QuickInSequence(
      QuickBody(host, "std::string SdkHost::AddHostBlockRule("),
      {"if (hosts.empty()) return {};", "override_.OverrideId = urnet::newId();",
       "blockOverride.Block = block;", "override_.BlockOverride = blockOverride;",
       "std::scoped_lock lock(mutex_);",
       "return AddBlockActionOverrideLocked(override_) ? *override_.OverrideId : std::string();"}));
  UR_EXPECT_TRUE(QuickInSequence(
      QuickBody(host, "std::string SdkHost::AddHostRouteRule("),
      {"if (hosts.empty()) return {};", "override_.OverrideId = urnet::newId();",
       "route.Local = local;", "route.Pin = false;", "override_.RouteOverride = route;",
       "return AddBlockActionOverrideLocked(override_) ? *override_.OverrideId : std::string();"}));
  const std::string locked =
      QuickBody(host, "bool SdkHost::AddBlockActionOverrideLocked(");
  UR_EXPECT_TRUE(QuickInSequence(locked, {"if (device_) {", "device_->addBlockActionOverride(override_);",
                                          "return true;", "if (localState_) {",
                                          "localState_->setBlockActionOverrides(overrides);",
                                          "return true;", "return false;"}));
  // the old entry point is the same write
  UR_EXPECT_TRUE(QuickMentions(QuickBody(host, "void SdkHost::AddBlockActionOverride("),
                               "AddBlockActionOverrideLocked(override_);"));
}

// Each effect is its one write; a create that wrote nothing confirms
// nothing; the overrides are re-read at once; and the Undo deletes exactly
// the rule the press created, by id.
UR_TEST(QuickActionWiring_APressWritesThenOffersUndo) {
  const std::string run = QuickBody(ReadQuickSource("ConnectPage.cpp"),
                                    "void ConnectPage::RunQuickAction(");
  UR_EXPECT_TRUE(QuickInSequence(run, {"case quick_action::Effect::RemoveRule:",
                                       "host_.RemoveBlockActionOverride(state.overrideId);",
                                       "case quick_action::Effect::CreateBlock:",
                                       "created = host_.AddHostBlockRule(state.hosts, true);",
                                       "case quick_action::Effect::CreateAllow:",
                                       "created = host_.AddHostBlockRule(state.hosts, false);",
                                       "case quick_action::Effect::CreateBypass:",
                                       "created = host_.AddHostRouteRule(state.hosts, true);",
                                       "case quick_action::Effect::CreateTunnel:",
                                       "created = host_.AddHostRouteRule(state.hosts, false);"}));
  UR_EXPECT_TRUE(QuickInSequence(
      run, {"if (effect != quick_action::Effect::RemoveRule && created.empty()) return;",
            "ApplyOverrides(host_.BlockActionOverrides(), /*force=*/false);",
            "if (created.empty()) {", "quickToast_.Show(*this, message);", "return;",
            "quickToast_.Show(*this, message, T_(\"adv_undo\", \"Undo\"), [this, alive, created] {",
            "if (!*alive) return;", "host_.RemoveBlockActionOverride(created);",
            "ApplyOverrides(host_.BlockActionOverrides(), /*force=*/false);"}));
}

// The inspector's buttons read the live host rules on every render, hide with
// the selection, and press with the state they showed; Copy details copies
// the inspector's own fields.
UR_TEST(QuickActionWiring_TheInspectorButtonsReadTheLiveRules) {
  const std::string page = ReadQuickSource("ConnectPage.cpp");
  const std::string inspector = QuickBody(page, "void ConnectPage::ApplyInspector()");
  UR_EXPECT_TRUE(QuickInSequence(inspector, {"if (!action) {", "inspectorActions_->set_visible(false);",
                                             "return;"}));
  UR_EXPECT_TRUE(QuickInSequence(
      inspector,
      {"blockQuick_ = quick_action::StateFor(facts, hostRules_, quick_action::Kind::Block);",
       "routeQuick_ = quick_action::StateFor(facts, hostRules_, quick_action::Kind::Route);",
       "inspectorActions_->set_visible(true);"}));
  // the Reason of a rule-decided connection opens the split rules sheet: a
  // link built once and relabelled in place, never rebuilt by a push
  const std::string build = QuickBody(page, "void ConnectPage::BuildInspectorGroup()");
  UR_EXPECT_TRUE(QuickInSequence(build, {"inspectorRowsAbove_ = Gtk::make_managed<Gtk::Box>(",
                                         "auto* link = Gtk::make_managed<Gtk::Button>();",
                                         "OpenSplitRulesSheet();", "inspectorReasonLink_ = link;",
                                         "inspectorRowsBelow_ = Gtk::make_managed<Gtk::Box>("}));
  UR_EXPECT_TRUE(!QuickMentions(inspector, "Gtk::make_managed<Gtk::Button>"));
  UR_EXPECT_TRUE(QuickInSequence(
      inspector, {"RemoveAllChildren(*inspectorRowsAbove_);",
                  "RemoveAllChildren(*inspectorRowsBelow_);",
                  "ApplyInspectorReason(ReasonText(*action), !overrideId.empty());",
                  "rows = inspectorRowsBelow_;"}));
  UR_EXPECT_TRUE(QuickInSequence(
      QuickBody(page, "void ConnectPage::ApplyInspectorReason("),
      {"inspectorReasonValue_->set_visible(!link);", "inspectorReasonLink_->set_visible(link);",
       "if (reason == inspectorReasonText_) return;",
       "T_(\"adv_open_split_rules\", \"open split rules\")"}));
  UR_EXPECT_TRUE(QuickInSequence(
      build, {"RunQuickAction(quick_action::Kind::Block, blockQuick_, QuickFacts(*action));",
              "RunQuickAction(quick_action::Kind::Route, routeQuick_, QuickFacts(*action));",
              "CopyConnectionDetails(*action);"}));
  const std::string button = QuickBody(page, "void ConnectPage::ApplyQuickActionButton(");
  UR_EXPECT_TRUE(QuickInSequence(button, {"button.set_sensitive(state.enabled);", "if (state.active) {",
                                          "button.add_css_class(\"ur-quick-on\");",
                                          "T_(\"adv_rule_active\", \"rule active, click to remove\")",
                                          "button.remove_css_class(\"ur-quick-on\");"}));
  const std::string copy = QuickBody(page, "void ConnectPage::CopyConnectionDetails(");
  UR_EXPECT_TRUE(QuickInSequence(copy, {"T_(\"adv_host\", \"Host\")", "T_(\"adv_addresses\", \"Addresses\")",
                                        "T_(\"adv_verdict\", \"Verdict\")", "T_(\"adv_reason\", \"Reason\")",
                                        "T_(\"adv_packets_total\", \"Packets (total)\")",
                                        "T_(\"adv_bytes_total\", \"Bytes (total)\")",
                                        "get_clipboard()->set_text(text);",
                                        "T_(\"adv_details_copied\", \"Connection details copied\")"}));
}

// A rule written anywhere (the sheet, the row menu, another device) reaches
// the buttons: both override paths go through ApplyOverrides, which keeps the
// full-fidelity host rules and re-renders a selection when they change.
UR_TEST(QuickActionWiring_EveryOverridePathFeedsTheHostRules) {
  const std::string page = ReadQuickSource("ConnectPage.cpp");
  UR_EXPECT_TRUE(QuickMentions(QuickBody(page, "void ConnectPage::RefreshFeeds("),
                               "ApplyOverrides(host_.BlockActionOverrides(), force);"));
  UR_EXPECT_TRUE(QuickInSequence(QuickBody(page, "void ConnectPage::OnHostEvent("),
                                 {"case DrawerEvent::Overrides:",
                                  "ApplyOverrides(host_.BlockActionOverrides(), /*force=*/true);",
                                  "break;"}));
  UR_EXPECT_TRUE(QuickInSequence(
      QuickBody(page, "void ConnectPage::ApplyOverrides("),
      {"if (!override_.OverrideId || !override_.Hosts || override_.Hosts->empty()) continue;",
       "rule.hasBlockOverride = override_.BlockOverride.has_value();",
       "rule.hasRouteOverride = override_.RouteOverride.has_value();",
       "if (force || hostSig != hostRulesSig_) {", "hostRules_ = std::move(hostRules);",
       "if (!selectedConnectionId_.empty()) ApplyInspector();"}));
}

// Advanced rows open their menu from a secondary click, Shift+F10 or the
// Menu key; the menu resolves its target at open, reads the live rules, and
// presses through the inspector's own RunQuickAction and Copy details.
UR_TEST(QuickActionWiring_TheRowMenuSharesTheInspectorsPress) {
  const std::string page = ReadQuickSource("ConnectPage.cpp");
  UR_EXPECT_TRUE(QuickInSequence(
      QuickBody(page, "ConnectPage::ConnectionRow ConnectPage::BuildConnectionRow("),
      {"if (advanced_) {", "press->set_button(GDK_BUTTON_SECONDARY);",
       "OpenConnectionRowMenu(key, *anchor, x, y);",
       "Gtk::ShortcutTrigger::parse_string(\"<Shift>F10|Menu\")",
       "OpenConnectionRowMenu(key, widget, -1, -1);", "} else {",
       "kit::MakePaneListRow(kListRowHeight);"}));
  const std::string menu = QuickBody(page, "void ConnectPage::OpenConnectionRowMenu(");
  UR_EXPECT_TRUE(QuickInSequence(
      menu, {"ResolveRowTarget(key);", "if (!target) return;",
             "quick_action::StateFor(facts, hostRules_, quick_action::Kind::Block);",
             "quick_action::StateFor(facts, hostRules_, quick_action::Kind::Route);",
             "rowMenu_->set_parent(*connectionsArea_);", "rowMenu_->popdown();",
             "RunQuickAction(quick_action::Kind::Block, blockState, facts);",
             "RunQuickAction(quick_action::Kind::Route, routeState, facts);",
             "CopyConnectionDetails(action);", "rowMenu_->popup();"}));
  UR_EXPECT_TRUE(QuickInSequence(QuickBody(page, "std::optional<ConnectPage::RowTarget> ConnectPage::ResolveRowTarget("),
                                 {"if (key.rfind(\"g:\", 0) != 0) {", "ConnectionRowKey(action) == key",
                                  "connection_filter::Passes(", "quick_action::GroupFacts("}));
  UR_EXPECT_TRUE(QuickMentions(QuickBody(page, "ConnectPage::~ConnectPage()"),
                               "if (rowMenu_) rowMenu_->unparent();"));
}

// One confirmation at a time, with the Undo button's callback owned by the
// toast and run when the button is pressed.
UR_TEST(QuickActionWiring_TheToastSlotShowsOneAtATime) {
  const std::string show = QuickBody(ReadQuickSource("Ui.cpp"), "void ToastSlot::Show(");
  UR_EXPECT_TRUE(QuickInSequence(show, {"ADW_IS_TOAST_OVERLAY(widget)", "Dismiss();",
                                        "adw_toast_new(message.c_str());",
                                        "adw_toast_set_button_label(toast, buttonLabel.c_str());",
                                        "\"button-clicked\"", "g_object_add_weak_pointer(",
                                        "adw_toast_overlay_add_toast(overlay, toast);"}));
}
