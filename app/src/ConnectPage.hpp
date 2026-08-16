// HOME / CONNECT — the three-pane destination (docs/parity/connect-page.md):
//
//   Pane A  CONNECT     330dip rail: status row, the hero canvas, the selected
//                       provider row, the connect action, provide + connect
//                       options, network peers.
//   Pane B  ACTIVITY    star: live throughput header + the routing-decision
//                       list (selectable in Advanced Mode).
//   Pane C  STATISTICS  380dip: session figures, contracts, split rules, DNS
//                       (and the connection inspector in Advanced Mode).
//
// Panes are floor-to-ceiling and separated by 1px rules, never gaps. The fold
// table lives in ApplyBreakpoint: Advanced >=1000 three panes, >=640 two,
// <640 one; Simple is ALWAYS one pane capped at 480dip and centred.
//
// One writer per surface (windows discipline): ApplyConnectStatus renders the
// aggregate health to the status row, the hero state, the button label AND
// the window status strip from ONE reading, so the hero can never lag the
// line above it.
//
// That reading is `urnw::health::Reading` (Health.hpp — the state table lives
// there). ApplyConnectStatus takes it ONCE per application and renders it;
// nothing on this page derives a status, a dot, a hero pose or a button label
// from anything else. The defect that motivated it: the headline "Connecting
// to providers" with a yellow dot and the connecting hero, beside a button
// reading "Disconnect", at the instant the owner pressed Disconnect — four
// channels taken from four different readings of the raw SDK status.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "ConnectCanvas.hpp"
#include "ContractsSheet.hpp"
#include "DnsSheet.hpp"
#include "Health.hpp"
#include "PaneKit.hpp"
#include "SdkHost.hpp"
#include "SplitRulesSheet.hpp"
#include "TransferChart.hpp"

namespace urnw {

class ConnectPage : public Gtk::Box {
 public:
  explicit ConnectPage(SdkHost& host);
  // Orphans every marshaled completion (see alive_) before any member dies:
  // SdkHost::RequestReliability delivers on the main loop and cannot know this
  // page is gone.
  ~ConnectPage() override;

  // live feeds (MainWindow relays; all already marshaled to the GTK loop)
  void ApplyStats(const LiveStats& stats);
  void SetConnected(bool connected);
  void SetConnectionStatus(const std::string& status);
  // The daemon-session line under the status row ("" = healthy).
  // TODO(wiring): MainWindow owns the SCM/daemon probe and still renders it in
  // the LEGACY column's daemonStatusLabel_; it must relay every change here
  // (connectPage_->SetDaemonNotice(text)) or a user whose urnetworkd is
  // missing/stopped/mismatched reads only "Disconnected" on the new Home.
  void SetDaemonNotice(const Glib::ustring& notice);
  // The drawer's change feed (SdkHost::DrawerEvent), dispatched per group
  // exactly as ConnectDrawer::OnHostEvent does: every case re-reads through the
  // SdkHost accessors on the GTK thread and re-applies ONE surface.
  void OnHostEvent(DrawerEvent event);
  // Re-seed every pane B/C cache from the Current* getters (login, tab entry,
  // window re-show). Idempotent.
  void Resync();

  // What the connect action IN FRONT OF THE USER does, from the same reading
  // that wrote the button's label (health::ActionIsDisconnect — the shared
  // predicate the tray item uses too).
  //
  // TODO(wiring): MainWindow::ToggleConnect still decides with its own
  // `connected_` ("if (connected_) host_.Disconnect(); else …connect"), which is
  // a SECOND answer to the question this button already answers. They disagree
  // in exactly the states this page exists to render: while Connecting, and
  // while the machine is captured with the SDK idle, the button says Disconnect
  // and the press starts a connect. The fix is one line there —
  // `if (connectPage_ && connectPage_->ConnectActionIsDisconnect())
  // host_.Disconnect();` — and it cannot be made here: this page does not own
  // the SDK calls.
  bool ConnectActionIsDisconnect() const {
    return reading_.action == health::Action::Disconnect;
  }

  // The rendered status line and its dot colour, for the window status strip.
  //
  // TODO(wiring): §2.1 ends "The same text+dot is relayed to the window status
  // strip via w_.ApplyStatusStripConnection(text, dot) — one derivation, two
  // surfaces." On this port MainWindow::SetConnected still writes the strip
  // itself, from the RAW SDK status string and a two-colour connected/not dot
  // (`shell_->SetStatusState(lastStatus_, connected ? "#87FB67" : "#2A60FF")`),
  // so the strip can read "CONNECTING" in blue under a status row that says
  // "Disconnecting…" in yellow. The fix is to push these two values from
  // MainWindow wherever it currently derives its own.
  Glib::ustring ConnectStatusText() const;
  const char* ConnectStatusDot() const { return statusDotColor_; }

  void SetAdvancedMode(bool on);   // structural: Simple <-> Advanced
  void ApplyBreakpoint(int widthDip);
  void SetPresentationActive(bool active);
  void Tick();  // the shared ~10fps clock: canvas dot transitions

  // the connect toggle, shared by the hero, the button and the tray
  std::function<void()> on_toggle_connect;
  // the selected-provider row opens the chooser (owned by MainWindow)
  std::function<void()> on_open_locations;
  // pane C's "Connected to N providers" row opens the globe sheet, which
  // MainWindow owns (it carries the GeoClue location-override controller); the
  // page cannot own that sheet itself. Assigned in MainWindow::BuildHome
  // beside the other two (MainWindow.cpp: on_open_provider_locations ->
  // OpenProviderLocations).
  std::function<void()> on_open_provider_locations;

 private:
  // one DNS status row: a state dot, the resolver name, On/Off
  struct DnsStatusRow {
    Gtk::Widget* root = nullptr;
    Gtk::Label* dot = nullptr;
    Gtk::Label* state = nullptr;
  };

  void BuildPaneA();
  void BuildPaneB();
  void BuildPaneC();
  void BuildInspectorGroup();
  void BuildDataUsageGroup();
  void BuildContractsGroup();
  void BuildSplitRulesGroup();
  void BuildDnsGroup();
  DnsStatusRow MakeDnsStatusRow(const Glib::ustring& title);

  void ApplyConnectStatus();       // THE one writer
  // Everything ApplyConnectStatus reads, gathered in one place: the SDK status,
  // the honest tunnel, the provider window and urnetworkd's own facts. Pure
  // reads — nothing here may take SdkHost's mutex or touch the daemon.
  health::Inputs ReadHealthInputs() const;
  // The provider grid re-push. Called from ApplyConnectStatus AFTER the hero
  // state is applied, because ConnectCanvas::SetGrid drops every push unless
  // the canvas is already Connecting (ConnectCanvas.cpp: "if (state_ !=
  // State::Connecting) return;" — correct iOS/Windows parity, never relaxed).
  // Pushing from here instead of from ApplyStats is what stops the entry-edge
  // push from being dropped no matter which feed applied the status.
  void PushGrid();
  // The connect toggle, shared by the hero and the button. Takes the current
  // reading BEFORE handing off, so a Disconnect press is recorded as a local
  // intent and the row can say "Disconnecting…" in the same frame.
  void OnConnectToggle();
  // Re-read urnetworkd's tunnel/floor facts (read-only). Called on a tunnel
  // edge and while a Disconnect press is in flight: nothing pushes them, and
  // the captured-machine reading is only honest against a fresh status.
  void RefreshServiceFacts();
  // the dev-only hero preview (connect-canvas.md §15); no-ops unless
  // URNETWORK_PREVIEW_HERO is set
  void SetupPreviewHero();
  void ApplyPreviewStep(size_t index);
  void PreviewTick();
  void ApplyMoreOptionsVisibility();
  void SyncProvideControlMode();
  void ApplyProvideControlMode();
  void ApplyBlockerUi();
  void ApplyKillSwitchUi();
  // the selected-provider row (§2.3): stats.locationName with a selected peer
  // resolved to its device name; empty => "Best available provider"
  void ApplyLocationRow();
  // §2.8 network peers: the count line, the group count and the peer rows
  void ApplyPeerCount();
  void ApplyPeersList();
  // §2.8 connect options: seed the mode segments + the three profile toggles
  // from the device/persisted PerformanceProfile (echo-guarded), and the one
  // writer back to the SDK.
  void SeedConnectControls();
  void PushPerformanceProfile();
  void OnConnectionModeChanged();

  // ---- pane B / C writers (one per surface) --------------------------------
  void ApplyConnectionsList();
  void ApplyConnectionSelectionVisuals();
  void SelectConnection(const std::string& id);
  void ApplySessionCardsVisibility();
  void ApplySessionRows();
  void ApplyContractsList();
  void ApplySplitRuleCount();
  void ApplyDnsCard();
  void ApplyDnsRecommendationPill();
  void ApplyInspector();
  void ApplyInspectorVisibility();
  // The exit a destination ip routed through, and that exit's health, joined
  // out of the reliability snapshot (DestinationExit.DestinationIp ->
  // ClientId -> Exit). nullopt = "no recorded address of this action is in the
  // snapshot", the normal case for a host that resolved after the last
  // refresh. Absent, never guessed: an inspector that answers "which exit"
  // with a plausible WRONG exit is worse than one that says it does not know.
  struct ExitRouting {
    std::string clientId;
    int32_t flowCount = 0;
    bool haveExit = false;  // the clientId was also found in exits_
    int32_t tier = 0;
    int32_t effectiveTier = 0;
    int32_t exitFlowCount = 0;
    int32_t dialFailureCount = 0;
    bool quarantined = false;
    bool warning = false;
    std::string warningCause;
    bool proven = false;
    int64_t probeAgeSeconds = 0;
  };
  std::optional<ExitRouting> RoutingForAddresses(
      const std::optional<urnet::StringList>& addresses) const;
  // Refresh the two tables the inspector joins against, via
  // SdkHost::RequestReliability (ExitsOnly): the host owns the worker, the
  // host-wide single-flight gate and the marshal back to the main loop, so
  // nothing here may call ReadReliability directly — it is several synchronous
  // device rpcs taken under the host lock.
  void RefreshExitRouting();
  void PullThroughput();
  // Re-read every feed and apply ONLY the surfaces whose reading changed.
  // force = apply everything (build, resync, mode change).
  void RefreshFeeds(bool force);
  void RefreshAllPanes() { RefreshFeeds(true); }
  // The clock-driven fallback for the change feed (see PollFeeds' comment):
  // until MainWindow routes DrawerEvent into OnHostEvent, the page's own clock
  // is the only thing that can keep panes B and C alive.
  void PollFeeds();
  // the fold decision, taken on the width the PANES share (not the toplevel)
  void ApplyFold(bool force);
  // the 100ms clock runs only while presenting AND this page is the mapped
  // destination (§5: "skip if window not visible", and a page nobody is
  // looking at must not wake the process ten times a second)
  void UpdateClock();

  // sheets (created on first open against the page's root window)
  void OpenContractsSheet();
  void OpenSplitRulesSheet();
  void OpenDnsSheet();
  Gtk::Window* RootWindow();

  SdkHost& host_;
  // The stale-async guard (CONTRACT.md rule 3): bumped on every reseed and on
  // presentation teardown. A worker completion captures the shared_ptr by
  // value plus the generation it started on and drops itself when they differ,
  // so a refresh in flight across a logout / mode toggle / teardown cannot
  // write into a surface that has since been rebuilt (see RefreshExitRouting).
  std::shared_ptr<uint64_t> epoch_ = std::make_shared<uint64_t>(0);
  // The other half of the guard. epoch_ alone is NOT enough for a completion
  // marshaled back from a worker: reading the epoch means dereferencing a
  // member of this page, so a completion that lands after the page is
  // destroyed would already have touched freed memory to discover it is late.
  // alive_ is held by the completion itself and is tested FIRST.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  bool advanced_ = false;
  bool presenting_ = false;
  bool pageVisible_ = false;  // this destination is the mapped stack child
  bool connected_ = false;
  bool moreOptionsExpanded_ = false;
  // set the first time a real DrawerEvent lands: the clock-driven poll then
  // drops to a slow safety net instead of carrying the page on its own.
  bool eventsWired_ = false;
  int widthDip_ = 1120;
  int foldWidth_ = -1;  // the pane-grid width the current fold was taken on
  bool foldRecheckPending_ = false;
  std::string connectStatus_ = "DISCONNECTED";
  LiveStats stats_;
  Glib::ustring daemonNotice_;
  std::string selectedConnectionId_;

  // ---- the aggregate health (Health.hpp) -------------------------------------
  // The last reading ApplyConnectStatus rendered. Kept so the toggle can ask
  // what the button in front of the user actually says before it acts, instead
  // of re-deriving it (a second derivation is a second answer).
  health::Reading reading_;
  // the dot colour ApplyConnectStatus resolved for `reading_` — resolved ONCE,
  // so the strip accessor cannot re-derive a different one
  const char* statusDotColor_ = "#2A60FF";
  // The Disconnect press, held locally until the session settles. An explicit
  // intent outranks the SDK's stale CONNECTING/DESTINATION_SET status — that
  // stale status beside a Disconnect press is the contradiction the owner
  // screenshotted. Bounded, never a latch: kDisconnectIntentUs after the press
  // the reading goes back to whatever the feeds say, so a press that the daemon
  // never acted on cannot leave the page stuck on "Disconnecting…".
  bool disconnectRequested_ = false;
  gint64 disconnectRequestedAtUs_ = 0;
  // When the "tunnel up, nothing carrying" reading began. The grace that stops
  // a single grid push from taking a working connection off Connected; 0 = the
  // reading is not standing.
  gint64 noProviderSinceUs_ = 0;

  // ---- the hero preview walk (connect-canvas.md §15) --------------------------
  // URNETWORK_PREVIEW_HERO=<state|walk>: the real status/grid writes are
  // SUPPRESSED and the reading is driven from here instead, so every row of the
  // state table can be rendered and screenshotted without a session, a daemon
  // or a tunnel. Env-gated once at construction; off in every real session.
  bool previewHero_ = false;
  bool previewWalk_ = false;
  size_t previewStep_ = 0;
  health::Inputs previewInputs_;
  std::vector<urnet::ProviderGridPoint> previewGrid_;
  int64_t previewCols_ = 14;

  // ---- the feed caches (§5) --------------------------------------------------
  // Every one is an optional/absent-capable read: with no session the getters
  // return nullopt and the surfaces render "no session", never a zero.
  std::optional<urnet::BlockActionList> blockActions_;
  std::optional<urnet::BlockStats> blockStats_;
  std::optional<urnet::ContractPeerRowList> contractRows_;
  std::optional<urnet::BlockActionOverrideList> splitRules_;
  std::optional<urnet::DnsResolverSettings> dnsSettings_;
  // §4.1's exit-routing cache, refreshed every 5 s in Advanced Mode. Carried
  // with the SNAPSHOT's own optionality, because on this surface a fabricated
  // zero is the failure mode: nullopt = "no session, or nothing has been read
  // yet / the rpc threw" (UNKNOWN); an EMPTY list is the real answer "this
  // device has no exits". The inspector and the "Exits" figure render those
  // two differently. UI thread only.
  std::optional<urnet::ExitList> exits_;
  std::optional<urnet::DestinationExitList> destinationExits_;
  std::string countryCode_;  // lowercased connected country (dns pill)
  std::string countryName_;
  std::optional<urnet::NetworkPeerList> peers_;  // nullopt = discovery down
  int64_t peerCount_ = 0;
  // the last SelectedLocation reading: the provider row renders from this
  // cache so a peers push does not take SdkHost's lock a second time
  std::optional<urnet::ConnectLocation> selectedLocation_;
  // Cheap value fingerprints of the feeds above: the poll re-reads every
  // second, and a list REBUILD destroys hover/keyboard focus, so a surface is
  // only re-applied when its reading actually changed. -1 = never read.
  uint64_t blockActionsSig_ = ~0ull;
  uint64_t contractRowsSig_ = ~0ull;
  uint64_t splitRulesSig_ = ~0ull;
  uint64_t dnsSig_ = ~0ull;
  uint64_t peersSig_ = ~0ull;
  uint64_t locationSig_ = ~0ull;
  uint64_t profileSig_ = ~0ull;

  // ---- pane A ---------------------------------------------------------------
  kit::Pane paneA_;
  // The pane's own column, inside an AdwClamp: Simple Mode's 480dip is a
  // MAXIMUM, and set_size_request is a floor (it would make 480 the window's
  // minimum and cap nothing).
  Gtk::Box* paneAContent_ = nullptr;
  GtkWidget* paneAClamp_ = nullptr;
  GtkWidget* heroClamp_ = nullptr;  // hero host MaxWidth 190 (Adv) / 320
  Gtk::Label* statusDot_ = nullptr;
  Gtk::Label* statusText_ = nullptr;
  Gtk::Label* protectionText_ = nullptr;
  Gtk::Label* trafficHeldText_ = nullptr;
  Gtk::Label* statusReasonText_ = nullptr;
  Gtk::Label* daemonNoticeText_ = nullptr;
  Gtk::Button* hero_ = nullptr;
  ConnectCanvas* canvas_ = nullptr;
  Gtk::Label* locationText_ = nullptr;
  Gtk::Button* locationRow_ = nullptr;
  Gtk::Button* connectBtn_ = nullptr;
  Gtk::Button* moreOptionsToggle_ = nullptr;
  Gtk::Box* moreOptionsHost_ = nullptr;
  Gtk::Label* provideDot_ = nullptr;
  Gtk::ToggleButton* provideAuto_ = nullptr;
  Gtk::ToggleButton* provideAlways_ = nullptr;
  Gtk::ToggleButton* provideNetwork_ = nullptr;
  Gtk::ToggleButton* provideNever_ = nullptr;
  bool syncingProvide_ = false;
  Gtk::Label* discoverableText_ = nullptr;
  // connect options (§2.8): the 3-item connection-mode segmented control and
  // the three PerformanceProfile toggles, all echo-guarded
  Gtk::ToggleButton* modeAuto_ = nullptr;
  Gtk::ToggleButton* modeWeb_ = nullptr;
  Gtk::ToggleButton* modeStreaming_ = nullptr;
  Gtk::Switch* fixedIpToggle_ = nullptr;
  Gtk::Switch* anonToggle_ = nullptr;
  Gtk::Switch* pqeToggle_ = nullptr;
  // network peers: the count line over the peer rows
  Gtk::Label* peersMeta_ = nullptr;
  Gtk::Button* peersLine_ = nullptr;
  Gtk::Label* peersDot_ = nullptr;
  Gtk::Label* peersText_ = nullptr;
  Gtk::Box* peersHost_ = nullptr;
  Gtk::Switch* blockerToggle_ = nullptr;
  Gtk::Switch* killSwitchToggle_ = nullptr;
  // What urnetworkd says is REALLY in force, under the switch. A dedicated
  // wrapped line: the row's own note is trimmed to one ellipsized line, and a
  // truncated failure disclosure is the same defect as no disclosure.
  Gtk::Label* killSwitchNote_ = nullptr;
  // ONE echo guard around every programmatic control write (§2.8): each
  // handler returns while it is set, so a feed-driven write cannot loop back
  // into the SDK.
  bool updatingControls_ = false;

  // ---- pane B: activity ------------------------------------------------------
  Gtk::Widget* paneBRule_ = nullptr;
  Gtk::Widget* paneCRule_ = nullptr;
  kit::Pane paneB_;
  kit::Pane paneC_;
  TransferChart* remoteChart_ = nullptr;
  Gtk::Label* connectionsCount_ = nullptr;
  Gtk::Box* connectionsArea_ = nullptr;
  Gtk::Box* connectionsHost_ = nullptr;
  Gtk::Widget* connectionsEmpty_ = nullptr;
  // rows and ids in PARALLEL vectors: the selection is held by block-action
  // id, never by index (the feed rebuilds and rows move).
  std::vector<std::string> connectionIds_;
  std::vector<kit::PaneListRowButton> connectionRows_;
  // the row's announcement without the selection suffix, kept so selection can
  // repaint the name without re-deriving it from the feed
  std::vector<Glib::ustring> connectionNames_;

  // ---- pane C: statistics / inspector ----------------------------------------
  Gtk::Box* inspectorGroup_ = nullptr;
  Gtk::Button* inspectorClear_ = nullptr;
  Gtk::Label* inspectorTitle_ = nullptr;
  Gtk::Label* inspectorDot_ = nullptr;
  Gtk::Label* inspectorVerdict_ = nullptr;
  Gtk::Box* inspectorRows_ = nullptr;
  TransferChart* blockedChart_ = nullptr;
  TransferChart* localChart_ = nullptr;
  Gtk::Box* liveStatsGroup_ = nullptr;
  Gtk::Button* providerCountLine_ = nullptr;
  Gtk::Label* providerCountText_ = nullptr;
  Gtk::Box* sessionHost_ = nullptr;
  Gtk::Box* contractsHost_ = nullptr;
  Gtk::Label* splitRuleCountText_ = nullptr;
  Gtk::Box* splitRulesHost_ = nullptr;
  Gtk::Widget* dnsPillRow_ = nullptr;
  Gtk::Label* dnsPillDot_ = nullptr;
  Gtk::Label* dnsPillText_ = nullptr;
  Gtk::Button* dnsEditButton_ = nullptr;  // desensitized with no settings
  Gtk::Box* dnsRowsPanel_ = nullptr;
  DnsStatusRow dnsDohRow_;
  DnsStatusRow dnsUnencryptedRow_;
  DnsStatusRow dnsLocalRow_;
  DnsStatusRow dnsFallbackRow_;
  Gtk::Widget* dnsUnavailableRow_ = nullptr;

  // the reused detail sheets (linux-reuse.md: REUSE-AS-IS), built lazily
  // against the page's root window on first open
  std::unique_ptr<ContractsSheet> contractsSheet_;
  std::unique_ptr<SplitRulesSheet> splitRulesSheet_;
  std::unique_ptr<DnsSheet> dnsSheet_;

  // The shared ~10fps clock (windows: one drawer clock at 100ms): advances the
  // hero's grid-dot transitions, feeds the charts, and drives the feed poll.
  // Started/stopped by presentation state AND by this page being the mapped
  // destination — never by focus.
  sigc::connection tick_;
  int tickCount_ = 0;
};

}  // namespace urnw
