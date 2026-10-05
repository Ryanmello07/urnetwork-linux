// Excluding an installed app from the tunnel from the GUI (ExcludeLauncher.hpp):
// reading a .desktop file, splitting and wrapping its Exec (quoting, string
// escapes, field codes), which apps are offered, the launcher copy, and the
// host check. The sheet and its Settings row need GTK, so the wiring cases
// read their source, and the strings they show are pinned against po/en.po.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ExcludeLauncher.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::exclude::Classify;
using urnw::exclude::CopyFileName;
using urnw::exclude::CopySource;
using urnw::exclude::DesktopEntryFile;
using urnw::exclude::MakeCopy;
using urnw::exclude::Offer;
using urnw::exclude::SplitExec;
using urnw::exclude::UnescapeValue;
using urnw::exclude::WrapExec;

using Args = std::vector<std::string>;

const char* const kEntry = "Desktop Entry";

// A browser entry with everything the copy has to handle: comments above the
// group, localized names, GNOME's full name, MIME types, D-Bus keys, an action
// it wraps twice, an action without a command line, and a vendor group.
const char* const kFirefox = R"desktop(# Firefox desktop entry
[Desktop Entry]
Version=1.0
Name=Firefox
Name[de]=Firefox
X-GNOME-FullName=Firefox Web Browser
GenericName=Web Browser
GenericName[de]=Webbrowser
Comment=Browse the Web
Exec=firefox %u
TryExec=firefox
Icon=firefox
Terminal=false
Type=Application
MimeType=text/html;x-scheme-handler/http;x-scheme-handler/https;
Categories=Network;WebBrowser;
DBusActivatable=false
Implements=org.freedesktop.Application;
StartupNotify=true
StartupWMClass=firefox
Actions=new-window;new-private-window;profile-manager;

[Desktop Action new-window]
Name=New Window
Exec=firefox --new-window %u

[Desktop Action new-private-window]
Name=New Private Window
Name[de]=Neues privates Fenster
Exec="firefox" --private-window %u

[Desktop Action profile-manager]
Name=Profile Manager

[X-Vendor Shortcut]
Exec=firefox --safe-mode
)desktop";

const char* const kFirefoxCopy =
    R"desktop(# Written by URnetwork: this launcher starts the app through urnetwork-exclude,
# outside the VPN tunnel. Remove it in URnetwork, under Settings > Connections >
# Exclude apps from the VPN.
[Desktop Entry]
Name=Firefox (outside VPN)
TryExec=urnetwork-exclude
X-URnetwork-Exclude-Source=firefox.desktop
Version=1.0
GenericName=Web Browser
GenericName[de]=Webbrowser
Comment=Browse the Web
Exec=urnetwork-exclude firefox %u
Icon=firefox
Terminal=false
Type=Application
Categories=Network;WebBrowser;
StartupNotify=true
StartupWMClass=firefox
Actions=new-window;new-private-window;

[Desktop Action new-window]
Name=New Window
Exec=urnetwork-exclude firefox --new-window %u

[Desktop Action new-private-window]
Name=New Private Window
Name[de]=Neues privates Fenster
Exec=urnetwork-exclude "firefox" --private-window %u

)desktop";

// A [Desktop Entry] group with the given lines.
std::string Entry(const std::string& lines) { return "[Desktop Entry]\n" + lines; }

Offer ClassifyText(const std::string& text, const std::string& id) {
  const auto file = DesktopEntryFile::Parse(text);
  if (!file) return Offer::NotAnApplication;
  return Classify(*file, id);
}

std::string ReadSource(const std::string& name) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + name, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The definition that starts at `signature`, through the closing brace in its
// first column; empty when it is gone.
std::string DefinitionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

// ---- reading a desktop entry ----------------------------------------------------

UR_TEST(desktopEntryReadsGroupsKeysAndLocales) {
  const auto file = DesktopEntryFile::Parse(
      "# a comment\r\n"
      "[Desktop Entry]\r\n"
      "Name = Text Editor\r\n"
      "Name[de]=Texteditor\r\n"
      "\r\n"
      "Exec=gedit %U\r\n"
      "[Desktop Action new]\r\n"
      "Exec=gedit --new-window\r\n");
  UR_EXPECT_TRUE(file.has_value());
  if (!file) return;
  UR_EXPECT_TRUE(file->Raw(kEntry, "Name") == std::optional<std::string>("Text Editor"));
  UR_EXPECT_TRUE(file->Raw(kEntry, "Exec") == std::optional<std::string>("gedit %U"));
  UR_EXPECT_TRUE(file->Raw("Desktop Action new", "Exec") ==
                 std::optional<std::string>("gedit --new-window"));
  UR_EXPECT_FALSE(file->Raw(kEntry, "Icon").has_value());
  UR_EXPECT_TRUE(file->HasGroup("Desktop Action new"));
  UR_EXPECT_FALSE(file->HasGroup("Desktop Action old"));
  // every line is kept as read (without its line break), in order
  UR_EXPECT_EQ(8, file->Lines().size());
  UR_EXPECT_TRUE(file->Lines()[3].key == "Name" && file->Lines()[3].locale == "de" &&
                 file->Lines()[3].value == "Texteditor");
  UR_EXPECT_TRUE(file->Lines()[2].text == "Name = Text Editor");
  UR_EXPECT_TRUE(file->Lines()[0].group.empty() && file->Lines()[1].header);
}

UR_TEST(desktopEntryRefusesWhatIsNotOne) {
  UR_EXPECT_FALSE(DesktopEntryFile::Parse("").has_value());
  UR_EXPECT_FALSE(DesktopEntryFile::Parse("[Other]\nName=x\n").has_value());
  UR_EXPECT_FALSE(DesktopEntryFile::Parse("Name=x\n[Desktop Entry]\n").has_value());
  UR_EXPECT_FALSE(DesktopEntryFile::Parse("[Desktop Entry]\nnot a key value pair\n").has_value());
  UR_EXPECT_FALSE(DesktopEntryFile::Parse("[Desktop Entry\nName=x\n").has_value());
  UR_EXPECT_FALSE(DesktopEntryFile::Parse("[Desktop Entry]\nName[]=x\n").has_value());
  UR_EXPECT_TRUE(DesktopEntryFile::Parse("[Desktop Entry]\n").has_value());
}

UR_TEST(desktopEntryValuesUndoTheStringEscapes) {
  UR_EXPECT_TRUE(UnescapeValue(R"(a\sb\nc\td\re\\f\x)") == "a b\nc\td\re\\f\\x");
  UR_EXPECT_TRUE(urnw::exclude::EscapeValue(" a\\b\tc\nd") == R"(\sa\\b\tc\nd)");
  UR_EXPECT_TRUE(UnescapeValue(urnw::exclude::EscapeValue(" a\\b\tc\nd")) == " a\\b\tc\nd");
  const auto file = DesktopEntryFile::Parse(Entry("Name=My\\sApp\nNoDisplay=true\nHidden=1\n"));
  UR_EXPECT_TRUE(file && file->Value(kEntry, "Name") == std::optional<std::string>("My App"));
  UR_EXPECT_TRUE(file && file->Boolean(kEntry, "NoDisplay") && file->Boolean(kEntry, "Hidden"));
  UR_EXPECT_TRUE(file && !file->Boolean(kEntry, "Terminal"));
}

UR_TEST(desktopEntryListsSplitOnUnescapedSemicolons) {
  using urnw::exclude::ListItems;
  UR_EXPECT_TRUE(ListItems("new-window;new-private-window;") ==
                 Args({"new-window", "new-private-window"}));
  UR_EXPECT_TRUE(ListItems("a\\;b;;c") == Args({"a;b", "c"}));
  UR_EXPECT_TRUE(ListItems("") == Args());
}

// ---- Exec: quoting and field codes ----------------------------------------------

UR_TEST(execSplitsOnSpacesAndKeepsTheFieldCodes) {
  UR_EXPECT_TRUE(SplitExec("firefox %u") == Args({"firefox", "%u"}));
  UR_EXPECT_TRUE(SplitExec("app  --url=%u\t%F %% %i %c %k") ==
                 Args({"app", "--url=%u", "%F", "%%", "%i", "%c", "%k"}));
}

UR_TEST(execDoubleQuotesEncloseOneArgument) {
  UR_EXPECT_TRUE(SplitExec(R"("/opt/My App/bin/app" --name "a b" %U)") ==
                 Args({"/opt/My App/bin/app", "--name", "a b", "%U"}));
  UR_EXPECT_TRUE(SplitExec(R"(app "" --x)") == Args({"app", "", "--x"}));
  // inside double quotes a backslash escapes ", `, $ and \ only
  UR_EXPECT_TRUE(SplitExec(R"(app "say \"hi\"" "\$HOME" "a\`b" "c\\d" "e\zf")") ==
                 Args({"app", "say \"hi\"", "$HOME", "a`b", "c\\d", "e\\zf"}));
}

UR_TEST(execStringEscapesComeBeforeTheQuoting) {
  // the spec's example: a literal backslash in a quoted argument is \\\\ in the
  // file, and \s is a space that separates arguments unless it is quoted
  UR_EXPECT_TRUE(SplitExec(UnescapeValue(R"(app "a\\\\b" my\sname "x\sy")")) ==
                 Args({"app", "a\\b", "my", "name", "x y"}));
}

UR_TEST(execAcceptsWhatTheDesktopsAccept) {
  // single quotes and a backslash outside quotes, as g_shell_parse_argv reads them
  UR_EXPECT_TRUE(SplitExec(R"(sh -c 'echo "$1"; exit' x a\ b)") ==
                 Args({"sh", "-c", "echo \"$1\"; exit", "x", "a b"}));
}

UR_TEST(execRefusesAnUnterminatedQuoteOrNoArgument) {
  UR_EXPECT_FALSE(SplitExec(R"(app "a)").has_value());
  UR_EXPECT_FALSE(SplitExec("app 'a").has_value());
  UR_EXPECT_FALSE(SplitExec(R"(app \)").has_value());
  UR_EXPECT_FALSE(SplitExec("").has_value());
  UR_EXPECT_FALSE(SplitExec("  \t ").has_value());
}

UR_TEST(execProgramIsPastEnvAndItsAssignments) {
  using urnw::exclude::ProgramOf;
  UR_EXPECT_TRUE(ProgramOf({"env", "FOO=1", "BAR=2", "/usr/bin/app", "%U"}) == "/usr/bin/app");
  UR_EXPECT_TRUE(ProgramOf({"/usr/bin/env", "-u", "LANG", "-i", "PATH=/bin", "app"}) == "app");
  UR_EXPECT_TRUE(ProgramOf({"app", "x=1"}) == "app");
  UR_EXPECT_TRUE(ProgramOf({"env", "A=1"}).empty());
}

UR_TEST(wrapPutsTheLauncherFirstAndKeepsTheCommandLine) {
  UR_EXPECT_TRUE(WrapExec("firefox %u") ==
                 std::optional<std::string>("urnetwork-exclude firefox %u"));
  UR_EXPECT_TRUE(WrapExec("  firefox %u  ") ==
                 std::optional<std::string>("urnetwork-exclude firefox %u"));
  // the quoting, the string escapes and the field codes go through unchanged
  UR_EXPECT_TRUE(
      WrapExec(R"("/opt/My App/bin/app" --url=%u %%)") ==
      std::optional<std::string>(R"(urnetwork-exclude "/opt/My App/bin/app" --url=%u %%)"));
  UR_EXPECT_TRUE(WrapExec(R"(app "a\\\\b" my\sname)") ==
                 std::optional<std::string>(R"(urnetwork-exclude app "a\\\\b" my\sname)"));
  // so the desktop runs the launcher with the original arguments after it
  const auto wrapped = WrapExec(R"("/opt/My App/bin/app" --url=%u %F)");
  UR_EXPECT_TRUE(wrapped.has_value());
  if (!wrapped) return;
  UR_EXPECT_TRUE(SplitExec(UnescapeValue(*wrapped)) ==
                 Args({"urnetwork-exclude", "/opt/My App/bin/app", "--url=%u", "%F"}));
}

UR_TEST(wrapRefusesABrokenOrAlreadyWrappedCommandLine) {
  UR_EXPECT_FALSE(WrapExec(R"(app "a)").has_value());
  UR_EXPECT_FALSE(WrapExec("").has_value());
  UR_EXPECT_FALSE(WrapExec("env A=1").has_value());
  UR_EXPECT_FALSE(WrapExec("urnetwork-exclude firefox").has_value());
  UR_EXPECT_FALSE(WrapExec("/usr/bin/urnetwork-exclude firefox").has_value());
  UR_EXPECT_FALSE(WrapExec("env A=1 urnetwork-exclude firefox").has_value());
}

// ---- which apps are offered -----------------------------------------------------

UR_TEST(classifyOffersAVisibleApplication) {
  UR_EXPECT_TRUE(ClassifyText(kFirefox, "firefox.desktop") == Offer::Offered);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nName=X\nExec=\"/opt/x app/x\" %F\n"),
                              "x.desktop") == Offer::Offered);
}

UR_TEST(classifyLeavesOutWhatTheMenuLeavesOut) {
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=x\nNoDisplay=true\n"), "x.desktop") ==
                 Offer::Hidden);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=x\nHidden=true\n"), "x.desktop") ==
                 Offer::Hidden);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Link\nURL=https://site.example\n"), "x.desktop") ==
                 Offer::NotAnApplication);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nDBusActivatable=true\n"), "x.desktop") ==
                 Offer::NotAnApplication);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=x \"unterminated\n"), "x.desktop") ==
                 Offer::InvalidExec);
}

// flatpak run and snap run move the app into a scope of their own, out of the
// slice: the copy would start them inside the tunnel
UR_TEST(classifyLeavesOutFlatpakAndSnapApps) {
  UR_EXPECT_TRUE(
      ClassifyText(Entry("Type=Application\nExec=/usr/bin/flatpak run --branch=stable "
                         "--command=firefox --file-forwarding org.mozilla.firefox @@u %u @@\n"),
                   "org.mozilla.firefox.desktop") == Offer::Sandboxed);
  UR_EXPECT_TRUE(
      ClassifyText(Entry("Type=Application\nExec=firefox %u\nX-Flatpak=org.mozilla.firefox\n"),
                   "org.mozilla.firefox.desktop") == Offer::Sandboxed);
  UR_EXPECT_TRUE(
      ClassifyText(Entry("Type=Application\nExec=env BAMF_DESKTOP_FILE_HINT=/var/lib/snapd/desktop/"
                         "applications/firefox_firefox.desktop /snap/bin/firefox %u\n"),
                   "firefox_firefox.desktop") == Offer::Sandboxed);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=code\nX-SnapInstanceName=code\n"),
                              "code_code.desktop") == Offer::Sandboxed);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=snap run spotify\n"),
                              "spotify.desktop") == Offer::Sandboxed);
}

UR_TEST(classifyLeavesOutTheCopiesAndUrnetworkItself) {
  UR_EXPECT_TRUE(ClassifyText(kFirefoxCopy, "urnetwork-exclude-firefox.desktop") ==
                 Offer::Excluded);
  UR_EXPECT_TRUE(ClassifyText(kFirefoxCopy, "renamed.desktop") == Offer::Excluded);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=urnetwork-exclude firefox\n"),
                              "mine.desktop") == Offer::Excluded);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=urnetwork %u\n"),
                              "com.bringyour.network.desktop") == Offer::Own);
  UR_EXPECT_TRUE(ClassifyText(Entry("Type=Application\nExec=x\n"), "my app.desktop") ==
                 Offer::UnsafeId);
}

UR_TEST(copyFileNameIsThePrefixedDesktopId) {
  UR_EXPECT_TRUE(CopyFileName("firefox.desktop") == "urnetwork-exclude-firefox.desktop");
  UR_EXPECT_TRUE(CopyFileName("org.gnome.Nautilus.desktop") ==
                 "urnetwork-exclude-org.gnome.Nautilus.desktop");
  UR_EXPECT_TRUE(CopyFileName("libreoffice-writer.desktop") ==
                 "urnetwork-exclude-libreoffice-writer.desktop");
  for (const char* id : {"", "firefox", ".desktop", "../firefox.desktop", "kde4/okular.desktop",
                         "my app.desktop", "-x.desktop", "urnetwork-exclude-firefox.desktop"}) {
    if (!CopyFileName(id).empty()) UR_FAIL(std::string("a copy name for ") + id);
  }
  UR_EXPECT_TRUE(CopyFileName(std::string(200, 'a') + ".desktop").empty());
}

// ---- the copy -------------------------------------------------------------------

UR_TEST(copyRunsTheAppAndItsActionsThroughTheLauncher) {
  const auto file = DesktopEntryFile::Parse(kFirefox);
  UR_EXPECT_TRUE(file.has_value());
  if (!file) return;
  const auto copy = MakeCopy(*file, "firefox.desktop", "Firefox (outside VPN)");
  UR_EXPECT_TRUE(copy.has_value());
  if (!copy) return;
  if (*copy != kFirefoxCopy) UR_FAIL("the copy differs from the expected text:\n" + *copy);
}

UR_TEST(copyNeverHandlesMimeTypesOrStartsWithoutTheLauncher) {
  const auto file = DesktopEntryFile::Parse(kFirefox);
  const auto copy = file ? MakeCopy(*file, "firefox.desktop", "Firefox (outside VPN)")
                         : std::nullopt;
  UR_EXPECT_TRUE(copy.has_value());
  if (!copy) return;
  // a copy that handled text/html or http would open every link outside the tunnel
  UR_EXPECT_FALSE(Contains(*copy, "MimeType"));
  UR_EXPECT_FALSE(Contains(*copy, "DBusActivatable"));
  UR_EXPECT_FALSE(Contains(*copy, "Implements"));
  UR_EXPECT_FALSE(Contains(*copy, "TryExec=firefox"));
  UR_EXPECT_FALSE(Contains(*copy, "--safe-mode"));
  const auto copied = DesktopEntryFile::Parse(*copy);
  UR_EXPECT_TRUE(copied.has_value());
  if (!copied) return;
  UR_EXPECT_TRUE(copied->Value(kEntry, "Name") ==
                 std::optional<std::string>("Firefox (outside VPN)"));
  UR_EXPECT_FALSE(copied->Raw(kEntry, "X-GNOME-FullName").has_value());
  for (const auto& line : copied->Lines()) {
    if (line.key != "Exec") continue;
    const auto args = SplitExec(UnescapeValue(line.value));
    if (!args || args->front() != "urnetwork-exclude") {
      UR_FAIL("a command line that skips the launcher: " + line.text);
    }
  }
  // the copy is never offered again, and it names its app
  UR_EXPECT_TRUE(Classify(*copied, "urnetwork-exclude-firefox.desktop") == Offer::Excluded);
  UR_EXPECT_TRUE(CopySource(*copy) == std::optional<std::string>("firefox.desktop"));
}

UR_TEST(copyEscapesItsName) {
  const auto file = DesktopEntryFile::Parse(Entry("Type=Application\nName=X\nExec=x\n"));
  const auto copy = file ? MakeCopy(*file, "x.desktop", " Tab\tName") : std::nullopt;
  UR_EXPECT_TRUE(copy && Contains(*copy, "\nName=\\sTab\\tName\n"));
}

UR_TEST(copyOfAnAppThatIsNotOfferedIsRefused) {
  for (const std::string& text :
       {Entry("Type=Application\nExec=x\nNoDisplay=true\n"),
        Entry("Type=Application\nExec=x\nX-Flatpak=org.example.X\n"),
        std::string(kFirefoxCopy)}) {
    const auto file = DesktopEntryFile::Parse(text);
    if (file && MakeCopy(*file, "x.desktop", "X (outside VPN)")) UR_FAIL("copied: " + text);
  }
}

UR_TEST(copySourceRecognizesOnlyTheCopies) {
  UR_EXPECT_TRUE(CopySource(kFirefoxCopy) == std::optional<std::string>("firefox.desktop"));
  UR_EXPECT_FALSE(CopySource(kFirefox).has_value());
  UR_EXPECT_FALSE(CopySource("not a desktop file").has_value());
  UR_EXPECT_FALSE(CopySource(Entry("X-URnetwork-Exclude-Source=\n")).has_value());
}

// ---- the host -------------------------------------------------------------------

UR_TEST(exclusionNeedsTheUnifiedHierarchyAndTheLauncher) {
  using urnw::exclude::ExclusionAvailable;
  const std::string unified = "0::/user.slice/user-1000.slice/session-2.scope\n";
  UR_EXPECT_TRUE(ExclusionAvailable(unified, true));
  UR_EXPECT_FALSE(ExclusionAvailable(unified, false));
  // hybrid and legacy hosts: the daemon emits no exclusion there
  UR_EXPECT_FALSE(ExclusionAvailable(
      "12:pids:/user.slice\n1:name=systemd:/user.slice\n0::/user.slice\n", true));
  UR_EXPECT_FALSE(ExclusionAvailable("12:pids:/user.slice\n1:name=systemd:/user.slice\n", true));
  UR_EXPECT_FALSE(ExclusionAvailable("", true));
}

// ---- wiring ---------------------------------------------------------------------

UR_TEST(settingsShowsTheRowOnlyWhereTheHostCanExclude) {
  const std::string settings = ReadSource("SettingsPage.cpp");
  UR_EXPECT_TRUE(!settings.empty());
  const size_t gate = settings.find("if (HostCanExcludeApps()) {");
  UR_EXPECT_TRUE(gate != std::string::npos);
  if (gate == std::string::npos) return;
  const std::string row = settings.substr(gate, settings.find("\n  }\n", gate) - gate);
  UR_EXPECT_TRUE(Contains(row, "T_(\"exclude_apps_from_vpn\", \"Exclude apps from the VPN\")"));
  UR_EXPECT_TRUE(Contains(row, "ShowExcludeAppsSheet();"));
  const std::string show = DefinitionBody(settings, "void SettingsPage::ShowExcludeAppsSheet(");
  UR_EXPECT_TRUE(Contains(show, "excludeAppsSheet_->Open();"));

  const std::string sheet = ReadSource("ExcludeAppsSheet.cpp");
  const std::string host = DefinitionBody(sheet, "bool HostCanExcludeApps(");
  UR_EXPECT_TRUE(Contains(host, "std::ifstream f(\"/proc/self/cgroup\");"));
  UR_EXPECT_TRUE(Contains(host, "exclude::ExclusionAvailable(text.str(),"));
  UR_EXPECT_TRUE(Contains(host, "Glib::find_program_in_path(exclude::kLauncher)"));
}

UR_TEST(sheetOffersClassifiedAppsAndRemovesOnlyItsOwnCopies) {
  const std::string sheet = ReadSource("ExcludeAppsSheet.cpp");
  UR_EXPECT_TRUE(!sheet.empty());
  const std::string scan = DefinitionBody(sheet, "void ExcludeAppsSheet::Scan(");
  UR_EXPECT_TRUE(Contains(scan, "exclude::Classify(*file, id) != exclude::Offer::Offered"));
  UR_EXPECT_TRUE(Contains(scan, "exclude::CopySource(*text)"));

  const std::string set = DefinitionBody(sheet, "bool ExcludeAppsSheet::SetExcluded(");
  UR_EXPECT_TRUE(Contains(set, "exclude::MakeCopy(*file, app.id,"));
  UR_EXPECT_TRUE(Contains(set, "T_(\"exclude_app_launcher_name\", \"{} (outside VPN)\")"));
  UR_EXPECT_TRUE(Contains(set, "exclude::CopyFileName(app.id)"));
  // a file is removed only after it proved it is this app's copy
  const size_t check = set.find("exclude::CopySource(*text) == std::optional<std::string>(app.id)");
  const size_t unlink = set.find("g_unlink(app.copyPath.c_str())");
  UR_EXPECT_TRUE(check != std::string::npos && unlink != std::string::npos && check < unlink);
}

UR_TEST(excludeAppsStringsAreInTheCatalog) {
  const std::string catalog = ReadSource("../po/en.po");
  UR_EXPECT_TRUE(!catalog.empty());
  for (const char* entry : {
           "msgctxt \"exclude_apps_from_vpn\"\nmsgid \"Exclude apps from the VPN\"",
           "msgctxt \"exclude_apps_from_vpn_note\"\nmsgid \"Adds a launcher to your app menu that "
           "opens the app outside the VPN.\"",
           "msgctxt \"exclude_app_launcher_name\"\nmsgid \"{} (outside VPN)\"",
           "msgctxt \"exclude_app_launcher_failed\"\nmsgid \"Couldn't change the launcher. Check "
           "that your applications folder can be written.\"",
           "msgctxt \"exclude_apps_launcher_note\"\nmsgid \"Each app you turn on gets a second "
           "launcher in your app menu, with (outside VPN) after its name. An app opened from that "
           "launcher bypasses the VPN and the kill switch. An app that is already running stays in "
           "the VPN until you quit it, and Flatpak and Snap apps are not listed.\"",
           "msgctxt \"search_apps_placeholder\"\nmsgid \"Search apps\"",
           "msgctxt \"no_apps_found\"\nmsgid \"No apps found\"",
           "msgctxt \"manage_apps\"\nmsgid \"Manage apps\"",
       }) {
    if (!Contains(catalog, entry)) UR_FAIL(std::string("po/en.po lacks ") + entry);
  }
}
