// The call sites of "Launch URnetwork on system startup" (LaunchAtStartup.hpp):
// Settings has the toggle after the update check, as Windows does, with no
// session gate, showing the entry as the filesystem has it on every Load and
// after every switch; the launch brings an old entry up to date and creates
// none; and the template the entry links to is the file the packages install.
// The logic is pure and runs in LaunchAtStartupTest.cpp; these sources need
// gtkmm, so this reads them with the comments blanked.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "LaunchAtStartup.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadStartupFile(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadStartupSource(const std::string& relative) {
  std::string text = ReadStartupFile(relative);
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

std::string StartupBetween(const std::string& text, const std::string& start,
                           const std::string& end) {
  const size_t from = text.find(start);
  if (from == std::string::npos) return std::string();
  const size_t to = text.find(end, from + start.size());
  return text.substr(from, to == std::string::npos ? std::string::npos : to - from);
}

bool StartupHas(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Each needle occurs after the one before it.
bool StartupInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

}  // namespace

// The row: after the update check and before the update notice, labelled with
// the macOS key and no note, hidden without the template, written under an
// echo guard, and switched by the user only.
UR_TEST(LaunchAtStartupWiring_SettingsHasTheToggleAfterTheUpdateCheck) {
  const std::string page = ReadStartupSource("SettingsPage.cpp");
  const std::string general =
      StartupBetween(page, "void SettingsPage::BuildGeneralSection(Gtk::Box& host) {", "\n}\n");
  UR_EXPECT_TRUE(StartupInOrder(
      general, {"autoCheckUpdates_ = AddToggleRow(", "launchAtStartup_ = AddToggleRow(",
                "T_(\"launch_urnetwork_on_system_startup\", \"Launch URnetwork on system startup\"), {},",
                "&launchAtStartupRow_);", "ApplyLaunchAtStartup();",
                "launchAtStartup_->property_active().signal_changed().connect(",
                "OnLaunchAtStartupToggled();", "updateRow_ = row.root;"}));
  const std::string apply =
      StartupBetween(page, "void SettingsPage::ApplyLaunchAtStartup() {", "\n}\n");
  UR_EXPECT_TRUE(StartupInOrder(
      apply, {"const startup::Files files = startup::PosixFiles();",
              "const startup::Locations locations = StartupLocations();",
              "launchAtStartupRow_->set_visible(startup::Available(files, locations));",
              "applyingLaunchAtStartup_ = true;",
              "launchAtStartup_->set_active(startup::Enabled(files, locations));",
              "applyingLaunchAtStartup_ = false;"}));
  const std::string toggled =
      StartupBetween(page, "void SettingsPage::OnLaunchAtStartupToggled() {", "\n}\n");
  UR_EXPECT_TRUE(StartupInOrder(
      toggled, {"if (applyingLaunchAtStartup_) return;",
                "startup::Set(startup::PosixFiles(), StartupLocations(), enabled)",
                "ApplyLaunchAtStartup();"}));
  const std::string locations = StartupBetween(page, "startup::Locations StartupLocations() {", "\n}\n");
  UR_EXPECT_TRUE(StartupHas(locations, "locations.configDir = g_get_user_config_dir();"));
  UR_EXPECT_TRUE(!StartupHas(locations, "templatePath"));
  UR_EXPECT_TRUE(StartupHas(ReadStartupSource("SettingsPage.hpp"), "bool applyingLaunchAtStartup_ = false;"));
}

// Every Load reads the entry again, before the session gate: the setting is
// the machine's, signed in or not, and the desktop can switch it off too.
UR_TEST(LaunchAtStartupWiring_EveryLoadReadsTheEntryWithoutASession) {
  const std::string load =
      StartupBetween(ReadStartupSource("SettingsPage.cpp"), "void SettingsPage::Load() {", "\n}\n");
  UR_EXPECT_TRUE(StartupInOrder(
      load, {"ApplyLocalDeviceState();", "ApplyLaunchAtStartup();", "if (!host_.IsLoggedIn()) {"}));
}

// The instance brings an entry from before --autostart up to date at each
// launch, before its window exists, and creates none: only the user's switch
// writes one.
UR_TEST(LaunchAtStartupWiring_TheLaunchRefreshesAndNeverCreates) {
  const std::string main = ReadStartupSource("main.cpp");
  const std::string startup = StartupBetween(main, "app->signal_startup().connect([&] {", "\n  });\n");
  UR_EXPECT_TRUE(StartupInOrder(
      startup, {"urnw::RegisterBrandIcons();", "startupLocations.configDir = g_get_user_config_dir();",
                "urnw::startup::Refresh(urnw::startup::PosixFiles(), startupLocations);",
                "window = std::make_shared<urnw::MainWindow>(*host);"}));
  UR_EXPECT_TRUE(!StartupHas(main, "startup::Set("));
  UR_EXPECT_TRUE(!StartupHas(main, "templatePath"));
}

// The entry links to the template the daemon packages install, whose Exec
// starts the app with --autostart.
UR_TEST(LaunchAtStartupWiring_TheTemplateIsTheInstalledOne) {
  UR_EXPECT_TRUE(std::string(urnw::startup::kTemplatePath) ==
                 std::string("/etc/urnetwork/autostart/") + urnw::startup::kEntryName);
  UR_EXPECT_TRUE(StartupHas(ReadStartupFile("../meson.build"),
                            R"(install_data('packaging/autostart/com.bringyour.network.desktop',
             install_dir : '/etc/urnetwork/autostart'))"));
  UR_EXPECT_TRUE(StartupHas(
      ReadStartupFile("../../packaging/lib/common.sh"),
      R"(cp "${src}/autostart/com.bringyour.network.desktop" "${root}/etc/urnetwork/autostart/")"));
  UR_EXPECT_TRUE(StartupHas(ReadStartupFile("../packaging/autostart/com.bringyour.network.desktop"),
                            "\nExec=urnetwork --autostart\n"));
}
