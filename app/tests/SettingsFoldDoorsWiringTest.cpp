// Pane A's second doors on the Settings destination are built and shown
// (SettingsPage.cpp, read as text: the page needs GTK). The version rows are
// built for the About pane and for pane A's About copy, Licenses and Stay in
// touch with them; the Advanced mode toggle for the Device pane and for pane
// A's Advanced copy; ApplyBreakpoint shows each copy from SettingsFold.hpp's
// reading; and both toggles are written by the one apply path under the one
// echo guard, and write only through SdkHost.
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

std::string ReadFoldSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The text from `start` up to the next `end` after it, or "".
std::string FoldBetween(const std::string& text, const std::string& start,
                        const std::string& end) {
  const size_t from = text.find(start);
  if (from == std::string::npos) return std::string();
  const size_t to = text.find(end, from + start.size());
  return text.substr(from, to == std::string::npos ? std::string::npos : to - from);
}

// Each needle occurs after the one before it.
bool FoldInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

bool FoldHas(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

UR_TEST(SettingsFoldDoors_PaneACarriesTheAboutAndAdvancedCopies) {
  const std::string page = ReadFoldSource("SettingsPage.cpp");
  const std::string ctor =
      FoldBetween(page, "SettingsPage::SettingsPage(SdkHost& host)", "\n}\n");
  UR_EXPECT_TRUE_MSG("could not read the SettingsPage constructor", !ctor.empty());
  // after the Connections group, the About copy (its header, the version
  // rows, Licenses, Stay in touch with the DePIN Hub and protocol links),
  // then the Advanced copy, both hidden until the first fold
  UR_EXPECT_TRUE(FoldInOrder(
      ctor, {"BuildConnectionsSection(*paneA_.content);",
             "aboutFoldHost_->append(*kit::MakePaneGroupHeader(T_(\"about\", \"About\")).root);",
             "BuildVersionSection(*aboutFoldHost_);", "AddLicensesRow(*aboutFoldHost_);",
             "BuildStayInTouchSection(*aboutFoldHost_);", "aboutFoldHost_->set_visible(false);", "paneA_.content->append(*aboutFoldHost_);",
             "BuildAdvancedFoldSection(*deviceFoldHost_);", "deviceFoldHost_->set_visible(false);",
             "paneA_.content->append(*deviceFoldHost_);", "paneSizes_->add_widget(*paneA_.root);"}));
  // and the primaries stay where they were
  UR_EXPECT_TRUE(FoldInOrder(ctor, {"BuildAdvancedSection(*paneB_.content);",
                                    "BuildVersionSection(*paneC_.content);",
                                    "AddLicensesRow(*paneC_.content);",
                                    "BuildStayInTouchSection(*paneC_.content);"}));
  const std::string fold =
      FoldBetween(page, "void SettingsPage::BuildAdvancedFoldSection(Gtk::Box& host) {", "\n}\n");
  UR_EXPECT_TRUE(FoldInOrder(
      fold, {"kit::MakePaneGroupHeader(T_(\"advanced\", \"Advanced\"))",
             "advancedModeFold_ = AddAdvancedModeToggle(host);"}));
  UR_EXPECT_TRUE(FoldHas(FoldBetween(page, "void SettingsPage::BuildAdvancedSection(Gtk::Box& host) {",
                                     "\n}\n"),
                         "advancedMode_ = AddAdvancedModeToggle(host);"));
}

UR_TEST(SettingsFoldDoors_TheBreakpointShowsEachCopyWhileItsPaneIsFolded) {
  const std::string page = ReadFoldSource("SettingsPage.cpp");
  const std::string apply =
      FoldBetween(page, "void SettingsPage::ApplyBreakpoint(int widthDip) {", "\n}\n");
  UR_EXPECT_TRUE(FoldInOrder(
      apply, {"const int panes = settings_fold::PaneCount(widthDip);",
              "paneC_.root->set_visible(panes >= 3);", "paneB_.root->set_visible(panes >= 2);",
              "aboutFoldHost_->set_visible(settings_fold::AboutDoorsShown(panes));",
              "deviceFoldHost_->set_visible(settings_fold::DeviceDoorsShown(panes));"}));
  // the table lives in the header, not as a second literal here
  UR_EXPECT_TRUE(!FoldHas(apply, "1400") && !FoldHas(apply, "900"));
}

UR_TEST(SettingsFoldDoors_BothAdvancedTogglesHaveOneWriterAndOneApplyPath) {
  const std::string page = ReadFoldSource("SettingsPage.cpp");
  // each toggle writes only through SdkHost, from its own state
  const std::string toggle = FoldBetween(
      page, "Gtk::Switch* SettingsPage::AddAdvancedModeToggle(Gtk::Box& host) {", "\n}\n");
  UR_EXPECT_TRUE(FoldInOrder(toggle, {"toggle->set_active(host_.CurrentAdvancedMode());",
                                      "[this, toggle] { OnAdvancedModeToggled(*toggle); }"}));
  const std::string toggled = FoldBetween(
      page, "void SettingsPage::OnAdvancedModeToggled(Gtk::Switch& source) {", "\n}\n");
  UR_EXPECT_TRUE(FoldInOrder(toggled, {"if (applyingAdvancedMode_) return;",
                                       "host_.SetAdvancedMode(source.get_active());"}));
  // the apply path writes both under the one guard, with no early return
  // that compares only one of them
  const std::string apply =
      FoldBetween(page, "void SettingsPage::SetAdvancedMode(bool on) {", "\n}\n");
  UR_EXPECT_TRUE(FoldInOrder(
      apply, {"applyingAdvancedMode_ = true;", "advancedMode_->set_active(on);",
              "advancedModeFold_->set_active(on);", "applyingAdvancedMode_ = false;"}));
  UR_EXPECT_TRUE(!FoldHas(apply, "advancedMode_->get_active() == on) return;"));
  // and the build-time replay goes through it
  UR_EXPECT_TRUE(FoldHas(
      FoldBetween(page, "void SettingsPage::ApplyLocalDeviceState() {", "\n}\n"),
      "SetAdvancedMode(host_.CurrentAdvancedMode());"));
  // no other write reaches either switch
  size_t writes = 0;
  for (const char* sw : {"advancedMode_->set_active(", "advancedModeFold_->set_active("}) {
    for (size_t at = page.find(sw); at != std::string::npos; at = page.find(sw, at + 1)) ++writes;
  }
  UR_EXPECT_EQ(size_t{2}, writes);
}
