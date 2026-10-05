// Settings -> Connections -> Exclude apps from the VPN: the installed apps,
// each with a switch that adds or removes its launcher copy,
// "<App> (outside VPN)", in the user's applications directory. An app opened
// from the copy runs through urnetwork-exclude and leaves outside the tunnel;
// the app's own launcher is not touched. Which apps are offered and what a
// copy says are decided in ExcludeLauncher.hpp; this sheet finds the apps
// (GIO), reads and writes the files, and shows the list.
//
// Only files this sheet wrote are ever removed: a copy is recognized by its
// X-URnetwork-Exclude-Source key (exclude::CopySource), never by its name
// alone. A copy whose app is gone is listed too, switched on, so it can be
// removed.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

#include <gtkmm.h>

namespace urnw {

// Whether this host can exclude apps: the cgroup v2 unified hierarchy alone
// (the daemon's own check on /proc/self/cgroup) and urnetwork-exclude on
// $PATH (the daemon packages install it; a Flatpak sandbox does not see it).
// Settings hides the row otherwise.
bool HostCanExcludeApps();

class ExcludeAppsSheet : public Gtk::Window {
 public:
  explicit ExcludeAppsSheet(Gtk::Window& parent);

  // Re-reads the installed apps and the copies, then presents the sheet.
  void Open();

 private:
  struct App {
    std::string id;          // the app's desktop file id (a gone app's, for an orphan copy)
    std::string name;        // the name the menu shows
    std::string icon;        // a serialized GIcon, or ""
    std::string sourcePath;  // the app's desktop file; "" for an orphan copy
    std::string copyPath;    // the copy, while there is one
  };

  void Scan();
  void Render();
  // Writes or removes the copy; false (and the error line) when the file could
  // not be written or removed.
  bool SetExcluded(App& app, bool excluded);

  Gtk::SearchEntry* search_ = nullptr;
  Gtk::Box* rows_ = nullptr;
  Gtk::Label* error_ = nullptr;
  std::vector<App> apps_;
};

}  // namespace urnw
