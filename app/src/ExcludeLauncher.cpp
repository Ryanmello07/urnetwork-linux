// SPDX-License-Identifier: MPL-2.0
#include "ExcludeLauncher.hpp"

#include <set>

#include "TunnelPolicy.hpp"  // IsCgroupV2Only, the daemon's own check

namespace urnw::exclude {
namespace {

// The comment every copy starts with, for whoever finds the file.
constexpr const char* kCopyHeader =
    R"(# Written by URnetwork: this launcher starts the app through urnetwork-exclude,
# outside the VPN tunnel. Remove it in URnetwork, under Settings > Connections >
# Exclude apps from the VPN.
)";

// What the copy leaves out of the [Desktop Entry] group: the names (the copy
// is named in the user's language, and GNOME shows X-GNOME-FullName in place
// of Name), TryExec (the launcher's replaces it), D-Bus activation and
// Implements (they would start the app without the copy's Exec), and the MIME
// types (the copy must never become a default handler).
bool DroppedFromCopy(const std::string& key) {
  return key == "Name" || key == "X-GNOME-FullName" || key == "TryExec" ||
         key == "DBusActivatable" || key == "Implements" || key == "MimeType" ||
         key == kSourceKey;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return prefix.size() <= text.size() && text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return suffix.size() <= text.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

std::string_view Trim(std::string_view text) {
  while (!text.empty() && IsSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && IsSpace(text.back())) text.remove_suffix(1);
  return text;
}

bool IsAsciiAlnum(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9');
}

// GLib's key names: anything but brackets, '=', whitespace and control
// characters (the spec's A-Za-z0-9- is a subset).
bool IsKeyName(std::string_view key) {
  if (key.empty()) return false;
  for (const char c : key) {
    const auto u = static_cast<unsigned char>(c);
    if (u <= ' ' || c == '[' || c == ']' || c == '=' || u == 0x7f) return false;
  }
  return true;
}

std::string BaseName(std::string_view path) {
  const size_t slash = path.rfind('/');
  return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// Inside double quotes a backslash escapes only these (the spec's rule).
bool EscapableInDoubleQuotes(char c) { return c == '"' || c == '`' || c == '$' || c == '\\'; }

// A list value (Actions=), each item escaped, ';' after each.
std::string JoinList(const std::vector<std::string>& items) {
  std::string out;
  for (const auto& item : items) {
    for (const char c : EscapeValue(item)) {
      if (c == ';') out += '\\';
      out += c;
    }
    out += ';';
  }
  return out;
}

}  // namespace

std::optional<DesktopEntryFile> DesktopEntryFile::Parse(std::string_view text) {
  DesktopEntryFile file;
  std::string group;
  bool desktopEntry = false;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    DesktopEntryLine entry;
    entry.text = std::string(line);
    const std::string_view trimmed = Trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      entry.group = group;
    } else if (trimmed.front() == '[') {
      if (trimmed.size() < 3 || trimmed.back() != ']') return std::nullopt;
      const std::string_view name = trimmed.substr(1, trimmed.size() - 2);
      if (name.find_first_of("[]") != std::string_view::npos) return std::nullopt;
      group = std::string(name);
      entry.group = group;
      entry.header = true;
      if (group == kDesktopEntryGroup) desktopEntry = true;
    } else {
      // key=value or key[locale]=value; an entry before any group is invalid
      const size_t equals = trimmed.find('=');
      if (equals == std::string_view::npos || group.empty()) return std::nullopt;
      std::string_view name = Trim(trimmed.substr(0, equals));
      std::string_view locale;
      const size_t open = name.find('[');
      if (open != std::string_view::npos) {
        if (name.back() != ']' || name.size() < open + 3) return std::nullopt;
        locale = name.substr(open + 1, name.size() - open - 2);
        name = name.substr(0, open);
      }
      if (!IsKeyName(name)) return std::nullopt;
      entry.group = group;
      entry.key = std::string(name);
      entry.locale = std::string(locale);
      entry.value = std::string(Trim(trimmed.substr(equals + 1)));
    }
    file.lines_.push_back(std::move(entry));
  }
  if (!desktopEntry) return std::nullopt;
  return file;
}

std::optional<std::string> DesktopEntryFile::Raw(std::string_view group,
                                                 std::string_view key) const {
  // the last occurrence wins, as GLib reads a repeated key
  std::optional<std::string> value;
  for (const auto& line : lines_) {
    if (line.group == group && line.key == key && line.locale.empty()) value = line.value;
  }
  return value;
}

std::optional<std::string> DesktopEntryFile::Value(std::string_view group,
                                                   std::string_view key) const {
  const auto raw = Raw(group, key);
  if (!raw) return std::nullopt;
  return UnescapeValue(*raw);
}

bool DesktopEntryFile::Boolean(std::string_view group, std::string_view key) const {
  const auto value = Value(group, key);
  return value && (*value == "true" || *value == "1");
}

bool DesktopEntryFile::HasGroup(std::string_view group) const {
  for (const auto& line : lines_) {
    if (line.header && line.group == group) return true;
  }
  return false;
}

std::string UnescapeValue(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] != '\\' || i + 1 == raw.size()) {
      out += raw[i];
      continue;
    }
    switch (raw[++i]) {
      case 's': out += ' '; break;
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': out += '\r'; break;
      case '\\': out += '\\'; break;
      default:
        out += '\\';
        out += raw[i];
        break;
    }
  }
  return out;
}

std::string EscapeValue(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    switch (text[i]) {
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case ' ':
        out += i == 0 ? "\\s" : " ";
        break;
      default: out += text[i]; break;
    }
  }
  return out;
}

std::vector<std::string> ListItems(std::string_view raw) {
  std::vector<std::string> items;
  std::string item;
  auto flush = [&] {
    if (!item.empty()) items.push_back(UnescapeValue(item));
    item.clear();
  };
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 1 < raw.size()) {
      // \; is a ';' inside an item; any other escape is the item's own
      if (raw[i + 1] != ';') item += '\\';
      item += raw[++i];
    } else if (raw[i] == ';') {
      flush();
    } else {
      item += raw[i];
    }
  }
  flush();
  return items;
}

std::optional<std::vector<std::string>> SplitExec(std::string_view exec) {
  enum class Quote { None, Double, Single };
  std::vector<std::string> args;
  std::string arg;
  bool inArg = false;
  Quote quote = Quote::None;
  for (size_t i = 0; i < exec.size(); ++i) {
    const char c = exec[i];
    switch (quote) {
      case Quote::Double:
        if (c == '"') {
          quote = Quote::None;
        } else if (c == '\\' && i + 1 < exec.size() && EscapableInDoubleQuotes(exec[i + 1])) {
          arg += exec[++i];
        } else {
          arg += c;
        }
        break;
      case Quote::Single:
        if (c == '\'') {
          quote = Quote::None;
        } else {
          arg += c;
        }
        break;
      case Quote::None:
        if (c == ' ' || c == '\t' || c == '\n') {
          if (inArg) args.push_back(arg);
          arg.clear();
          inArg = false;
        } else if (c == '"') {
          quote = Quote::Double;
          inArg = true;
        } else if (c == '\'') {
          quote = Quote::Single;
          inArg = true;
        } else if (c == '\\') {
          if (i + 1 == exec.size()) return std::nullopt;  // nothing left to escape
          arg += exec[++i];
          inArg = true;
        } else {
          arg += c;
          inArg = true;
        }
        break;
    }
  }
  if (quote != Quote::None) return std::nullopt;
  if (inArg) args.push_back(arg);
  if (args.empty()) return std::nullopt;
  return args;
}

std::string ProgramOf(const std::vector<std::string>& args) {
  size_t i = 0;
  if (i < args.size() && BaseName(args[i]) == "env") {
    ++i;
    while (i < args.size()) {
      const std::string& arg = args[i];
      if (arg == "-u" || arg == "--unset" || arg == "-C" || arg == "--chdir") {
        i += 2;  // the option and its value
      } else if (StartsWith(arg, "-") || (arg.find('=') != std::string::npos && arg[0] != '/')) {
        ++i;
      } else {
        break;
      }
    }
  }
  return i < args.size() ? args[i] : std::string();
}

std::optional<std::string> WrapExec(std::string_view rawExec) {
  const std::string_view exec = Trim(rawExec);
  const auto args = SplitExec(UnescapeValue(exec));
  if (!args) return std::nullopt;
  const std::string program = ProgramOf(*args);
  if (program.empty() || BaseName(program) == kLauncher) return std::nullopt;
  return std::string(kLauncher) + " " + std::string(exec);
}

Offer Classify(const DesktopEntryFile& file, std::string_view desktopId) {
  const std::string group = kDesktopEntryGroup;
  if (StartsWith(desktopId, kCopyIdPrefix) || file.Raw(group, kSourceKey)) return Offer::Excluded;
  if (desktopId == kOwnDesktopId) return Offer::Own;
  if (file.Value(group, "Type") != std::optional<std::string>("Application")) {
    return Offer::NotAnApplication;
  }
  const auto exec = file.Raw(group, "Exec");
  if (!exec || Trim(*exec).empty()) return Offer::NotAnApplication;
  if (file.Boolean(group, "NoDisplay") || file.Boolean(group, "Hidden")) return Offer::Hidden;
  const auto args = SplitExec(UnescapeValue(Trim(*exec)));
  if (!args) return Offer::InvalidExec;
  const std::string program = ProgramOf(*args);
  if (program.empty()) return Offer::InvalidExec;
  const std::string base = BaseName(program);
  if (base == kLauncher) return Offer::Excluded;
  if (file.Raw(group, "X-Flatpak") || file.Raw(group, "X-SnapInstanceName") ||
      base == "flatpak" || base == "snap" || StartsWith(program, "/snap/")) {
    return Offer::Sandboxed;
  }
  if (CopyFileName(desktopId).empty()) return Offer::UnsafeId;
  return Offer::Offered;
}

std::string CopyFileName(std::string_view desktopId) {
  static constexpr std::string_view kSuffix = ".desktop";
  if (desktopId.size() <= kSuffix.size() || 200 < desktopId.size()) return std::string();
  if (!EndsWith(desktopId, kSuffix) || StartsWith(desktopId, kCopyIdPrefix)) return std::string();
  if (!IsAsciiAlnum(desktopId.front())) return std::string();
  for (const char c : desktopId) {
    if (!IsAsciiAlnum(c) && c != '.' && c != '_' && c != '-' && c != '+') return std::string();
  }
  return std::string(kCopyIdPrefix) + std::string(desktopId);
}

std::optional<std::string> MakeCopy(const DesktopEntryFile& file, std::string_view desktopId,
                                    std::string_view name) {
  if (Classify(file, desktopId) != Offer::Offered) return std::nullopt;

  // The actions the copy keeps: listed, present, and with a command line the
  // launcher can wrap. Any other would start the app inside the tunnel from
  // a launcher that says it is outside.
  std::vector<std::string> actions;
  std::set<std::string> actionGroups;
  if (const auto listed = file.Raw(kDesktopEntryGroup, "Actions")) {
    for (const auto& action : ListItems(*listed)) {
      const std::string group = kDesktopActionGroupPrefix + action;
      const auto exec = file.Raw(group, "Exec");
      if (file.HasGroup(group) && exec && WrapExec(*exec) && actionGroups.insert(group).second) {
        actions.push_back(action);
      }
    }
  }

  std::string out = kCopyHeader;
  std::set<std::string> seenGroups;
  std::string group;
  bool keep = false;
  for (const auto& line : file.Lines()) {
    if (line.header) {
      group = line.group;
      // a repeated group is dropped; so is every group other than the entry
      // and its kept actions (vendor groups can carry command lines too)
      keep = seenGroups.insert(group).second &&
             (group == kDesktopEntryGroup || actionGroups.count(group) != 0);
      if (!keep) continue;
      out += line.text + "\n";
      if (group == kDesktopEntryGroup) {
        out += "Name=" + EscapeValue(name) + "\n";
        out += std::string("TryExec=") + kLauncher + "\n";
        out += std::string(kSourceKey) + "=" + EscapeValue(desktopId) + "\n";
      }
      continue;
    }
    // the comments above the first group belong to the original
    if (line.group.empty() || !keep) continue;
    if (group == kDesktopEntryGroup && DroppedFromCopy(line.key)) continue;
    if (line.key == "Exec") {
      const auto wrapped = line.locale.empty() ? WrapExec(line.value) : std::nullopt;
      if (wrapped) out += "Exec=" + *wrapped + "\n";
      continue;
    }
    if (group == kDesktopEntryGroup && line.key == "Actions") {
      if (line.locale.empty() && !actions.empty()) out += "Actions=" + JoinList(actions) + "\n";
      continue;
    }
    out += line.text + "\n";
  }
  return out;
}

std::optional<std::string> CopySource(std::string_view text) {
  const auto file = DesktopEntryFile::Parse(text);
  if (!file) return std::nullopt;
  auto source = file->Value(kDesktopEntryGroup, kSourceKey);
  if (!source || source->empty()) return std::nullopt;
  return source;
}

bool ExclusionAvailable(std::string_view procSelfCgroup, bool launcherInstalled) {
  return launcherInstalled && IsCgroupV2Only(procSelfCgroup);
}

}  // namespace urnw::exclude
