// SPDX-License-Identifier: MPL-2.0
#include "HomeShell.hpp"

#include "I18n.hpp"
#include "ShellLayout.hpp"
#include "UrMotion.hpp"

namespace urnw {
namespace {
constexpr int kNavExpandedWidth = 220;  // windows OpenPaneLength
constexpr int kNavCompactWidth = 48;    // the compact rail
// accent bar to icon to label, and accent bar to icon in the compact rail
constexpr int kNavExpandedGap = 10;
constexpr int kNavCompactGap = 6;
// the Advanced fields' fade on a mode flip (Windows' 180 in, 120 out)
constexpr int kAdvancedFadeInMs = 180;
constexpr int kAdvancedFadeOutMs = 120;
}  // namespace

HomeShell::HomeShell() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
  // ---- the left rail --------------------------------------------------------
  navRail_.add_css_class("ur-nav");
  navRail_.set_size_request(kNavExpandedWidth, -1);
  navRail_.set_vexpand(true);

  navPrimary_.set_margin_top(8);
  navPrimary_.set_vexpand(true);
  navRail_.append(navPrimary_);
  navFooter_.set_margin_bottom(8);
  navRail_.append(navFooter_);

  // primary items, in the windows order (tag -> store key -> Segoe Fluent
  // glyph the drawn NavIcon reproduces: E80F Home, E774 Globe, E8C7 Wallet,
  // E77B Contact; footer E897 Help, EBE8 DeveloperTools, E713 Settings)
  MakeNavItem(navPrimary_, "connect", NavIcon::Kind::Home, T_("connect", "Connect"));
  MakeNavItem(navPrimary_, "network", NavIcon::Kind::Globe, T_("network", "Network"));
  MakeNavItem(navPrimary_, "earnings", NavIcon::Kind::Wallet, T_("earnings", "Earnings"));
  MakeNavItem(navPrimary_, "account", NavIcon::Kind::Person, T_("account", "Account"));
  // footer: support, [developer — inserted by Advanced Mode], settings
  MakeNavItem(navFooter_, "support", NavIcon::Kind::Help, T_("support", "Support"));
  MakeNavItem(navFooter_, "settings", NavIcon::Kind::Gear, T_("settings", "Settings"));

  body_.append(navRail_);

  // ---- the content column ---------------------------------------------------
  // the standing session-mode notice (never closable; persists across
  // destinations); empty = hidden
  modeNotice_.add_css_class("ur-mode-notice");
  modeNotice_.set_xalign(0);
  modeNotice_.set_wrap(true);
  modeNotice_.set_visible(false);
  contentColumn_.append(modeNotice_);

  // Sized by the page on screen, not by the widest of the seven: a homogeneous
  // stack carried the Account page's wide-layout minimum (825) under every
  // destination, so a window opened at its default width could not be
  // narrowed below 1045, and no page reached a fold below that.
  stack_.set_hhomogeneous(false);
  stack_.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
  stack_.set_transition_duration(motion::kBaseMs);  // the page-swap default
  stack_.set_vexpand(true);
  stack_.set_hexpand(true);
  contentColumn_.append(stack_);

  // ---- the status strip -----------------------------------------------------
  statusStrip_.add_css_class("ur-status-strip");
  // the dot and the word name themselves; a screen reader hears the name
  stateField_ =
      kit::MakeStatusField("", /*withDot=*/true, T_("urnetwork_status", "URnetwork Status"));
  statusStrip_.append(*stateField_.root);
  statusStrip_.append(*kit::MakeStatusSeparator());
  providerField_ =
      kit::MakeStatusField(T_("selected_provider", "Selected provider"), false);
  statusStrip_.append(*providerField_.root);
  trafficSeparator_ = kit::MakeStatusSeparator();
  statusStrip_.append(*trafficSeparator_);
  trafficField_ = kit::MakeStatusField(T_("data", "Data"), false);
  statusStrip_.append(*trafficField_.root);

  // the 5 Advanced fields ride the same row and drop entirely with the mode,
  // and each one after its own separator, which drops with it on a narrow
  // window (ApplyStripLayout)
  const auto appendAdvanced = [this](kit::StatusField& field, Gtk::Widget*& separator) {
    separator = kit::MakeStatusSeparator();
    advancedFields_.append(*separator);
    advancedFields_.append(*field.root);
  };
  networkField_ = kit::MakeStatusField(T_("network", "Network"), false);
  appendAdvanced(networkField_, networkSeparator_);
  sessionField_ = kit::MakeStatusField(T_("adv_session_mode", "Session"), false);
  appendAdvanced(sessionField_, sessionSeparator_);
  routesField_ = kit::MakeStatusField(T_("adv_routes", "Routes"), false);
  appendAdvanced(routesField_, routesSeparator_);
  rpcField_ = kit::MakeStatusField(T_("adv_rpc", "RPC"), false);
  appendAdvanced(rpcField_, rpcSeparator_);
  rawField_ = kit::MakeStatusField(T_("adv_raw_status", "Raw status"), false);
  appendAdvanced(rawField_, rawSeparator_);
  // ...closed by a standing tag naming the mode, as Windows has it: the mode
  // changes what half the app's surfaces mean, so the chrome says which
  // reading it is in. Caption-less (the word is the fact), in the action
  // blue chrome wears, and it drops with the fields it closes.
  advancedFields_.append(*kit::MakeStatusSeparator());
  modeField_ = kit::MakeStatusField({}, false, T_("adv_advanced_mode", "Advanced mode"));
  kit::SetStatusFieldValue(modeField_, T_("advanced", "Advanced"));
  modeField_.value->add_css_class("ur-status-mode");
  advancedFields_.append(*modeField_.root);
  advancedFields_.set_visible(false);
  statusStrip_.append(advancedFields_);

  // snackbar overlays the content bottom-center (windows AccountSnackbar,
  // MaxWidth 480)
  contentOverlay_.set_child(contentColumn_);
  contentOverlay_.add_overlay(snackbar_.root());
  contentOverlay_.set_hexpand(true);
  body_.append(contentOverlay_);
  body_.set_vexpand(true);
  append(body_);
  // The strip is the last row and spans the window, under the rail and the
  // content, as Windows' root grid has it: its room is the window's width.
  append(statusStrip_);

  PaintSelection();
}

HomeShell::NavItem* HomeShell::MakeNavItem(Gtk::Box& parent, const std::string& tag,
                                           NavIcon::Kind icon, const Glib::ustring& label) {
  NavItem item;
  item.tag = tag;
  item.button = Gtk::make_managed<Gtk::Button>();
  item.button->add_css_class("ur-nav-item");
  auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, kNavExpandedGap);
  item.row = row;
  item.accent = Gtk::make_managed<Gtk::Box>();
  item.accent->add_css_class("ur-nav-accent");
  item.accent->set_size_request(3, 16);
  item.accent->set_valign(Gtk::Align::CENTER);
  item.accent->set_opacity(0);
  kit::MarkDecorative(*item.accent);
  row->append(*item.accent);
  // drawn, not themed: two of the windows glyphs (globe, wallet) do not exist
  // in Adwaita, and a missing themed icon renders BLANK
  auto* image = Gtk::make_managed<NavIcon>(icon, 16);
  kit::MarkDecorative(*image);
  row->append(*image);
  item.label = Gtk::make_managed<Gtk::Label>(label);
  item.label->set_xalign(0);
  row->append(*item.label);
  item.button->set_child(*row);
  kit::SetAccessibleLabel(*item.button, label);
  item.button->signal_clicked().connect([this, tag] { Navigate(tag); });
  parent.append(*item.button);
  ApplyCompact(item);
  items_.push_back(item);
  return &items_.back();
}

void HomeShell::SetPage(const std::string& tag, Gtk::Widget& page) {
  stack_.add(page, tag);
}

void HomeShell::Navigate(const std::string& tag) {
  if (!stack_.get_child_by_name(tag)) return;
  currentTag_ = tag;
  stack_.set_visible_child(tag);
  PaintSelection();
  if (on_navigate) on_navigate(tag);
}

void HomeShell::PaintSelection() {
  // pages reached FROM a destination (no rail item of their own) keep that
  // destination's item lit: "referrals" belongs to Account
  const std::string railTag = currentTag_ == "referrals" ? "account" : currentTag_;
  for (auto& item : items_) {
    const bool selected = (item.tag == railTag);
    if (selected) {
      item.button->add_css_class("selected");
    } else {
      item.button->remove_css_class("selected");
    }
    if (item.accent) item.accent->set_opacity(selected ? 1.0 : 0.0);
  }
}

void HomeShell::SetAdvancedMode(bool on) {
  if (advanced_ == on) return;
  advanced_ = on;
  FadeAdvancedFields(shell::StatusStripLayoutFor(widthDip_, on).advancedRow);
  ApplyStripLayout();
  if (on && !developerItem_) {
    // INSERTED into the footer collection ahead of settings, not un-hidden
    auto* settingsButton = items_.empty() ? nullptr : items_.back().button;
    developerItem_ = MakeNavItem(navFooter_, "developer", NavIcon::Kind::DevTools,
                                 T_("developer", "Developer"));
    if (settingsButton) navFooter_.reorder_child_after(*settingsButton,
                                                       *developerItem_->button);
    // NOTE: items_ may have reallocated; repaint from tags, not stale pointers
    developerItem_ = &items_.back();
    PaintSelection();
  } else if (!on && developerItem_) {
    // the user standing on it is navigated away, then the item is REMOVED
    if (currentTag_ == "developer") Navigate("settings");
    navFooter_.remove(*developerItem_->button);
    for (auto it = items_.begin(); it != items_.end(); ++it) {
      if (it->tag == "developer") {
        items_.erase(it);
        break;
      }
    }
    developerItem_ = nullptr;
  }
}

void HomeShell::FadeAdvancedFields(bool show) {
  // a later flip cancels this one's frames and its hide
  const uint64_t generation = ++advancedFade_;
  advancedFields_.set_opacity(1.0);
  if (!show && !advancedFields_.get_visible()) return;  // nothing on screen to fade
  // a hard cut when animations are off, or with the strip not on screen
  // (the mode read at launch), where no frame would run the fade
  if (!motion::ShouldAnimate() || !statusStrip_.get_mapped()) {
    advancedFields_.set_visible(show);
    return;
  }
  if (show) {
    advancedFields_.set_opacity(0.0);
    advancedFields_.set_visible(true);
    motion::AnimateValue(advancedFields_, 0, kAdvancedFadeInMs, motion::kStandardP1,
                         motion::kStandardP2, [this, generation](double eased) {
                           if (generation == advancedFade_) advancedFields_.set_opacity(eased);
                         });
    return;
  }
  motion::AnimateValue(
      advancedFields_, 0, kAdvancedFadeOutMs, motion::kExitP1, motion::kExitP2,
      [this, generation](double eased) {
        if (generation == advancedFade_) advancedFields_.set_opacity(1.0 - eased);
      },
      [this, generation] {
        if (generation != advancedFade_) return;
        advancedFields_.set_visible(false);
        advancedFields_.set_opacity(1.0);
      });
}

void HomeShell::ApplyBreakpoint(int windowWidthDip) {
  SetCompactNav(shell::NavRailCompact(windowWidthDip));
  widthDip_ = windowWidthDip;
  // a resize applies at once: a fade still running for a flip is cut short
  ++advancedFade_;
  advancedFields_.set_opacity(1.0);
  advancedFields_.set_visible(shell::StatusStripLayoutFor(widthDip_, advanced_).advancedRow);
  ApplyStripLayout();
}

void HomeShell::ApplyStripLayout() {
  const shell::StatusStripLayout layout = shell::StatusStripLayoutFor(widthDip_, advanced_);
  // the Normal fields' captions; the Advanced fields keep theirs
  for (kit::StatusField* field : {&providerField_, &trafficField_}) {
    if (field->caption) field->caption->set_visible(layout.captions);
  }
  trafficSeparator_->set_visible(layout.traffic);
  trafficField_.root->set_visible(layout.traffic);
  // The Advanced fields the row has room for, each with its separator; the
  // tag closes whatever shows. Off the mode the row hides whole, fading, so
  // its fields are left as they were rather than cut from under the fade.
  if (!advanced_) return;
  const auto showAdvanced = [](Gtk::Widget* separator, kit::StatusField& field, bool show) {
    separator->set_visible(show);
    field.root->set_visible(show);
  };
  showAdvanced(networkSeparator_, networkField_, layout.sessionFields);
  showAdvanced(sessionSeparator_, sessionField_, layout.sessionFields);
  showAdvanced(routesSeparator_, routesField_, layout.sessionFields);
  showAdvanced(rpcSeparator_, rpcField_, layout.rpc);
  showAdvanced(rawSeparator_, rawField_, layout.raw);
}

void HomeShell::SetCompactNav(bool compact) {
  if (compact_ == compact) return;
  compact_ = compact;
  navRail_.set_size_request(compact ? kNavCompactWidth : kNavExpandedWidth, -1);
  if (compact) {
    navRail_.add_css_class("compact");
  } else {
    navRail_.remove_css_class("compact");
  }
  for (auto& item : items_) ApplyCompact(item);
}

void HomeShell::ApplyCompact(NavItem& item) {
  // the icon alone names nothing: the label moves into the tooltip, and the
  // accessible label (set at build) stays
  if (item.label) item.label->set_visible(!compact_);
  if (item.row) item.row->set_spacing(compact_ ? kNavCompactGap : kNavExpandedGap);
  if (compact_ && item.label) {
    item.button->set_tooltip_text(item.label->get_text());
  } else {
    item.button->set_has_tooltip(false);
  }
}

void HomeShell::SetModeNotice(const Glib::ustring& message) {
  modeNotice_.set_text(message);
  modeNotice_.set_visible(!message.empty());
}

void HomeShell::SetStatusState(const Glib::ustring& word, const std::string& dotHex) {
  kit::SetStatusFieldValue(stateField_, word);
  kit::SetStatusFieldDot(stateField_, dotHex);
}
void HomeShell::SetStatusProvider(const Glib::ustring& provider) {
  kit::SetStatusFieldValue(providerField_, provider);
}
void HomeShell::SetStatusTraffic(const Glib::ustring& traffic) {
  kit::SetStatusFieldValue(trafficField_, traffic);
}
void HomeShell::SetStatusNetwork(const Glib::ustring& network) {
  kit::SetStatusFieldValue(networkField_, network);
}
void HomeShell::SetStatusSession(const Glib::ustring& session) {
  kit::SetStatusFieldValue(sessionField_, session);
}
void HomeShell::SetStatusRoutes(const Glib::ustring& routes) {
  kit::SetStatusFieldValue(routesField_, routes);
}
void HomeShell::SetStatusRpc(const Glib::ustring& rpc) {
  kit::SetStatusFieldValue(rpcField_, rpc);
}
void HomeShell::SetStatusRaw(const Glib::ustring& raw) {
  kit::SetStatusFieldValue(rawField_, raw);
}

}  // namespace urnw
