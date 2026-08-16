// SettingsPage — the SETTINGS destination of the Windows-parity shell
// (windows SettingsPage.{h,cpp} + SettingsSheets.cpp; spec
// docs/parity/settings.md). "Preferences in three columns": three EQUAL
// full-bleed panes separated by 1px rules, every row code-built from the kit
// so the whole destination is one row species per list.
//
//   Pane A "General" — what the app DOES: the General group (product updates,
//     automatic update checks), the Connections group (kill switch + its two
//     honesty disclosures, blocked locations, app split rules, the VPN
//     service row), then the Service updates group (where the service is
//     downloaded FROM: channel, pinned release, the two version readouts).
//   Pane B "Device" — what this machine IS: the Device group (name, spec),
//     Post Quantum Identity, then Advanced (the advanced-mode toggle and
//     Save logs).
//   Pane C "About" — what the app IS: the version rows and Stay in touch.
//
// Neither the account-subject sections (security / referrals / plan / danger)
// nor Sign out live here: R4 moved them to the ACCOUNT destination's hosts.
// This page owns General/Connections/Device/PQI/Advanced/About only.
//
// THREE INVARIANTS THIS PAGE IS BUILT AROUND:
//
//  1. The advanced-mode toggle is THE ONE WRITER and it ONLY WRITES. It calls
//     SdkHost::SetAdvancedMode (persist FIRST, publish SECOND) and never
//     applies anything itself; the standing value comes back through the
//     host's handler into SetAdvancedMode(bool) below — the SAME path a
//     disk-restored value takes, so toggle-now and on-at-launch cannot render
//     differently. An echo guard keeps that apply from re-entering the
//     handler as a user edit.
//  2. Every async field terminates in exactly one of six states (FieldState):
//     NoSession / NoDevice / Loading / Loaded / Empty / Failed. NoDevice
//     exists because "signed in but the service is not up" must not say
//     "please login" — that is a lie. A dash is never an answer.
//  3. The kill switch reads BACK rather than trusting a write: it is the one
//     toggle where a wrong state costs privacy. Kill switch == !routeLocal;
//     the inversion lives in SdkHost, never in this view.
//
// Fold table (windows MainWindow::ApplyBreakpoint): >= 1400 dip three panes;
// 900..1399 About folds; < 900 Device folds too and only General remains.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "PaneKit.hpp"
#include "SdkHost.hpp"
#include "Ui.hpp"

namespace urnw {

// Reused sheets (docs/parity/linux-reuse.md): the split-rules editor and the
// provider-identities list already exist in this tree and are opened as-is.
class SplitRulesSheet;
class ProviderIdentitiesSheet;
// Built for this destination, file-local to SettingsPage.cpp (spec §6.1/§6.2).
class SettingsDeviceNameSheet;
class SettingsBlockedLocationsSheet;
// The release picker behind the "Release" row (this file's own addition).
class SettingsReleaseTagSheet;

// ---- where the VPN SERVICE comes from ---------------------------------------
// The Flatpak and the AppImage ship the INTERFACE ONLY: urnetworkd is not in
// either bundle, it is downloaded from GitHub Releases and installed with one
// polkit prompt. "Which releases" is therefore a user-visible choice, not a
// build constant — and GitHub Releases is the source of truth for "is the
// service up to date".
enum class ServiceChannel {
  Beta,          // this fork's prereleases (beta/custom-server) — the default
  UpstreamMain,  // the upstream project's main — NO BUILDS PUBLISHED YET, so
                 // the row renders visibly DISABLED with that reason on it,
                 // never hidden and never silently inert
};

// What Settings SHOWS about the service. Pushed in by the shell from
// ServiceSetup + the release check; this page never polls GitHub, never
// downloads, never elevates. Until something is pushed (`valid == false`) the
// installed row falls back to the daemon_version the control session's hello
// already carries, which is real evidence and free.
struct ServiceReleaseView {
  // TWO INDEPENDENT PIECES OF EVIDENCE, deliberately not one "valid" flag.
  //
  // `classified` says the classifier RAN and `installed`/`installedVersion`
  // below are its answer — which is the only thing that can distinguish
  // "registered but stopped" from "not installed at all". It is windows
  // ServiceSetup::State::Unknown inverted: leave it false and the page falls
  // back to the daemon_version the control hello carried, and says "Not
  // detected" rather than claiming an absence it cannot see.
  //
  // The check fields below stand on their own and are rendered whatever
  // `classified` says: a release check can succeed on a machine whose service
  // state is unknown, and vice versa.
  bool classified = false;

  // ServiceSetup::Classify's answer, flattened to what the rows render.
  bool installed = false;         // a urnetworkd is registered in any form
  std::string installedVersion;   // "" = registered but the version is unread

  // The release check against the CURRENT channel/tag.
  bool checking = false;          // a check is in flight
  bool checkFailed = false;       // the last check could not reach GitHub
  std::string availableVersion;   // "" = unknown (never checked / failed)
  std::string availableTag;       // the tag that version came from, as minted

  // Newest-first tags the last check parsed. Feeds the pin picker so a user
  // does not have to type a tag from memory; empty is fine (the picker then
  // takes free text and validates it).
  std::vector<std::string> knownTags;

  bool busy = false;  // an elevated verb is in flight: every action disabled
};

// §2.1 — the six terminal states of every async field on this destination and
// its sheets. NoDevice is NOT a nicety: "signed in but the service is not up"
// rendered as "please login" is a lie, and a dash cannot distinguish nothing
// from not-loaded from failed. Namespace scope because the page and its two
// sheets all terminate in it.
enum class SettingsFieldState { NoSession, NoDevice, Loading, Loaded, Empty, Failed };

class SettingsPage : public Gtk::Box {
 public:
  explicit SettingsPage(SdkHost& host);
  ~SettingsPage() override;

  // nav-select + auth-change API loads (windows LoadSettings, which fires on
  // navigation to Settings AND to Account). Bumps the stale-async epoch, then:
  // local device state with no round trips, and — only with a session — the
  // preferences and device-info reads. With no session every server-backed
  // field lands on NoSession and NOTHING is left on a spinner. This no-session
  // branch IS the Settings-owned half of windows ResetForSignOut (the account
  // half lives on the Account destination).
  void Load();

  // The D5 apply path: MainWindow's advanced-mode handler calls this with the
  // standing value (bind-then-replay, so a disk-restored true is never lost).
  // No-op when the switch already reads `on`; otherwise written under the echo
  // guard so the apply cannot echo back out through SdkHost as a user edit.
  void SetAdvancedMode(bool on);

  // The spec's pane-fold table (1400 / 900 dip). A folded pane is hidden
  // together with its rule — never left as a zero-width column.
  void ApplyBreakpoint(int widthDip);

  // PUBLIC because the Network destination's detail pane is a SECOND door to
  // the same sheet; the blocked list is a network-API read this page owns.
  void ShowBlockedLocationsSheet();

  // Feed slots the window's DrawerEvent dispatcher forwards (already
  // marshalled onto the GTK loop). RouteLocal keeps the kill switch honest
  // when it is changed from the connect surface; ProviderIdentities cascades
  // into the identities sheet while it is open.
  // TODO(sdk-wiring): SdkHost::SetSettingsObserver — a dedicated slot so this
  // page does not ride the drawer feed once the host grows one.
  void OnRouteLocalEvent();
  void OnProviderIdentitiesEvent();

  // The snackbar surface belongs to the shell (windows binds SettingsPage's
  // snackbar_ to the SUPPORT destination's InfoBar — a flagged quirk this port
  // deliberately fixes by emitting to the window-level bar). error=true is the
  // persistent treatment (Warning/Error stay until dismissed).
  std::function<void(const Glib::ustring& message, bool error)> on_snackbar;

  // ---- the release-source choice (owner ask) --------------------------------
  // Hand the page everything it renders about the service. Call it whenever
  // ServiceSetup republishes (classify, check started/finished, elevated verb
  // started/ended); it is idempotent and cheap, and it REPLACES the previous
  // view rather than merging into it — send a whole snapshot every time.
  // Marshal to the GTK loop first: this page never leaves the main thread.
  void ApplyServiceRelease(ServiceReleaseView view);

  // The user changed the channel or the pinned tag. The shell hands this to
  // ServiceSetup/the release checker so the NEXT check and the NEXT install
  // follow the new source. `tag` is "" for "follow the newest release on this
  // channel"; when non-empty it has already been shape-validated here, but
  // ServiceSetup must re-validate before it ever reaches a URL.
  std::function<void(ServiceChannel channel, const std::string& tag)>
      on_service_source_changed;
  // "Check" — re-run the GitHub release check against the current source.
  // Unprivileged: a plain HTTPS GET of the releases JSON.
  std::function<void()> on_service_check_now;
  // "Install" / "Update" — the whole download-verify-elevate flow, which lives
  // in ServiceSetup and NOT here. Unbound leaves the button disabled rather
  // than clickable-and-inert.
  std::function<void()> on_service_install;

  // ---- the standing choice, readable with no view ---------------------------
  // ServiceSetup reads these to know WHAT to fetch. They are plain
  // app_prefs.json reads (AppPrefs.hpp), valid before any window exists — the
  // same standing-value contract advanced_mode follows.
  //
  // The two keys, documented here because AppPrefs.hpp's known-key list is not
  // this file's to edit:
  //   "service_release_channel"  string  "beta" | "upstream"   default "beta"
  //   "service_release_tag"      string  ""=newest, else the tag AS MINTED
  //                                      (with the leading v, e.g.
  //                                      "v2026.8.15-101076420-beta")
  static constexpr const char* kChannelPrefKey = "service_release_channel";
  static constexpr const char* kTagPrefKey = "service_release_tag";

  // The stored channel, exactly as persisted — it may name a channel that has
  // no builds (only a hand-edited prefs file can get there today, because the
  // row for it is disabled). The page renders that truthfully rather than
  // silently substituting; a caller that is about to FETCH must check
  // ChannelHasBuilds() first and report "unavailable" instead of querying.
  static ServiceChannel StoredChannel();
  static bool ChannelHasBuilds(ServiceChannel channel);
  // "owner/repo" for the GitHub releases API. One constant per channel — if
  // this fork's repo is renamed, that is the only edit (windows keeps its twin
  // as config::kUpdateRepo in App/Config.h; lift both into Config.hpp when
  // ServiceSetup lands and this returns from there instead).
  static const char* ChannelRepo(ServiceChannel channel);
  // The pinned tag, or "" for "follow the newest release on the channel".
  static std::string PinnedTag();

 private:
  // ---- construction --------------------------------------------------------
  void BuildGeneralSection(Gtk::Box& host);
  void BuildConnectionsSection(Gtk::Box& host);
  void BuildServiceUpdatesSection(Gtk::Box& host);
  void BuildDeviceSection(Gtk::Box& host);
  void BuildIdentitySection(Gtk::Box& host);
  void BuildAdvancedSection(Gtk::Box& host);
  void BuildVersionSection(Gtk::Box& host);
  void BuildStayInTouchSection(Gtk::Box& host);

  // ---- loads ---------------------------------------------------------------
  // No round trips: client id off the device, kill switch off LocalState.
  // Runs at build time and on every Load().
  void ApplyLocalDeviceState();
  void LoadPreferences();  // accountPreferencesGet -> the product-updates toggle
  void LoadDeviceInfo();   // getNetworkClients -> this client's name + spec

  // ---- handlers ------------------------------------------------------------
  void OnProductUpdatesToggled();
  void OnKillSwitchToggled();
  // The one writer for the "what is actually in force" line under the switch.
  // Deliberately never touches the switch: the switch is the request.
  void ApplyKillSwitchState();
  void OnAdvancedModeToggled();
  void SaveLogsToFile();
  void ConfirmUninstallService();
  // A channel radio became ACTIVE. Only the newly-active half of a radio pair
  // calls this — reading "which one is on" from inside a toggled handler races
  // the group's own deactivate/activate order and would write the pref twice.
  void OnChannelSelected(ServiceChannel channel);
  // ONE writer for every Service-updates row: it reads serviceView_ (what the
  // shell pushed) and the two prefs, and renders. Never called from a row
  // handler's own branch — the handler persists, then calls this.
  void RenderServiceRelease();
  // Persist-then-publish, the shape SetAdvancedMode uses: the pref is written
  // first (it is the standing truth a late-built ServiceSetup will read), then
  // the change is announced.
  void PublishReleaseSource();

  // ---- sheets --------------------------------------------------------------
  Gtk::Window* RootWindow();  // the transient parent, resolved lazily
  void ShowDeviceNameSheet();
  void ShowAppSplitRulesSheet();
  void ShowIdentitySheet();
  void ShowReleaseTagSheet();

  // ---- helpers -------------------------------------------------------------
  void Snack(const Glib::ustring& message, bool error);

  SdkHost& host_;
  // stale-async guard: bumped by Load() and the destructor; a completion
  // carrying an older value is dropped before it touches the page
  std::shared_ptr<uint64_t> epoch_ = std::make_shared<uint64_t>(0);

  // ---- the pane shell ------------------------------------------------------
  kit::Pane paneA_;  // General
  kit::Pane paneB_;  // Device
  kit::Pane paneC_;  // About
  Gtk::Widget* ruleB_ = nullptr;  // A | B
  Gtk::Widget* ruleC_ = nullptr;  // B | C
  // three EQUAL columns: a horizontal size group pins the panes to one
  // request, hexpand then splits the remainder evenly between them
  Glib::RefPtr<Gtk::SizeGroup> paneSizes_;
  int lastFold_ = -1;  // 3 / 2 / 1 panes; -1 = never applied

  // ---- Pane A: General -----------------------------------------------------
  Gtk::Switch* productUpdates_ = nullptr;
  Gtk::Label* productUpdatesState_ = nullptr;
  Gtk::Widget* productUpdatesStateRow_ = nullptr;  // hidden when the line is empty
  bool applyingPreference_ = false;  // echo guard: the load writes IsOn
  bool preferencesLoaded_ = false;   // the toggle is inert until the value is known
  Gtk::Switch* autoCheckUpdates_ = nullptr;

  // ---- Pane A: Connections -------------------------------------------------
  Gtk::Switch* killSwitch_ = nullptr;
  bool applyingKillSwitch_ = false;  // echo guard
  // What is REALLY in force (SdkHost::KillSwitchStatus), rendered under the
  // switch. Hidden while the switch is off — there is nothing to disclose.
  Gtk::Label* killSwitchState_ = nullptr;
  Gtk::Widget* killSwitchStateRow_ = nullptr;
  // the WHOLE row hides together (a caption pointing at a hidden button is
  // worse than no row at all)
  Gtk::Box* serviceRowHost_ = nullptr;
  Gtk::Button* serviceUninstall_ = nullptr;

  // ---- Pane A: Service updates ---------------------------------------------
  Gtk::Label* installedVersionValue_ = nullptr;
  Gtk::Label* availableVersionValue_ = nullptr;
  Gtk::Button* checkReleasesNow_ = nullptr;
  Gtk::CheckButton* channelBeta_ = nullptr;
  Gtk::CheckButton* channelUpstream_ = nullptr;
  bool applyingChannel_ = false;  // echo guard: RenderServiceRelease writes them
  // Shown ONLY when the stored channel has no builds (a hand-edited prefs
  // file): the page says so instead of quietly querying a different repo.
  Gtk::Widget* channelStrandedRow_ = nullptr;
  Gtk::Label* channelStranded_ = nullptr;
  kit::PaneTwoLineRowButton releaseRow_;  // value = "Latest" or the pinned tag
  Gtk::Label* installNote_ = nullptr;     // "up to date" vs the standing note
  Gtk::Button* serviceInstall_ = nullptr;
  ServiceReleaseView serviceView_;  // the last thing the shell pushed

  // ---- Pane B: Device ------------------------------------------------------
  kit::PaneTwoLineRowButton deviceNameRow_;
  Gtk::Label* deviceSpecValue_ = nullptr;
  std::string clientId_;    // "" with no device
  std::string deviceName_;  // cached for the edit sheet's prefill

  // ---- Pane B: Advanced ----------------------------------------------------
  Gtk::Switch* advancedMode_ = nullptr;
  bool applyingAdvancedMode_ = false;  // re-entrancy guard on the apply path

  // ---- sheets --------------------------------------------------------------
  std::unique_ptr<SettingsDeviceNameSheet> deviceNameSheet_;
  std::unique_ptr<SettingsBlockedLocationsSheet> blockedSheet_;
  std::unique_ptr<SplitRulesSheet> splitRulesSheet_;
  std::unique_ptr<ProviderIdentitiesSheet> identitiesSheet_;
  std::unique_ptr<SettingsReleaseTagSheet> releaseTagSheet_;
  std::unique_ptr<Gtk::Window> confirmDialog_;  // the uninstall confirmation
};

}  // namespace urnw
