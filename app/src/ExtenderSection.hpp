// The Extenders section of the Account destination (connect/EXTENDER.md K6):
// the network space's extender settings, the bootstrap DNS-over-HTTPS servers
// (ControlDohSection), the legacy private extender behind an Advanced
// expander, and the share / import entry points.
//
// Three values are edited through the SDK's ExtenderViewController, which owns
// the write and restarts the space's network client and node in place: the
// extender DNS name, the gossip URL, and the manual host list (one hostname or
// IP per line, unioned with DNS bootstrap and everything the feed and the mesh
// deliver). An EMPTY field means the derived default, and the default is shown
// as the field's placeholder.
//
// A SECTION INSIDE THE ACCOUNT PANE, not a fourth pane. AccountPage's fold
// table hides its optional panes below 1500 dip (and the plan pane below 900),
// so settings living in a fourth column would be unreachable on an ordinary
// laptop. EXTENDER.md K6 asks for "a section named Extenders" under account,
// and the account pane is the one that is always on screen.
//
// The private extender is the exception to "the SDK owns the write": it has no
// view-controller surface, so it goes onto the network space values directly
// (SdkHost::SetPrivateExtender), with the daemon-split caveat documented there.
//
// Reset extenders (connect EXTENDER.md E7) sits with share and import. The page
// confirms it, then the section runs it (SdkHost::ResetExtenders): this
// process's space and the daemon's go back to a fresh install's extender state,
// and the form shows what that leaves. It needs no device.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <gtkmm.h>

#include "ControlDohSection.hpp"
#include "ExtenderSharePresentation.hpp"
#include "PaneKit.hpp"
#include "SdkHost.hpp"

namespace urnw {

class ExtenderSection : public Gtk::Box {
 public:
  explicit ExtenderSection(SdkHost& host);
  ~ExtenderSection() override;

  // (Re)read the settings from the SDK. Called on the page's Load() and on
  // DrawerEvent::DeviceLifecycle -- the controller lives only while a device
  // does, so "the tunnel just came up" is exactly when this stops being the
  // no-device state.
  void Load();

  // The page's snackbar, and its one-modal-at-a-time gate: this section owns
  // no window, so the two sheets and the reset's confirmation are opened by
  // the page.
  std::function<void(const Glib::ustring& message, bool error)> on_snackbar;
  std::function<void()> on_share;
  std::function<void()> on_import;
  std::function<void()> on_reset;

  // Runs a reset the page's confirmation accepted: Reset extenders goes
  // insensitive until SdkHost::ResetExtenders reports, then the form is read
  // again and says "Extenders reset".
  void ResetExtenders();

 private:
  void BuildForm(Gtk::Box& host);
  void BuildControlDoh(Gtk::Box& host);
  void BuildAdvanced(Gtk::Box& host);
  void BuildActions(Gtk::Box& host);
  void ApplySettings(const std::optional<urnet::ExtenderSettings>& settings);
  void ApplyEnabled();
  void Save();
  void SavePrivateExtender();
  // The form after a reset reported: everything read again, and with no
  // device's view controller to read the settings, the fields a reset leaves.
  void FinishReset(const SdkHost::ExtenderResetOutcome& outcome);
  void Snack(const Glib::ustring& message, bool error);

  SdkHost& host_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  Gtk::Entry* dnsName_ = nullptr;
  Gtk::Entry* gossipUrl_ = nullptr;
  Gtk::TextView* hosts_ = nullptr;
  Gtk::Button* save_ = nullptr;
  Gtk::Label* status_ = nullptr;
  ControlDohSection* controlDoh_ = nullptr;
  Gtk::Entry* privateIp_ = nullptr;
  Gtk::Entry* privateSecret_ = nullptr;
  Gtk::Button* savePrivate_ = nullptr;
  Gtk::Button* share_ = nullptr;
  Gtk::Button* import_ = nullptr;
  Gtk::Button* reset_ = nullptr;
  bool resetting_ = false;  // a reset is in flight

  // the last defaults the SDK reported, carried forward so an overridden field
  // still shows what it is overriding
  extender::SettingsField dnsField_;
  extender::SettingsField gossipField_;
  bool haveSettings_ = false;
};

}  // namespace urnw
