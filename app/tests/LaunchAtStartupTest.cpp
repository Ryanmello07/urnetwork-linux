// "Launch URnetwork on system startup" (LaunchAtStartup.hpp): the toggle shows
// whether a login will start the app, turning it on links the user's autostart
// entry to the package's template (whose Exec has --autostart) atomically,
// turning it off removes the entry, nothing creates it by itself, and a launch
// brings an entry from before --autostart up to date. A fake filesystem logs
// every operation, so order is checked, not timing; the last case runs the
// same against a temporary directory.
//
// SPDX-License-Identifier: MPL-2.0
#include "LaunchAtStartup.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "TestHarness.hpp"

namespace {

using urnw::startup::Files;
using urnw::startup::Locations;
using urnw::startup::Node;

const std::string kTemplate = "/template.example/com.bringyour.network.desktop";
const std::string kAutostart = "/config.example/autostart";
const std::string kEntry = kAutostart + "/com.bringyour.network.desktop";
const std::string kTemporary = kEntry + ".new";

constexpr const char* kTemplateContent = R"(# the package's template
[Desktop Entry]
Type=Application
Name=URnetwork
Exec=urnetwork --autostart
TryExec=urnetwork
)";

Locations TestLocations() {
  Locations locations;
  locations.configDir = "/config.example";
  locations.templatePath = kTemplate;
  return locations;
}

std::string EntryWith(const std::string& lines) {
  return "[Desktop Entry]\nType=Application\nName=URnetwork\n" + lines;
}

// A filesystem of files and links that logs every change it is asked for.
struct FakeFiles {
  std::map<std::string, std::string> files;
  std::map<std::string, std::string> links;
  std::set<std::string> dirs;
  std::vector<std::string> ops;
  bool failRename = false;

  Files Bind() {
    Files bound;
    bound.node = [this](const std::string& path) {
      if (links.count(path)) return Node::Link;
      if (files.count(path)) return Node::File;
      if (dirs.count(path)) return Node::Other;
      return Node::Absent;
    };
    bound.read = [this](const std::string& path) -> std::optional<std::string> {
      std::string at = path;
      for (int hops = 0; hops < 8 && links.count(at); ++hops) at = links[at];
      if (!files.count(at)) return std::nullopt;
      return files[at];
    };
    bound.makeDirectories = [this](const std::string& dir) {
      ops.push_back("mkdir " + dir);
      dirs.insert(dir);
      return true;
    };
    bound.link = [this](const std::string& target, const std::string& path) {
      ops.push_back("link " + target + " " + path);
      if (links.count(path) || files.count(path)) return false;
      links[path] = target;
      return true;
    };
    bound.rename = [this](const std::string& from, const std::string& to) {
      ops.push_back("rename " + from + " " + to);
      if (failRename) return false;
      if (links.count(from)) {
        files.erase(to);
        links[to] = links[from];
        links.erase(from);
        return true;
      }
      if (files.count(from)) {
        links.erase(to);
        files[to] = files[from];
        files.erase(from);
        return true;
      }
      return false;
    };
    bound.remove = [this](const std::string& path) {
      ops.push_back("remove " + path);
      return links.erase(path) + files.erase(path) > 0;
    };
    return bound;
  }
};

std::string Joined(const std::vector<std::string>& ops) {
  std::string out;
  for (const std::string& op : ops) out += "[" + op + "] ";
  return out;
}

#define EXPECT_OPS(expected, fake) \
  UR_EXPECT_TRUE_MSG("ops were " + Joined((fake).ops), (expected) == (fake).ops)

const std::vector<std::string> kLinkOps = {"mkdir " + kAutostart,
                                           "link " + kTemplate + " " + kTemporary,
                                           "rename " + kTemporary + " " + kEntry};

}  // namespace

// Off until the user turns it on: a launch with no entry creates none.
UR_TEST(LaunchAtStartup_NothingCreatesTheEntryByItself) {
  UR_EXPECT_FALSE(urnw::startup::kDefaultEnabled);
  FakeFiles fake;
  fake.files[kTemplate] = kTemplateContent;
  urnw::startup::Refresh(fake.Bind(), TestLocations());
  EXPECT_OPS(std::vector<std::string>{}, fake);
  UR_EXPECT_FALSE(urnw::startup::Enabled(fake.Bind(), TestLocations()));
  UR_EXPECT_TRUE(urnw::startup::Available(fake.Bind(), TestLocations()));
}

// Turning it on links the entry to the template under a temporary name and
// renames it over the entry, never removing the entry first; an entry the
// desktop switched off is replaced the same way, and a leftover temporary
// link is cleared first. The entry then runs the template's Exec, with
// --autostart.
UR_TEST(LaunchAtStartup_TurningItOnLinksTheTemplateAtomically) {
  {
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    UR_EXPECT_TRUE(urnw::startup::Set(fake.Bind(), TestLocations(), true));
    EXPECT_OPS(kLinkOps, fake);
    UR_EXPECT_TRUE(fake.links.count(kEntry) && fake.links[kEntry] == kTemplate);
    UR_EXPECT_TRUE(urnw::startup::Enabled(fake.Bind(), TestLocations()));
    const auto content = fake.Bind().read(kEntry);
    UR_EXPECT_TRUE(content && urnw::startup::ParseDesktopEntry(*content).exec ==
                                  "urnetwork --autostart");
  }
  {
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    fake.files[kEntry] = EntryWith("Exec=urnetwork --autostart\nHidden=true\n");
    UR_EXPECT_FALSE(urnw::startup::Enabled(fake.Bind(), TestLocations()));
    UR_EXPECT_TRUE(urnw::startup::Set(fake.Bind(), TestLocations(), true));
    EXPECT_OPS(kLinkOps, fake);
    UR_EXPECT_TRUE(urnw::startup::Enabled(fake.Bind(), TestLocations()));
  }
  {
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    fake.links[kTemporary] = "/elsewhere.example";
    UR_EXPECT_TRUE(urnw::startup::Set(fake.Bind(), TestLocations(), true));
    EXPECT_OPS((std::vector<std::string>{"mkdir " + kAutostart, "remove " + kTemporary,
                                         "link " + kTemplate + " " + kTemporary,
                                         "rename " + kTemporary + " " + kEntry}),
               fake);
  }
  {
    // a rename that fails leaves the old entry and no temporary link, and
    // says so
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    fake.files[kEntry] = EntryWith("Exec=urnetwork\nHidden=true\n");
    fake.failRename = true;
    UR_EXPECT_FALSE(urnw::startup::Set(fake.Bind(), TestLocations(), true));
    UR_EXPECT_TRUE(fake.files.count(kEntry) == 1 && fake.links.count(kTemporary) == 0);
  }
}

// Turning it off removes the entry, whatever made it; with none there is
// nothing to do.
UR_TEST(LaunchAtStartup_TurningItOffRemovesTheEntry) {
  for (const bool linked : {true, false}) {
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    if (linked) {
      fake.links[kEntry] = kTemplate;
    } else {
      fake.files[kEntry] = EntryWith("Exec=urnetwork %u\n");
    }
    UR_EXPECT_TRUE(urnw::startup::Enabled(fake.Bind(), TestLocations()));
    UR_EXPECT_TRUE(urnw::startup::Set(fake.Bind(), TestLocations(), false));
    EXPECT_OPS(std::vector<std::string>{"remove " + kEntry}, fake);
    UR_EXPECT_FALSE(urnw::startup::Enabled(fake.Bind(), TestLocations()));
  }
  FakeFiles none;
  none.files[kTemplate] = kTemplateContent;
  UR_EXPECT_TRUE(urnw::startup::Set(none.Bind(), TestLocations(), false));
  EXPECT_OPS(std::vector<std::string>{}, none);
}

// The desktop's startup settings switch an entry off without removing it
// (Hidden=true, or GNOME's X-GNOME-Autostart-enabled=false): it reads as off,
// and a launch leaves it as the user set it.
UR_TEST(LaunchAtStartup_AnEntrySwitchedOffByTheDesktopReadsAsOff) {
  for (const char* off : {"Hidden=true\n", "X-GNOME-Autostart-enabled=false\n",
                          "X-GNOME-Autostart-enabled = false\n"}) {
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    fake.files[kEntry] = EntryWith(std::string("Exec=urnetwork\n") + off);
    UR_EXPECT_TRUE_MSG(off, !urnw::startup::Enabled(fake.Bind(), TestLocations()));
    urnw::startup::Refresh(fake.Bind(), TestLocations());
    UR_EXPECT_TRUE_MSG(off, fake.ops.empty());
  }
}

// An entry that runs the launcher without --autostart, the way the template
// did before it or a copy of the menu entry does, is replaced by the link at
// launch, so its logins show only the tray.
UR_TEST(LaunchAtStartup_AnEntryFromBeforeAutostartIsBroughtUpToDateAtLaunch) {
  for (const char* exec : {"Exec=urnetwork\n", "Exec=urnetwork %u\n", "Exec=/usr/bin/urnetwork\n",
                           "Exec = urnetwork %U\n"}) {
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    fake.files[kEntry] = EntryWith(exec);
    urnw::startup::Refresh(fake.Bind(), TestLocations());
    UR_EXPECT_TRUE_MSG(exec + Joined(fake.ops), fake.ops == kLinkOps);
    UR_EXPECT_TRUE_MSG(exec, fake.links.count(kEntry) && fake.links[kEntry] == kTemplate);
    UR_EXPECT_TRUE_MSG(exec, urnw::startup::Enabled(fake.Bind(), TestLocations()));
  }
}

// Every other entry is left as it is: one the user wrote to run something
// else (an AppImage path, an env prefix for a renderer), one already up to
// date, a link (the template, which the package keeps current) and a
// directory.
UR_TEST(LaunchAtStartup_AnyOtherEntryIsLeftAsItIs) {
  for (const char* exec : {"Exec=env GSK_RENDERER=cairo urnetwork\n",
                           "Exec=/home/user.example/Applications/URnetwork.AppImage\n",
                           "Exec=urnetwork --autostart\n", "Exec=urnetwork-gui\n", ""}) {
    FakeFiles fake;
    fake.files[kTemplate] = kTemplateContent;
    fake.files[kEntry] = EntryWith(exec);
    urnw::startup::Refresh(fake.Bind(), TestLocations());
    UR_EXPECT_TRUE_MSG(exec, fake.ops.empty());
  }
  FakeFiles linked;
  linked.files[kTemplate] = kTemplateContent;
  linked.links[kEntry] = kTemplate;
  urnw::startup::Refresh(linked.Bind(), TestLocations());
  UR_EXPECT_TRUE(linked.ops.empty());
  FakeFiles directory;
  directory.files[kTemplate] = kTemplateContent;
  directory.dirs.insert(kEntry);
  urnw::startup::Refresh(directory.Bind(), TestLocations());
  UR_EXPECT_TRUE(directory.ops.empty());
  UR_EXPECT_FALSE(urnw::startup::Enabled(directory.Bind(), TestLocations()));
}

// Without the template there is nothing to link to: the setting is
// unavailable (its row hidden), turning it on fails without touching
// anything, a launch migrates nothing, and a link to the missing template
// reads as off.
UR_TEST(LaunchAtStartup_WithoutTheTemplateTheSettingIsUnavailable) {
  FakeFiles fake;
  UR_EXPECT_FALSE(urnw::startup::Available(fake.Bind(), TestLocations()));
  UR_EXPECT_FALSE(urnw::startup::Set(fake.Bind(), TestLocations(), true));
  EXPECT_OPS(std::vector<std::string>{}, fake);
  fake.files[kEntry] = EntryWith("Exec=urnetwork\n");
  urnw::startup::Refresh(fake.Bind(), TestLocations());
  EXPECT_OPS(std::vector<std::string>{}, fake);
  FakeFiles dangling;
  dangling.links[kEntry] = kTemplate;
  UR_EXPECT_FALSE(urnw::startup::Enabled(dangling.Bind(), TestLocations()));
}

// Only the [Desktop Entry] group counts, with spaces around '=' ignored and
// comments, other groups and localized keys skipped.
UR_TEST(LaunchAtStartup_ParseDesktopEntryReadsTheDesktopEntryGroup) {
  const auto parsed = urnw::startup::ParseDesktopEntry(R"(# Exec=commented
[Desktop Entry]
Name[de]=URnetwork
Exec = urnetwork --autostart
Hidden=false

[Desktop Action Quit]
Exec=urnetwork --quit
Hidden=true
X-GNOME-Autostart-enabled=false
)");
  UR_EXPECT_TRUE(parsed.exec == "urnetwork --autostart");
  UR_EXPECT_FALSE(parsed.hidden);
  UR_EXPECT_FALSE(parsed.gnomeDisabled);
  UR_EXPECT_FALSE(urnw::startup::SwitchedOff(parsed));
  UR_EXPECT_TRUE(urnw::startup::ParseDesktopEntry("[Desktop Entry]\r\nHidden=true\r\n").hidden);
  UR_EXPECT_TRUE(urnw::startup::ParseDesktopEntry("Exec=urnetwork\n").exec.empty());
}

// The real filesystem does what the fake does, in a temporary directory.
UR_TEST(LaunchAtStartup_TheRealFilesystemBehavesAsTheFake) {
  namespace fs = std::filesystem;
  const fs::path root =
      fs::temp_directory_path() / ("urnetwork-startup-test-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root / "template");
  const fs::path templatePath = root / "template" / urnw::startup::kEntryName;
  std::ofstream(templatePath) << kTemplateContent;
  Locations locations;
  locations.configDir = (root / "config").string();
  locations.templatePath = templatePath.string();
  const fs::path entry = urnw::startup::EntryPath(locations);
  const fs::path temporary = urnw::startup::TemporaryPath(locations);
  const Files files = urnw::startup::PosixFiles();
  const auto exists = [](const fs::path& path) { return fs::exists(fs::symlink_status(path)); };

  UR_EXPECT_TRUE(urnw::startup::Available(files, locations));
  UR_EXPECT_FALSE(urnw::startup::Enabled(files, locations));
  UR_EXPECT_TRUE(urnw::startup::Set(files, locations, true));
  UR_EXPECT_TRUE(fs::is_symlink(entry) && fs::read_symlink(entry) == templatePath);
  UR_EXPECT_FALSE(exists(temporary));
  UR_EXPECT_TRUE(urnw::startup::Enabled(files, locations));
  UR_EXPECT_TRUE(urnw::startup::Set(files, locations, false));
  UR_EXPECT_FALSE(exists(entry));

  std::ofstream(entry) << EntryWith("Exec=urnetwork %u\n");
  urnw::startup::Refresh(files, locations);
  UR_EXPECT_TRUE(fs::is_symlink(entry) && fs::read_symlink(entry) == templatePath);

  fs::remove(entry);
  std::ofstream(entry) << EntryWith("Exec=urnetwork\nHidden=true\n");
  UR_EXPECT_FALSE(urnw::startup::Enabled(files, locations));
  urnw::startup::Refresh(files, locations);
  UR_EXPECT_FALSE(fs::is_symlink(entry));
  UR_EXPECT_TRUE(urnw::startup::Set(files, locations, true));
  UR_EXPECT_TRUE(fs::is_symlink(entry));

  // a directory where the entry goes is not an entry, though it opens
  fs::remove(entry);
  fs::create_directories(entry);
  UR_EXPECT_FALSE(urnw::startup::Enabled(files, locations));
  fs::remove(entry);

  fs::remove(templatePath);
  UR_EXPECT_FALSE(urnw::startup::Available(files, locations));
  UR_EXPECT_FALSE(urnw::startup::Enabled(files, locations));
  fs::remove_all(root);
}
