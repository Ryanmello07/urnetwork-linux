// The service warning card — the GTK port of the Windows ServiceSetupBar
// (windows:app/src/App/ConnectPage.cpp ApplyServiceSetup, around line 400) and
// its wording (svc_setup_*, svc_start_*, svc_update_*).
//
// SELF-CONTAINED BY DESIGN. ConnectPage is 135 KB and is owned by another
// workstream; this card must be droppable into it — or into Settings, or into
// the developer screen — WITHOUT that page learning anything about release
// channels, polkit or sha256. So the whole surface is ONE call:
//
//     urnw::ServiceNotice::Attach(*someBox);
//
// which builds the card, inserts it, subscribes it to ServiceSetup::Instance(),
// replays the current snapshot onto it, and kicks a refresh. It returns the
// widget for callers that want it, and returning it is optional — the card
// owns its own visibility (hidden while the service is healthy, which costs a
// GTK box exactly nothing), its own busy/error states and its own teardown.
//
// WHAT IT RENDERS, following the Windows bar's discipline exactly:
//   * the TITLE says what the card is for and never changes mid-action;
//   * the MESSAGE is the pitch, REPLACED (not appended to) by the in-flight
//     stage line, the cancelled line or the failure line;
//   * ONE primary action, disabled while busy, whose label is the state's verb
//     — Set up / Start / Update are three wordings of one idempotent action;
//   * a progress line that always settles, because every ServiceSetup failure
//     is a distinct terminal state with its own sentence;
//   * the no-permission fallback: the exact one-line command, copyable, shown
//     on demand and forced open for the failures that mean "the app cannot do
//     this for you here" (no pkexec, no host spawn, no tar).
//
// STRINGS. The key ids are the Windows ones so the two clients stay one
// product, but the ENGLISH is Linux's: there is no "Windows service" and no
// "administrator permission" here — there is a system service and a polkit
// prompt. Every id below is still absent from the localization store (the
// Windows twin marks the same ids Adv(), i.e. awaiting a store key), so the
// T_() lookups fall through to the English written here until
// localizations/keys/*.yaml carries them. See the wiring notes.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <memory>
#include <string>

#include <gtkmm.h>

#include "ServiceSetup.hpp"

namespace urnw {

class ServiceNotice : public Gtk::Box {
 public:
  ServiceNotice();
  ~ServiceNotice() override;

  // THE ONE CALL. Builds a managed card, inserts it into `host` (at the top by
  // default — a warning belongs above the thing it warns about), subscribes it
  // to ServiceSetup::Instance(), replays Current() so a card built after the
  // first probe is never blank, and requests a refresh.
  //
  // Safe to call before any daemon facts provider is wired: the classifier
  // then works from the control socket and systemd alone and says so.
  static ServiceNotice* Attach(Gtk::Box& host, bool prepend = true);

  // Render one snapshot. Attach() wires this up for you; it is public so a
  // page that already owns the snapshot (the Windows one-writer shape) can
  // push instead of subscribing.
  void Render(const ServiceSetup::Snapshot& snapshot);

  // Whether this snapshot deserves a card at all. Healthy shows NOTHING —
  // and neither does an unknown state, because a banner with no evidence
  // behind it is how users learn to ignore banners.
  static bool ShouldShow(const ServiceSetup::Snapshot& snapshot);

  // Bind/unbind the ServiceSetup subscription by hand (Attach does this).
  void Subscribe(ServiceSetup& setup);
  void Unsubscribe();

 private:
  void BuildUi();
  void OnAction();
  void OnCancel();
  void ToggleCommand();
  void CopyCommand();
  void SetPulsing(bool on);

  Gtk::Box* card_ = nullptr;
  Gtk::Label* title_ = nullptr;
  Gtk::Label* message_ = nullptr;
  Gtk::Label* status_ = nullptr;
  Gtk::ProgressBar* progress_ = nullptr;
  Gtk::Button* action_ = nullptr;
  Gtk::Button* cancel_ = nullptr;
  Gtk::Button* commandToggle_ = nullptr;
  Gtk::Revealer* commandRevealer_ = nullptr;
  Gtk::Label* commandText_ = nullptr;
  Gtk::Button* commandCopy_ = nullptr;

  sigc::connection pulse_;
  std::string command_;
  bool commandOpen_ = false;
  // The wording the card is CURRENTLY committed to. An action that restarts
  // the daemon walks the state through Running and Unknown on its way back;
  // re-deriving the title from each of those would make the card flicker
  // between "Update the VPN service" and "Set up the VPN service" while the
  // user watches. The Windows rule is the same one: the title says what the
  // card is for and only the message changes.
  ServiceSetup::State wording_ = ServiceSetup::State::NotInstalled;

  ServiceSetup* setup_ = nullptr;
  std::uint64_t token_ = 0;
  // Lifetime token for the posted handler: a snapshot already queued on the
  // main loop when this widget is destroyed must find an expired weak_ptr and
  // return, rather than write into freed labels.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace urnw
