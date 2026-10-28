// The slow-internet indicator's call sites. Its decisions are pure and run in
// SlowInternetTest.cpp; the surfaces need GTK, gio and the SDK, so this reads
// their sources with the comments blanked (WiringSource.hpp), so prose cannot
// satisfy a contract:
//   - SdkHost registers the transport delay and remote listeners with the
//     device-lifetime subscriptions, reads both once after registering, takes
//     only the feed's own lock in them, and clears the feed with the device
//     and on the daemon's tunnel-gone verdict;
//   - the window takes the feed ungated, narrows it to a session that is up,
//     and drives the pill, the tray and the notification (posted, replaced
//     and withdrawn under one id, re-observed at the dwell's deadline) from
//     it, re-reads the numbers while slow, sooner while it is up, and the
//     app withdraws the notification as it ends;
//   - the tray's icon and tooltip come from tray_policy; the Connect page
//     moves its pill with the fold; the pill says what PillContent decides,
//     holds "Internet back to normal" on a timeout and opens nothing;
//     Settings saves the switch and tells the window; and the build compiles
//     the widgets and installs the tray art.
// SPDX-License-Identifier: MPL-2.0
#include <string>
#include <vector>

#include "TestHarness.hpp"
#include "WiringSource.hpp"

namespace {

namespace wiring = urnw::testing::wiring;

// Each needle occurs after the one before it.
bool InOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

}  // namespace

UR_TEST(SlowInternetWiring_TheHostKeepsADeviceLifetimeGatedFeed) {
  const std::string host = wiring::ReadCode("SdkHost.cpp");
  // the refresh takes the feed's revision with its read, so an answer the main
  // loop kept loses to any push since, and a failed read changes nothing
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(host, "void SdkHost::RefreshTransportDelay() {"),
                         {"ReadOnMain<Refresh>(", "delayFeed_.DelayRevision();",
                          "device_->getTransportDelay();", "if (!refresh.read) return;",
                          "delayFeed_.SeedDelay(refresh.generation, refresh.revision,",
                          "PublishTransportDelayLocked();"}));
  const std::string bind =
      wiring::FunctionBody(host, "TunnelStartResult SdkHost::BindRemoteDeviceLocked(");
  UR_EXPECT_TRUE(InOrder(bind, {"device_->setRpcServer(", "addConnectLocationChangeListener(",
                                "BindTransportDelayLocked();"}));

  const std::string feed = wiring::FunctionBody(host, "void SdkHost::BindTransportDelayLocked() {");
  UR_EXPECT_TRUE(InOrder(
      feed, {"generation = delayFeed_.Bind();", "delayRevision = delayFeed_.DelayRevision();",
             "remoteRevision = delayFeed_.RemoteRevision();",
             "subs_.push_back(device_->addRemoteChangeListener(",
             "delayFeed_.NoteRemote(generation, connected)",
             "subs_.push_back(device_->addTransportDelayChangeListener(",
             "delayFeed_.NoteDelay(generation, ToSlowInternetDelay(delay))",
             // the reads come after the registrations, and lose to their pushes
             "device_->getRemoteConnected();", "device_->getTransportDelay()",
             "delayFeed_.SeedRemote(generation, remoteRevision, connected);",
             "delayFeed_.SeedDelay(generation, delayRevision, delay)"}));
  // the bind, both listeners and the reads take the feed's lock, never mutex_
  UR_EXPECT_EQ(4u, wiring::CountOf(feed, "std::scoped_lock lock(delayMutex_);"));
  UR_EXPECT_FALSE(wiring::Contains(feed, "lock(mutex_)"));

  // the published reading is whole and numbered, and goes out only when it changed
  const std::string publish =
      wiring::FunctionBody(host, "void SdkHost::PublishTransportDelayLocked() {");
  UR_EXPECT_TRUE(InOrder(publish, {"delayFeed_.Current();", "reading.SameAs(delayPublished_)",
                                   "delayPublished_.serial = ++delaySerial_;",
                                   "onTransportDelay_(delayPublished_);"}));

  // gone with its device, and with the daemon's tunnel
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(host, "void SdkHost::TeardownDeviceLocked() {"),
                         {"subs_.clear();", "ClearTransportDelay();", "device_.reset();"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(host, "void SdkHost::NoteDaemonTunnelGone() {"),
                         {"daemonTunnelGone_.store(true);", "ClearTransportDelay();"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(host, "void SdkHost::ClearTransportDelay() {"),
                         {"std::scoped_lock lock(delayMutex_);", "delayFeed_.Clear();",
                          "PublishTransportDelayLocked();"}));

  // the feed outlives the subscriptions that call into it
  const std::string header = wiring::ReadCode("SdkHost.hpp");
  UR_EXPECT_TRUE(wiring::Before(header, "std::mutex delayMutex_;", "std::vector<urnet::Sub> subs_;"));
  UR_EXPECT_TRUE(wiring::Before(header, "TransportDelayHandler onTransportDelay_;",
                                "std::vector<urnet::Sub> subs_;"));
}

UR_TEST(SlowInternetWiring_TheWindowDrivesEverySurface) {
  const std::string window = wiring::ReadCode("MainWindow.cpp");
  // ungated: the tray and the notification follow it while hidden
  UR_EXPECT_TRUE(wiring::Contains(
      window, "PostToMain([this, reading] { ApplyTransportDelay(reading); });"));
  UR_EXPECT_TRUE(wiring::Contains(
      window, "slowNotifyEnabled_ = prefs::Get<bool>(slow_internet::kNotifyPrefKey, true);"));
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(window, "void MainWindow::ApplyTransportDelay("),
      {"if (reading.serial <= slowReading_.serial) return;", "slowReading_ = reading;",
       "ApplySlowInternet();"}));
  // a session up in this window's reading, and signed in
  const std::string shown = wiring::FunctionBody(window, "slow_internet::Reading MainWindow::SlowShown() const {");
  UR_EXPECT_TRUE(InOrder(shown, {"slowReading_.available && signedIn_ &&",
                                 "health::SessionUp(reading_.ToSignals("}));
  // a reading that ends the session takes the indicator with it
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(window, "void MainWindow::ApplyConnectReading("),
      {"on_tray_state(connected_, proven, status, signedIn_);", "ApplySlowInternet();"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(window, "void MainWindow::ApplySlowInternet() {"),
                         {"const slow_internet::Reading shown = SlowShown();",
                          "connectPage_->ApplySlowInternet(shown);", "ObserveSlowNotice();"}));
  // while slow, the numbers are read again on a one-shot timer, sooner while
  // the window is up, where the pill shows them; the window showing or
  // hiding re-arms it at the other period
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(window, "void MainWindow::ApplySlowInternet() {"),
                         {"ObserveSlowNotice();", "ScheduleSlowRefresh();"}));
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(window, "void MainWindow::ScheduleSlowRefresh() {"),
      {"slow_internet::Level(SlowShown()) <= 0", "slowRefreshTimer_.disconnect();",
       "windowVisible_ ? slow_internet::kPillRefreshSeconds : slow_internet::kRefreshSeconds",
       "if (slowRefreshTimer_.connected() && seconds == slowRefreshSeconds_) return;",
       "slowRefreshTimer_.disconnect();", "slowRefreshSeconds_ = seconds;",
       "Glib::signal_timeout().connect_seconds(", "slowRefreshTimer_ = sigc::connection();",
       "host_.RefreshTransportDelay();", "ScheduleSlowRefresh();", "return false;", "seconds);"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(window, "auto reconcilePresentation = [this] {"),
                         {"windowVisible_ = presentationActive;", "ScheduleSlowRefresh();"}));
  UR_EXPECT_TRUE(wiring::Contains(wiring::FunctionBody(window, "MainWindow::~MainWindow() {"),
                                  "slowRefreshTimer_.disconnect();"));
  // the tray follows the dwell, not the setting, and is told only of a change
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(window, "void MainWindow::PushTraySlow() {"),
                         {"traySlow_.view();", "T_(view.line->key, view.line->english)",
                          "on_tray_slow(level, text);"}));
  // the dwell is re-observed at its deadline, with nothing new to read
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(window, "void MainWindow::ObserveSlowNotice() {"),
      {"slowNoticeTimer_.disconnect();",
       "slowNotice_.Observe(shown, slowNotifyEnabled_, nowMillis, sink);",
       "traySlow_.Observe(shown, nowMillis);", "PushTraySlow();",
       "slowNotice_.NextDeadlineMillis();", "traySlow_.NextDeadlineMillis();",
       "Glib::signal_timeout().connect(",
       "ObserveSlowNotice();", "return false;"}));
  // one id of its own, posted and withdrawn through the app
  UR_EXPECT_TRUE(wiring::Contains(window, "kSlowNoticeId = \"slow-internet\";"));
  UR_EXPECT_TRUE(wiring::Contains(window, "app->send_notification(kSlowNoticeId, notification);"));
  UR_EXPECT_TRUE(wiring::Contains(window, "app->withdraw_notification(kSlowNoticeId);"));
  UR_EXPECT_TRUE(wiring::Contains(window, "slow_internet::ComposeNotice(notice,"));
  // Settings' switch reaches the notification at once
  UR_EXPECT_TRUE(wiring::Contains(
      window, "settingsPage_->on_slow_internet_notify = [this](bool on) { SetSlowInternetNotify(on); };"));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(window, "void MainWindow::SetSlowInternetNotify(bool on) {"),
                         {"slowNotifyEnabled_ = on;", "ObserveSlowNotice();"}));
  UR_EXPECT_TRUE(wiring::Contains(wiring::FunctionBody(window, "MainWindow::~MainWindow() {"),
                                  "slowNoticeTimer_.disconnect();"));

  const std::string app = wiring::ReadCode("main.cpp");
  UR_EXPECT_TRUE(InOrder(app, {"window->on_tray_slow = [&](int level, const std::string& text) {",
                               "if (tray) tray->SetSlow(level, text);"}));
  // the app's ending withdraws it: a notification must not outlive its app
  UR_EXPECT_TRUE(InOrder(app, {"const auto endApp = [&](bool teardown) {",
                               "if (!quit.Begin(teardown)) return;",
                               "window->WithdrawSlowInternetNotice();"}));
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(window, "void MainWindow::WithdrawSlowInternetNotice() {"),
      {"slowNotifyEnabled_ = false;", "ObserveSlowNotice();"}));
}

UR_TEST(SlowInternetWiring_TheTrayThePillAndTheSwitch) {
  const std::string tray = wiring::ReadCode("Tray.cpp");
  UR_EXPECT_TRUE(wiring::Contains(
      tray, "tray_policy::IconNameFor(self->provenForIcon(), self->slowLevelForIcon())"));
  UR_EXPECT_TRUE(wiring::Contains(
      tray, "tray_policy::ToolTipFor(self->statusForToolTip(), self->slowForToolTip())"));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(tray, "void Tray::SetSlow(int level, const std::string& text) {"),
                         {"slow_level_ = level;", "slow_text_ = text;", "EmitIconAndToolTip();"}));

  // the pill: pane A's header first, then wherever the fold puts the right end
  const std::string page = wiring::ReadCode("ConnectPage.cpp");
  UR_EXPECT_TRUE(InOrder(page, {"slowPill_ = Gtk::make_managed<SlowInternetPill>();",
                                "paneA_.header->append(*slowPill_);"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(page, "void ConnectPage::ApplyFold(bool force) {"),
                         {"const int panes = connect_fold::PaneCount(advanced_, paneWidth);",
                          "PlaceSlowPill(panes);"}));
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(page, "void ConnectPage::PlaceSlowPill(int panes) {"),
      {"panes == 3 ? paneC_.header : panes == 2 ? paneB_.header : paneA_.header",
       "g_object_ref(slowPill_->gobj());", "->remove(*slowPill_);",
       "header->append(*slowPill_);", "g_object_unref(slowPill_->gobj());"}));

  // the pill says what slow_internet::PillContent decides, with motion only on
  // screen and with animations on
  const std::string pill = wiring::ReadCode("SlowInternetPill.cpp");
  UR_EXPECT_TRUE(wiring::Contains(
      wiring::FunctionBody(pill, "bool SlowInternetPill::Animates() const {"),
      "motion::ShouldAnimate() && parent != nullptr && parent->get_mapped()"));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(pill, "void SlowInternetPill::Apply("),
                         {"const bool animate = Animates();",
                          "shown_ = slow_internet::PillContent::Next(shown_, reading);",
                          "if (previous) Leave(animate);", "if (!wasRecovered) Recover(animate);",
                          "hold_.disconnect();", "Draw(*shown_, animate && up_);",
                          "tunnel_->SetDepth(slow_internet::Depth(reading), animate);",
                          "if (!up_) Appear(animate);"}));
  // the reading going away: the pill goes once the tunnel's last square has
  // started fading
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(pill, "void SlowInternetPill::Leave(bool animate) {"),
      {"hold_.disconnect();", "tunnel::ContainerFadeOutDelayMs(tunnel_->depth())",
       "tunnel_->SetDepth(0, animate);", "FadeOut(exitDelayMs, animate);"}));
  // back to normal: the squares fade out as the check and the words come in,
  // and that holds 3 s on a timeout, which a hidden window runs too
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(pill, "void SlowInternetPill::Recover(bool animate) {"),
      {"Draw(*shown_, animate);", "tunnel_->SetDepth(0, animate);",
       "hold_ = Glib::signal_timeout().connect(", "hold_ = sigc::connection();", "shown_.reset();",
       "FadeOut(0, Animates());", "return false;", "slow_internet::kRecoveredMillis"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(pill, "SlowInternetPill::~SlowInternetPill() {"),
                         {"hold_.disconnect();", "roomResize_.disconnect();",
                          "layoutHook_.disconnect();", "frame_->unparent();"}));
  // it asks for no more than the line its header's room fits (a rail pane is
  // as wide as its natural width), and the frame hugs the line that fits; the
  // room is read in a new header at once, and after each layout of the window
  // while it is on screen, and a new one asks again once that pass is done
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(pill, "void SlowInternetPill::measure_vfunc("),
                         {"room_ < 0 ? minimum", "lines_->Fit(room_ - chrome)"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(pill, "void SlowInternetPill::size_allocate_vfunc("),
                         {"lines_->Fit(width - chrome)", "frame_->size_allocate("}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(pill, "SlowInternetPill::SlowInternetPill() {"),
                         {"property_parent().signal_changed().connect([this] { room_ = HeaderRoom(); });",
                          "signal_map().connect(", "native->get_surface()->signal_layout().connect(",
                          "CheckRoom();", "signal_unmap().connect([this] { layoutHook_.disconnect(); });"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(pill, "void SlowInternetPill::CheckRoom() {"),
                         {"const int room = HeaderRoom();", "room_ = room;",
                          "Glib::signal_idle().connect(", "roomResize_ = sigc::connection();",
                          "queue_resize();", "return false;"}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(pill, "void SlowInternetPill::Appear(bool animate) {"),
                         {"room_ = HeaderRoom();", "set_visible(true);"}));
  // the copy: the whole line, then the worse direction, then the label; one
  // accessible element named by what it says
  UR_EXPECT_TRUE(InOrder(
      wiring::FunctionBody(pill, "void SlowInternetPill::Draw("),
      {"FillLine(full_, content);", "FillLine(worseOnly_, content.WorseOnly());",
       "FillLine(labelOnly_, content.LabelOnly());",
       "kit::SetAccessibleLabel(*this, slow_internet::PillAccessibleName(content, Text));"}));
  UR_EXPECT_TRUE(InOrder(pill, {"\"accessible-role\", GTK_ACCESSIBLE_ROLE_STATUS",
                                "kit::MarkDecorative(*frame_);"}));
  // the look: square corners, the 35% amber outline, tabular digits, the
  // green check
  UR_EXPECT_TRUE(InOrder(pill, {"border: 1px solid alpha(#F5C242, .35); border-radius: 0;",
                                "font-feature-settings: \"tnum\";",
                                "background-color: #87FB67;"}));
  // a readout, not a button: nothing opens from it
  for (const char* control : {"MenuButton", "Popover", "GestureClick", "signal_clicked"}) {
    UR_EXPECT_FALSE(wiring::Contains(pill, control));
  }
  const std::string indicator = wiring::ReadCode("TunnelIndicator.cpp");
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(indicator, "void TunnelIndicator::SetDepth("),
                         {"if (!animate || !motion::ShouldAnimate()) {", "Settle();", "return;",
                          "tunnel::Plan(from, depth)", "motion::AnimateValue("}));
  UR_EXPECT_TRUE(InOrder(wiring::FunctionBody(indicator, "void TunnelIndicator::snapshot_vfunc("),
                         {"Cairo::Context::LineJoin::MITER", "tunnel::SquareAt(i, size)",
                          "cr->rectangle(square.x, square.y, square.side, square.side);"}));

  const std::string settings = wiring::ReadCode("SettingsPage.cpp");
  UR_EXPECT_TRUE(InOrder(settings, {"slow_internet::NotifyToggleCopy()",
                                    "prefs::Get<bool>(slow_internet::kNotifyPrefKey, true)",
                                    "prefs::Set(slow_internet::kNotifyPrefKey, on);",
                                    "on_slow_internet_notify(on);"}));
}

UR_TEST(SlowInternetWiring_TheBuildShipsTheWidgetsAndTheArt) {
  // read as it is: meson's comments are not C++'s
  const std::string build = wiring::ReadRaw("../meson.build");
  UR_EXPECT_TRUE(wiring::Contains(build, "'src/TunnelIndicator.cpp',"));
  UR_EXPECT_TRUE(wiring::Contains(build, "'src/SlowInternetPill.cpp',"));
  UR_EXPECT_TRUE(InOrder(build, {"install_data('assets/urnetwork-tray-disconnected.png',",
                                 "'assets/urnetwork-tray-slow-1.png',",
                                 "'assets/urnetwork-tray-slow-2.png',",
                                 "'assets/urnetwork-tray-slow-3.png',",
                                 "'assets/urnetwork-tray-slow-4.png',",
                                 "install_dir : get_option('datadir') / 'urnetwork/icons')"}));
}
