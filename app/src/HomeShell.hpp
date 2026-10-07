// The signed-in window shell — the GTK port of the windows NavigationView
// home (docs/linux_agent_help.md §7.4/§7.5): a left nav rail (#151515, 44px
// items, selected = card-fill pill + 3px #EFF7BB accent bar), a content stack
// of sibling destination pages toggled by visibility (no navigation frame —
// page swaps ride CrossfadePageSwap), the window-level ModeNoticeBar, and the
// persistent bottom status strip under the rail and the content (normal 3
// fields; Advanced Mode adds Network / Session / Routes / RPC / Raw and an
// Advanced tag to the same row).
//
// The shell owns navigation + chrome ONLY. Destination pages are registered
// with SetPage and load their data through the on_navigate hook (the windows
// tag->load mapping lives in MainWindow, which owns the SdkHost wiring).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "BrandIcons.hpp"
#include "PaneKit.hpp"

namespace urnw {

class HomeShell : public Gtk::Box {
 public:
  HomeShell();

  // Register a destination page under its tag ("connect", "network",
  // "earnings", "account", "support", "developer", "settings").
  void SetPage(const std::string& tag, Gtk::Widget& page);
  // Navigate (nav click or programmatic): crossfades the stack, paints the
  // rail selection, fires on_navigate for the per-destination API loads.
  void Navigate(const std::string& tag);
  const std::string& CurrentTag() const { return currentTag_; }

  // The Advanced-only Developer item is INSERTED/REMOVED, not hidden; a user
  // standing on it when the mode turns off is navigated away (to settings).
  // The strip's Advanced fields fade in and out with the flip.
  void SetAdvancedMode(bool on);
  // expanded rail (icon+label, 220) vs compact rail (icons only, 48, each
  // named by its tooltip); ApplyBreakpoint picks it from the width
  void SetCompactNav(bool compact);
  // The window's width (MainWindow::ApplyPageBreakpoint): the rail's mode and
  // what the status strip has room for (ShellLayout.hpp).
  void ApplyBreakpoint(int windowWidthDip);

  // ---- window-level chrome ------------------------------------------------
  // The standing "this session carries no traffic" notice; empty hides it.
  void SetModeNotice(const Glib::ustring& message);
  kit::Snackbar& snackbar() { return snackbar_; }

  // ---- the status strip ---------------------------------------------------
  void SetStatusState(const Glib::ustring& word, const std::string& dotHex);
  void SetStatusProvider(const Glib::ustring& provider);
  void SetStatusTraffic(const Glib::ustring& traffic);
  // the 5 Advanced fields (hidden while Advanced Mode is off)
  void SetStatusNetwork(const Glib::ustring& network);
  void SetStatusSession(const Glib::ustring& session);
  void SetStatusRoutes(const Glib::ustring& routes);
  void SetStatusRpc(const Glib::ustring& rpc);
  void SetStatusRaw(const Glib::ustring& raw);

  std::function<void(const std::string& tag)> on_navigate;

 private:
  struct NavItem {
    std::string tag;
    Gtk::Button* button = nullptr;
    Gtk::Box* row = nullptr;      // accent, icon, label
    Gtk::Box* accent = nullptr;   // the 3px selection bar
    Gtk::Label* label = nullptr;  // hidden in compact mode
  };
  NavItem* MakeNavItem(Gtk::Box& parent, const std::string& tag, NavIcon::Kind icon,
                       const Glib::ustring& label);
  void PaintSelection();
  // one item in the rail's current mode: the label, or the tooltip in its place
  void ApplyCompact(NavItem& item);
  // shows or hides the strip's Advanced fields, faded when animations are on
  void FadeAdvancedFields(bool show);
  // the strip's captions, traffic field and Advanced fields for the width
  // and the mode
  void ApplyStripLayout();

  Gtk::Box body_{Gtk::Orientation::HORIZONTAL, 0};  // the rail and the content
  Gtk::Box navRail_{Gtk::Orientation::VERTICAL, 0};
  Gtk::Box navPrimary_{Gtk::Orientation::VERTICAL, 2};
  Gtk::Box navFooter_{Gtk::Orientation::VERTICAL, 2};
  std::vector<NavItem> items_;
  NavItem* developerItem_ = nullptr;  // present only in Advanced Mode
  bool advanced_ = false;
  bool compact_ = false;

  Gtk::Overlay contentOverlay_;  // stack + snackbar overlay
  Gtk::Box contentColumn_{Gtk::Orientation::VERTICAL, 0};
  Gtk::Label modeNotice_;
  Gtk::Stack stack_;
  std::string currentTag_ = "connect";

  // status strip fields
  Gtk::Box statusStrip_{Gtk::Orientation::HORIZONTAL, 0};
  kit::StatusField stateField_;
  kit::StatusField providerField_;
  Gtk::Widget* trafficSeparator_ = nullptr;  // drops with the traffic field
  kit::StatusField trafficField_;
  Gtk::Box advancedFields_{Gtk::Orientation::HORIZONTAL, 0};
  kit::StatusField networkField_;
  kit::StatusField sessionField_;
  kit::StatusField routesField_;
  kit::StatusField rpcField_;
  kit::StatusField rawField_;
  // each Advanced field's leading separator, which drops with it
  Gtk::Widget* networkSeparator_ = nullptr;
  Gtk::Widget* sessionSeparator_ = nullptr;
  Gtk::Widget* routesSeparator_ = nullptr;
  Gtk::Widget* rpcSeparator_ = nullptr;
  Gtk::Widget* rawSeparator_ = nullptr;
  kit::StatusField modeField_;  // the "Advanced" tag closing the row
  uint64_t advancedFade_ = 0;   // the flip whose fade may still write
  // the window's width; until the first breakpoint nothing is dropped
  int widthDip_ = std::numeric_limits<int>::max();

  kit::Snackbar snackbar_;
};

}  // namespace urnw
