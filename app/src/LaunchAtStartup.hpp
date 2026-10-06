// "Launch URnetwork on system startup" on Linux (owner decision, 2026-10-05:
// Linux matches Windows, which takes the macOS setting and its default).
//
// macOS and Windows. Settings has the toggle (launch_urnetwork_on_system_startup).
// It shows whether a sign-in will start the app (macOS: a login item that is
// enabled; Windows: the user's Run value, not switched off in Task Manager),
// registers the app when turned on and unregisters it when turned off, and
// shows the real state when that fails. Nothing registers the app by itself,
// so it starts off.
//
// Linux, the same way. The registration is the user's own XDG autostart
// entry, $XDG_CONFIG_HOME/autostart/com.bringyour.network.desktop, a link to
// the daemon package's root-owned template (MIGRATION.md), whose Exec runs the
// launcher with --autostart, so a login shows only the tray icon
// (InstanceHandover.hpp). The desktop's own startup settings can switch the
// entry off without removing it (Hidden=true, or GNOME's
// X-GNOME-Autostart-enabled=false), and the toggle then shows it as off.
//   * Turning it on links the entry atomically: a link under a temporary name
//     is renamed over the entry, never removed first, so an interrupted switch
//     cannot leave the user with no entry. It replaces an entry switched off,
//     since the user has just asked for it here.
//   * Turning it off removes the entry.
//   * Nothing creates the entry by itself (kDefaultEnabled). At each launch an
//     entry that runs the launcher the way the template did before
//     --autostart, or the way a copy of the menu entry does, is replaced by the
//     link, so its logins show only the tray. An entry switched off, one that
//     runs anything else (an AppImage path, an env prefix) and a missing one
//     are left as they are.
//   * Without the template (the Flatpak, a build run from its tree) the
//     setting is unavailable and its row hidden: a link would point at nothing.
//
// C++17 and POSIX, free of glib: tests/LaunchAtStartupTest.cpp runs it against
// a fake filesystem that logs every operation, and against a temporary
// directory. SettingsPage and main.cpp pass g_get_user_config_dir().
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace urnw::startup {

inline constexpr const char* kEntryName = "com.bringyour.network.desktop";
// Installed by the daemon packages (meson.build, packaging/lib/common.sh).
inline constexpr const char* kTemplatePath =
    "/etc/urnetwork/autostart/com.bringyour.network.desktop";
// macOS's default: off until the user turns it on.
inline constexpr bool kDefaultEnabled = false;

// Where the setting lives.
struct Locations {
  // The user's configuration directory ($XDG_CONFIG_HOME, ~/.config).
  std::string configDir;
  std::string templatePath = kTemplatePath;
};

inline std::string AutostartDir(const Locations& locations) {
  return locations.configDir + "/autostart";
}

inline std::string EntryPath(const Locations& locations) {
  return AutostartDir(locations) + "/" + kEntryName;
}

// Not a desktop entry, so no session reads a leftover one.
inline std::string TemporaryPath(const Locations& locations) {
  return EntryPath(locations) + ".new";
}

// What a path holds, without following a link.
enum class Node { Absent, Link, File, Other };

// The filesystem as the setting uses it.
struct Files {
  std::function<Node(const std::string& path)> node;
  // The content, following a link; nullopt when it cannot be read (nothing
  // there, or a link to nothing).
  std::function<std::optional<std::string>(const std::string& path)> read;
  std::function<bool(const std::string& dir)> makeDirectories;
  std::function<bool(const std::string& target, const std::string& path)> link;
  std::function<bool(const std::string& from, const std::string& to)> rename;
  std::function<bool(const std::string& path)> remove;
};

// The real filesystem.
inline Files PosixFiles() {
  Files files;
  files.node = [](const std::string& path) {
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) return errno == ENOENT ? Node::Absent : Node::Other;
    if (S_ISLNK(info.st_mode)) return Node::Link;
    if (S_ISREG(info.st_mode)) return Node::File;
    return Node::Other;
  };
  files.read = [](const std::string& path) -> std::optional<std::string> {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  };
  files.makeDirectories = [](const std::string& dir) {
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    return std::filesystem::is_directory(dir, error);
  };
  files.link = [](const std::string& target, const std::string& path) {
    return ::symlink(target.c_str(), path.c_str()) == 0;
  };
  files.rename = [](const std::string& from, const std::string& to) {
    return ::rename(from.c_str(), to.c_str()) == 0;
  };
  files.remove = [](const std::string& path) { return ::unlink(path.c_str()) == 0; };
  return files;
}

// The keys of a desktop entry the setting reads, from its [Desktop Entry]
// group only.
struct DesktopEntry {
  std::string exec;
  bool hidden = false;
  bool gnomeDisabled = false;
};

inline DesktopEntry ParseDesktopEntry(std::string_view content) {
  const auto trim = [](std::string_view text) {
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return std::string_view();
    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
  };
  DesktopEntry entry;
  bool inGroup = false;
  size_t start = 0;
  while (start <= content.size()) {
    const size_t end = content.find('\n', start);
    const std::string_view line =
        trim(content.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                                 : end - start));
    if (!line.empty() && line.front() == '[') {
      inGroup = line == "[Desktop Entry]";
    } else if (inGroup && !line.empty() && line.front() != '#') {
      const size_t equals = line.find('=');
      if (equals != std::string_view::npos) {
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (key == "Exec") {
          entry.exec = std::string(value);
        } else if (key == "Hidden") {
          entry.hidden = value == "true";
        } else if (key == "X-GNOME-Autostart-enabled") {
          entry.gnomeDisabled = value == "false";
        }
      }
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return entry;
}

// Switched off by the desktop's startup settings, though still there.
inline bool SwitchedOff(const DesktopEntry& entry) {
  return entry.hidden || entry.gnomeDisabled;
}

// An Exec that runs the launcher without --autostart: the template's before
// it (urnetwork), or a copy of the menu entry's (urnetwork %u).
inline bool RunsLauncherWithoutAutostart(std::string_view exec) {
  std::vector<std::string> words;
  std::istringstream in{std::string(exec)};
  for (std::string word; in >> word;) words.push_back(word);
  if (words.empty() || (words[0] != "urnetwork" && words[0] != "/usr/bin/urnetwork")) return false;
  for (size_t at = 1; at < words.size(); ++at) {
    if (words[at] != "%u" && words[at] != "%U") return false;
  }
  return true;
}

// The template is there to link to.
inline bool Available(const Files& files, const Locations& locations) {
  return files.read(locations.templatePath).has_value();
}

// A login will start the app: the entry is a file, or a link to one, and not
// switched off.
inline bool Enabled(const Files& files, const Locations& locations) {
  const std::string entry = EntryPath(locations);
  const Node node = files.node(entry);
  if (node != Node::Link && node != Node::File) return false;
  const std::optional<std::string> content = files.read(entry);
  return content && !SwitchedOff(ParseDesktopEntry(*content));
}

// True when the filesystem now says what was asked.
inline bool Set(const Files& files, const Locations& locations, bool enabled) {
  const std::string entry = EntryPath(locations);
  if (!enabled) {
    if (files.node(entry) != Node::Absent) files.remove(entry);
    return !Enabled(files, locations);
  }
  if (!Available(files, locations) || !files.makeDirectories(AutostartDir(locations))) {
    return false;
  }
  const std::string temporary = TemporaryPath(locations);
  // a leftover from an interrupted switch would refuse the link
  if (files.node(temporary) != Node::Absent) files.remove(temporary);
  if (!files.link(locations.templatePath, temporary)) return false;
  if (!files.rename(temporary, entry)) {
    files.remove(temporary);
    return false;
  }
  return Enabled(files, locations);
}

// At each launch: an entry that runs the launcher without --autostart is
// replaced by the link. Never creates one.
inline void Refresh(const Files& files, const Locations& locations) {
  if (!Available(files, locations)) return;
  const std::string entry = EntryPath(locations);
  // a link is the template's, which the package keeps current
  if (files.node(entry) != Node::File) return;
  const std::optional<std::string> content = files.read(entry);
  if (!content) return;
  const DesktopEntry parsed = ParseDesktopEntry(*content);
  if (SwitchedOff(parsed) || !RunsLauncherWithoutAutostart(parsed.exec)) return;
  Set(files, locations, true);
}

}  // namespace urnw::startup
