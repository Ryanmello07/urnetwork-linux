// Excluding an installed app from the tunnel from the GUI (Settings >
// Connections > Exclude apps from the VPN): which .desktop apps can be
// excluded, and the launcher copy that excludes one.
//
// The daemon packages install `urnetwork-exclude <command>`
// (app/packaging/urnetwork-exclude), which runs a command in the user's
// urnetwork-exclude.slice; urnetworkd lets that slice out of the tunnel. nft
// cannot match an executable path, so an app is excluded by how it is started:
// the sheet writes a copy of the app's desktop entry, "<App> (outside VPN)",
// to the user's applications directory with its Exec run through
// urnetwork-exclude. Only the copy is outside the tunnel; the original
// launcher is not touched.
//
// The copy prefixes the original command line unchanged, so its quoting and
// its field codes reach the desktop's launcher exactly as before: %f/%F/%u/%U
// still carry the files and URLs, %i the icon, and %% a percent sign. (%c and
// %k name the copy rather than the original.) It drops what would act outside
// the copy's own launch: the MIME types (the copy must never become a default
// handler, which would move every opened link or file outside the tunnel),
// D-Bus activation, Implements and any action it cannot wrap. TryExec names
// the launcher, so the copy leaves the menus with the daemon package.
//
// Flatpak and Snap apps are not offered: `flatpak run` and `snap run` move the
// app into a systemd scope of their own, out of the slice, so the copy would
// start them inside the tunnel while it says otherwise.
//
// Pure C++17, no GTK, GLib or SDK: ExcludeAppsSheet.cpp finds the apps (GIO)
// and writes the files; tests/ExcludeLauncherTest.cpp covers everything here.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace urnw::exclude {

// The launcher the daemon packages install, run by name through $PATH.
inline constexpr const char* kLauncher = "urnetwork-exclude";
// Every copy's desktop file id is this prefix plus its app's id, so a copy
// never shadows the app's own entry (a same-named file in the user's
// applications directory would replace it).
inline constexpr const char* kCopyIdPrefix = "urnetwork-exclude-";
// The key that marks a file as one of the copies and names its app's id. A
// file without it is never removed.
inline constexpr const char* kSourceKey = "X-URnetwork-Exclude-Source";
// URnetwork's own entry (packaging/com.bringyour.network.desktop): excluding
// the app from its own tunnel would change nothing.
inline constexpr const char* kOwnDesktopId = "com.bringyour.network.desktop";

inline constexpr const char* kDesktopEntryGroup = "Desktop Entry";
inline constexpr const char* kDesktopActionGroupPrefix = "Desktop Action ";

// One line of a desktop entry file, kept as read so a line the copy does not
// change is written back unchanged.
struct DesktopEntryLine {
  std::string text;    // the line, without its line break
  std::string group;   // the group it belongs to ("" before the first header)
  bool header = false; // a [group] line
  std::string key;     // "Name" for Name[de]=...; empty for headers, comments, blanks
  std::string locale;  // "de" for Name[de]=...
  std::string value;   // the raw value, still escaped
};

// A desktop entry file (freedesktop.org Desktop Entry Specification 1.5).
class DesktopEntryFile {
 public:
  // nullopt when a line is neither a group header, an entry, a comment nor
  // blank, or when there is no [Desktop Entry] group.
  static std::optional<DesktopEntryFile> Parse(std::string_view text);

  // The unlocalized key's raw (escaped) value in the group; nullopt when absent.
  std::optional<std::string> Raw(std::string_view group, std::string_view key) const;
  // The same, unescaped (string values).
  std::optional<std::string> Value(std::string_view group, std::string_view key) const;
  // A boolean key: true only for "true".
  bool Boolean(std::string_view group, std::string_view key) const;
  bool HasGroup(std::string_view group) const;

  const std::vector<DesktopEntryLine>& Lines() const { return lines_; }

 private:
  std::vector<DesktopEntryLine> lines_;
};

// The string escapes of a value: \s \n \t \r \\ (any other backslash is kept).
std::string UnescapeValue(std::string_view raw);
// The inverse, for a value the copy writes: \\, \n, \t, \r, and a leading space
// as \s.
std::string EscapeValue(std::string_view text);
// A list value's items (Actions=a;b;): split on unescaped ';', each unescaped.
std::vector<std::string> ListItems(std::string_view raw);

// An Exec command line, string escapes already undone, split into its
// arguments by the spec's quoting rules: double quotes enclose an argument,
// and inside them a backslash escapes ", `, $ and \. Outside quotes a
// backslash escapes the next character and single quotes enclose a literal,
// as the desktops' own parsers (GLib's g_shell_parse_argv) accept. Field codes
// stay as written ("%U", "--url=%u", "%%"). nullopt for an unterminated quote
// or a command line with no argument.
std::optional<std::vector<std::string>> SplitExec(std::string_view exec);

// The program a split command line runs: the first argument, past `env` and
// the options and NAME=value assignments it takes. "" when there is none.
std::string ProgramOf(const std::vector<std::string>& args);

// The copy's Exec: the launcher, then the original raw command line
// unchanged. nullopt when the original does not parse or already runs through
// the launcher.
std::optional<std::string> WrapExec(std::string_view rawExec);

// Whether an app is offered in the sheet.
enum class Offer {
  Offered,
  NotAnApplication,  // not Type=Application, or no Exec
  Hidden,            // NoDisplay=true or Hidden=true
  InvalidExec,       // the command line does not parse
  Sandboxed,         // a Flatpak or Snap app: it leaves the slice by itself
  Excluded,          // one of the copies, or it already runs through the launcher
  Own,               // URnetwork itself
  UnsafeId,          // an id that is not a plain file name
};
Offer Classify(const DesktopEntryFile& file, std::string_view desktopId);

// The copy's file name for an app's desktop id: kCopyIdPrefix + id, or "" for
// an id that is not a plain .desktop file name or is itself a copy's.
std::string CopyFileName(std::string_view desktopId);

// The copy of an offered app's entry, named `name` (the sheet passes
// "<App> (outside VPN)" in the user's language). nullopt when the app is not
// offered.
std::optional<std::string> MakeCopy(const DesktopEntryFile& file, std::string_view desktopId,
                                    std::string_view name);

// The app id a copy was made for, from the copy's text; nullopt for any file
// that is not one of the copies.
std::optional<std::string> CopySource(std::string_view text);

// Whether this host can exclude apps: the cgroup v2 unified hierarchy alone
// (the daemon's own check, IsCgroupV2Only on /proc/self/cgroup) and the
// launcher installed. The sheet's row is hidden otherwise.
bool ExclusionAvailable(std::string_view procSelfCgroup, bool launcherInstalled);

}  // namespace urnw::exclude
