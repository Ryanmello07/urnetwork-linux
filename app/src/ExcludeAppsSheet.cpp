// SPDX-License-Identifier: MPL-2.0
#include "ExcludeAppsSheet.hpp"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

#include "ExcludeLauncher.hpp"
#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"
#include "UrTheme.hpp"

namespace urnw {
namespace {

constexpr int kSheetWidth = 480;
constexpr int kSheetHeight = 600;
constexpr int kRowTall = 44;  // the settings row species (SettingsPage kRowTall)
constexpr int kInset = 24;    // the sheets' content margin
constexpr int kIconSize = 24;

// $XDG_DATA_HOME/applications: it takes precedence over the system's
// directories, which is why every copy has a desktop id of its own.
std::string ApplicationsDir() {
  return Glib::build_filename(Glib::get_user_data_dir(), "applications");
}

std::optional<std::string> ReadFile(const std::string& path) {
  gchar* contents = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(path.c_str(), &contents, &length, nullptr)) return std::nullopt;
  std::string text(contents, length);
  g_free(contents);
  return text;
}

Gtk::Label* MakeWrappedLabel(const Glib::ustring& text, const char* cssClass) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->add_css_class(cssClass);
  label->set_xalign(0);
  label->set_wrap(true);
  label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  label->set_hexpand(true);
  return label;
}

}  // namespace

bool HostCanExcludeApps() {
  std::ifstream f("/proc/self/cgroup");
  std::ostringstream text;
  text << f.rdbuf();
  return exclude::ExclusionAvailable(text.str(),
                                     !Glib::find_program_in_path(exclude::kLauncher).empty());
}

ExcludeAppsSheet::ExcludeAppsSheet(Gtk::Window& parent) {
  EnsureBrandCss();   // the pane-row vocabulary the list is built from
  EnsureDrawerCss();  // .ur-caption / .ur-error-text
  set_transient_for(parent);
  set_modal(true);
  set_title(T_("exclude_apps_from_vpn", "Exclude apps from the VPN"));
  set_default_size(kSheetWidth, kSheetHeight);
  set_hide_on_close(true);
  add_css_class("ur-sheet");
  AddEscapeToClose(*this);

  auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroller->set_vexpand(true);
  auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

  auto* intro = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  intro->set_margin_start(kInset);
  intro->set_margin_end(kInset);
  intro->set_margin_top(kInset);
  intro->set_margin_bottom(16);
  intro->append(*MakeWrappedLabel(T_("exclude_apps_from_vpn", "Exclude apps from the VPN"),
                                  "ur-step-heading"));
  intro->append(*MakeWrappedLabel(
      T_("exclude_apps_launcher_note",
         "Each app you turn on gets a second launcher in your app menu, with (outside VPN) "
         "after its name. An app opened from that launcher bypasses the VPN and the kill "
         "switch. An app that is already running stays in the VPN until you quit it, and "
         "Flatpak and Snap apps are not listed."),
      "ur-caption"));

  search_ = Gtk::make_managed<Gtk::SearchEntry>();
  search_->set_placeholder_text(T_("search_apps_placeholder", "Search apps"));
  // a placeholder is not an accessible name
  kit::SetAccessibleLabel(*search_, T_("search_apps_placeholder", "Search apps"));
  search_->signal_search_changed().connect([this] { Render(); });
  intro->append(*search_);

  error_ = MakeWrappedLabel(
      T_("exclude_app_launcher_failed",
         "Couldn't change the launcher. Check that your applications folder can be written."),
      "ur-caption");
  error_->add_css_class("ur-error-text");
  error_->set_visible(false);
  intro->append(*error_);
  column->append(*intro);

  rows_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  column->append(*rows_);

  scroller->set_child(*column);
  set_child(*scroller);
}

void ExcludeAppsSheet::Open() {
  error_->set_visible(false);
  search_->set_text("");
  Scan();
  Render();
  present();
}

void ExcludeAppsSheet::Scan() {
  apps_.clear();

  // The copies already written, by the app each was made for. A file is one
  // of ours only when it names its app (exclude::CopySource); anything else in
  // the directory is never listed and never removed.
  std::map<std::string, App> copies;
  const std::string dir = ApplicationsDir();
  if (GDir* entries = g_dir_open(dir.c_str(), 0, nullptr)) {
    while (const gchar* entry = g_dir_read_name(entries)) {
      const std::string fileName = entry;
      if (fileName.rfind(exclude::kCopyIdPrefix, 0) != 0) continue;
      const std::string path = Glib::build_filename(dir, fileName);
      const auto text = ReadFile(path);
      const auto source = text ? exclude::CopySource(*text) : std::nullopt;
      if (!source) continue;
      App copy;
      copy.id = *source;
      copy.copyPath = path;
      copy.name = *source;
      if (const auto file = exclude::DesktopEntryFile::Parse(*text)) {
        copy.name = file->Value(exclude::kDesktopEntryGroup, "Name").value_or(*source);
        copy.icon = file->Value(exclude::kDesktopEntryGroup, "Icon").value_or("");
      }
      copies[copy.id] = std::move(copy);
    }
    g_dir_close(entries);
  }

  // The installed apps the menu shows, and of those the ones that can be
  // excluded (exclude::Classify).
  GList* infos = g_app_info_get_all();
  for (GList* item = infos; item != nullptr; item = item->next) {
    GAppInfo* info = G_APP_INFO(item->data);
    if (!G_IS_DESKTOP_APP_INFO(info) || !g_app_info_should_show(info)) continue;
    const char* id = g_app_info_get_id(info);
    const char* path = g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(info));
    const char* name = g_app_info_get_display_name(info);
    if (id == nullptr || path == nullptr || name == nullptr) continue;
    const auto text = ReadFile(path);
    const auto file = text ? exclude::DesktopEntryFile::Parse(*text) : std::nullopt;
    if (!file || exclude::Classify(*file, id) != exclude::Offer::Offered) continue;
    App app;
    app.id = id;
    app.name = name;
    app.sourcePath = path;
    if (GIcon* icon = g_app_info_get_icon(info)) {
      if (gchar* serialized = g_icon_to_string(icon)) {
        app.icon = serialized;
        g_free(serialized);
      }
    }
    if (const auto copy = copies.find(app.id); copy != copies.end()) {
      app.copyPath = copy->second.copyPath;
      copies.erase(copy);
    }
    apps_.push_back(std::move(app));
  }
  g_list_free_full(infos, g_object_unref);

  // A copy whose app is no longer offered (uninstalled, or now a Flatpak) is
  // listed under its own name, switched on, so it can be removed.
  for (auto& entry : copies) apps_.push_back(std::move(entry.second));

  std::sort(apps_.begin(), apps_.end(), [](const App& a, const App& b) {
    return Glib::ustring(a.name).casefold_collate_key() <
           Glib::ustring(b.name).casefold_collate_key();
  });
}

// The apps already excluded are pinned on top under Excluded, the rest follow
// under Apps, each in name order; a group's header shows only while the
// group has a row to head. The grouping is read off each app's copy as it
// stands, so a switch turned on moves its row at the next search or open,
// never under the pointer. The search matches the name the menu shows or the
// desktop id.
void ExcludeAppsSheet::Render() {
  RemoveAllChildren(*rows_);
  const Glib::ustring query = search_->get_text().casefold();
  auto matches = [&query](const App& app) {
    return query.empty() ||
           Glib::ustring(app.name).casefold().find(query) != Glib::ustring::npos ||
           Glib::ustring(app.id).casefold().find(query) != Glib::ustring::npos;
  };
  int shown = 0;
  for (const bool excludedGroup : {true, false}) {
    bool headed = false;
    for (size_t index = 0; index < apps_.size(); ++index) {
      const App& app = apps_[index];
      if (app.copyPath.empty() == excludedGroup || !matches(app)) continue;
      if (!headed) {
        rows_->append(*kit::MakePaneGroupHeader(excludedGroup ? T_("excluded", "Excluded")
                                                              : T_("apps", "Apps"))
                           .root);
        headed = true;
      }
      RenderRow(index);
      ++shown;
    }
  }
  if (shown == 0) rows_->append(*kit::MakePaneEmptyLine(T_("no_apps_found", "No apps found")));
}

void ExcludeAppsSheet::RenderRow(size_t index) {
  const App& app = apps_[index];
  auto row = kit::MakePaneTwoLineRow(app.name, {}, kRowTall);
  if (auto* inner = dynamic_cast<Gtk::Box*>(row.root->get_first_child())) {
    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_pixel_size(kIconSize);
    icon->set_valign(Gtk::Align::CENTER);
    GIcon* gicon = app.icon.empty() ? nullptr : g_icon_new_for_string(app.icon.c_str(), nullptr);
    if (gicon != nullptr) {
      gtk_image_set_from_gicon(icon->gobj(), gicon);
      g_object_unref(gicon);
    } else {
      icon->set_from_icon_name("application-x-executable");
    }
    kit::MarkDecorative(*icon);
    inner->prepend(*icon);
  }

  auto* toggle = Gtk::make_managed<Gtk::Switch>();
  toggle->set_valign(Gtk::Align::CENTER);
  toggle->set_active(!app.copyPath.empty());
  kit::SetAccessibleLabel(*toggle, app.name);
  toggle->property_active().signal_changed().connect([this, index, toggle] {
    App& changed = apps_[index];
    const bool excluded = toggle->get_active();
    if (excluded == !changed.copyPath.empty()) return;  // the switch put back below
    if (!SetExcluded(changed, excluded)) {
      toggle->set_active(!excluded);  // the switch says where the file is
      return;
    }
    // a removed orphan copy cannot be written again: its app is gone
    if (changed.sourcePath.empty()) toggle->set_sensitive(false);
  });
  row.trailing->append(*toggle);
  rows_->append(*row.root);
}

bool ExcludeAppsSheet::SetExcluded(App& app, bool excluded) {
  error_->set_visible(false);
  bool done = false;
  if (excluded) {
    // the copy of the app's entry, named in the user's language
    const auto text = app.sourcePath.empty() ? std::nullopt : ReadFile(app.sourcePath);
    const auto file = text ? exclude::DesktopEntryFile::Parse(*text) : std::nullopt;
    const auto copy =
        file ? exclude::MakeCopy(*file, app.id,
                                 Format(T_("exclude_app_launcher_name", "{} (outside VPN)"),
                                        app.name))
             : std::nullopt;
    const std::string fileName = exclude::CopyFileName(app.id);
    const std::string dir = ApplicationsDir();
    if (copy && !fileName.empty() && g_mkdir_with_parents(dir.c_str(), 0755) == 0) {
      const std::string path = Glib::build_filename(dir, fileName);
      GError* error = nullptr;
      if (g_file_set_contents(path.c_str(), copy->data(), static_cast<gssize>(copy->size()),
                              &error)) {
        app.copyPath = path;
        done = true;
      } else {
        g_warning("exclude apps: writing %s failed: %s", path.c_str(), error->message);
        g_clear_error(&error);
      }
    }
  } else {
    // only a file this sheet wrote for this app is removed
    const auto text = ReadFile(app.copyPath);
    if (!text && !g_file_test(app.copyPath.c_str(), G_FILE_TEST_EXISTS)) {
      app.copyPath.clear();  // already gone
      done = true;
    } else if (text && exclude::CopySource(*text) == std::optional<std::string>(app.id)) {
      if (g_unlink(app.copyPath.c_str()) == 0 || errno == ENOENT) {
        app.copyPath.clear();
        done = true;
      } else {
        g_warning("exclude apps: removing %s failed: %s", app.copyPath.c_str(),
                  std::strerror(errno));
      }
    }
  }
  error_->set_visible(!done);
  return done;
}

}  // namespace urnw
