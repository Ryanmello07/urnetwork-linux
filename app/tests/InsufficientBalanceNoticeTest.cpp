// The out-of-balance notice: the drawer banner text and the once-per-episode
// desktop notification (urnetwork/android#483). Every case drives the tracker
// through an explicit sequence of readings, so the expected calls are exact.
//
// SPDX-License-Identifier: MPL-2.0
#include "Health.hpp"
#include "InsufficientBalanceNotice.hpp"
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::balance_notice::Banner;
using urnw::balance_notice::Signals;
using urnw::balance_notice::Tracker;

// Counts every call. Disconnect is here only to prove the tracker never asks
// for one: the notice informs, the user's own Disconnect is the way out.
struct CountingSink {
  int posts = 0;
  int withdraws = 0;
  int disconnects = 0;
  void Post() { ++posts; }
  void Withdraw() { ++withdraws; }
  void Disconnect() { ++disconnects; }
};

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

Signals Held() {
  Signals s;
  s.insufficientBalance = true;
  s.connectRequested = true;
  return s;
}

// The harness's EXPECT_EQ is numeric; strings compare here with both values in
// the failure.
void ExpectText(const char* what, const std::string& expected, const char* actual) {
  if (expected != actual) {
    UR_FAIL(std::string(what) + ": \"" + actual + "\", expected \"" + expected + "\"");
  }
}

Signals Funded() {
  Signals s;
  s.connectRequested = true;
  return s;
}

}  // namespace

UR_TEST(balanceNoticePostsOncePerEpisode) {
  Tracker tracker;
  CountingSink sink;
  tracker.Observe(Funded(), sink);
  UR_EXPECT_EQ(0, sink.posts);
  tracker.Observe(Held(), sink);
  UR_EXPECT_EQ(1, sink.posts);
  UR_EXPECT_TRUE(tracker.Shown());
  // the feed re-emits the same state many times a second: still one post
  for (int i = 0; i < 10; ++i) tracker.Observe(Held(), sink);
  UR_EXPECT_EQ(1, sink.posts);
  UR_EXPECT_EQ(0, sink.withdraws);
  UR_EXPECT_EQ(0, sink.disconnects);
}

UR_TEST(balanceNoticeReArmsAfterTheEpisodeEnds) {
  Tracker tracker;
  CountingSink sink;
  tracker.Observe(Held(), sink);
  tracker.Observe(Funded(), sink);
  UR_EXPECT_EQ(1, sink.withdraws);
  UR_EXPECT_FALSE(tracker.Shown());
  tracker.Observe(Held(), sink);
  UR_EXPECT_EQ(2, sink.posts);
  UR_EXPECT_EQ(0, sink.disconnects);
}

UR_TEST(balanceNoticeNeverPostsForProOrWhilePolling) {
  Tracker tracker;
  CountingSink sink;
  Signals pro = Held();
  pro.pro = true;
  Signals polling = Held();
  polling.polling = true;
  for (int i = 0; i < 3; ++i) {
    tracker.Observe(pro, sink);
    tracker.Observe(polling, sink);
  }
  UR_EXPECT_EQ(0, sink.posts);
  // a poll at entry defers the post; it lands once the poll settles unfunded
  tracker.Observe(Held(), sink);
  UR_EXPECT_EQ(1, sink.posts);
  UR_EXPECT_EQ(0, sink.disconnects);
}

UR_TEST(balanceNoticeWithdrawsOnDisconnectAndDoesNotRePost) {
  Tracker tracker;
  CountingSink sink;
  tracker.Observe(Held(), sink);
  Signals disconnected = Held();
  disconnected.connectRequested = false;
  tracker.Observe(disconnected, sink);
  UR_EXPECT_EQ(1, sink.withdraws);
  UR_EXPECT_FALSE(tracker.Shown());
  // reconnecting within the same episode does not post again
  tracker.Observe(Held(), sink);
  UR_EXPECT_EQ(1, sink.posts);
  // and a disconnected start never posts at all
  Tracker idle;
  CountingSink idleSink;
  idle.Observe(disconnected, idleSink);
  UR_EXPECT_EQ(0, idleSink.posts);
  UR_EXPECT_EQ(0, idleSink.withdraws);
  UR_EXPECT_EQ(0, sink.disconnects + idleSink.disconnects);
}

UR_TEST(balanceNoticeBannerSaysTrafficIsHeldWhileConnectRequested) {
  ExpectText("held key", "insufficient_balance_held_notice", Banner(Held()).key);
  ExpectText("held text", "Your traffic is held in the tunnel until you upgrade or disconnect.",
             Banner(Held()).english);
  Signals idle = Held();
  idle.connectRequested = false;
  ExpectText("not requested", "insufficient_balance_message", Banner(idle).key);
  Signals pro = Held();
  pro.pro = true;
  ExpectText("pro", "insufficient_balance_message", Banner(pro).key);
  Signals polling = Held();
  polling.polling = true;
  ExpectText("polling", "insufficient_balance_message", Banner(polling).key);
}

UR_TEST(balanceNoticeGateKeepsDisconnectAsTheAction) {
  // the notice tells the user to disconnect, so the button must offer it
  urnw::health::Signals s;
  s.sdk = urnw::health::SdkStatus::Connected;
  s.destinationSelected = true;
  s.tunnelBound = true;
  s.insufficientBalance = true;
  UR_EXPECT_TRUE(urnw::health::Render(s).action == urnw::health::Action::Disconnect);
}

UR_TEST(balanceNoticeIsWiredIntoTheWindowAndTheDrawer) {
  // a tracker nothing feeds is the defect again, so the call sites are read as
  // text (MainWindow and ConnectDrawer need GTK and the SDK)
  const std::string window = ReadSource("MainWindow.cpp");
  const std::string drawer = ReadSource("ConnectDrawer.cpp");
  const std::string app = ReadSource("main.cpp");
  UR_EXPECT_TRUE(Has(window, "balanceNotice_.Observe("));
  UR_EXPECT_TRUE(Has(window, "send_notification("));
  UR_EXPECT_TRUE(Has(window, "withdraw_notification("));
  UR_EXPECT_TRUE(Has(window, "ToggleConnect(/*disconnect=*/true)"));
  UR_EXPECT_TRUE(Has(drawer, "balance_notice::Banner("));
  UR_EXPECT_TRUE(Has(app, "kBalanceNoticeDisconnectAction"));
}

UR_TEST(balanceNoticeHeldAlertTable) {
  // gate x connect requested x pro x polling: the alert (and its Upgrade and
  // Disconnect) shows only for an unfunded, non-Pro, settled account whose
  // tunnel is asked to carry traffic
  for (int bits = 0; bits < 32; ++bits) {
    Signals s;
    s.insufficientBalance = bits & 1;
    s.connectRequested = bits & 2;
    s.pro = bits & 4;
    s.polling = bits & 8;
    const bool expected = s.insufficientBalance && s.connectRequested && !s.pro && !s.polling;
    if (urnw::balance_notice::HeldAlert(s) != expected) {
      UR_FAIL("held alert wrong for case " + std::to_string(bits));
    }
    // the banner text and the alert are one decision
    const bool bannerHeld = std::string(Banner(s).key) == "insufficient_balance_held_notice";
    if (bannerHeld != expected) {
      UR_FAIL("banner disagrees with the alert, case " + std::to_string(bits));
    }
  }
}

UR_TEST(balanceNoticeHeldAlertIsOnTheReachableConnectPage) {
  // the drawer banner lives on the legacy column the navigation cannot reach;
  // the Connect page the user sees must host the alert and both ways out
  const std::string page = ReadSource("ConnectPage.cpp");
  const std::string window = ReadSource("MainWindow.cpp");
  const size_t alertAt = page.find("heldAlert_ = Gtk::make_managed");
  UR_EXPECT_TRUE(alertAt != std::string::npos);
  if (alertAt != std::string::npos) {
    const size_t endAt = page.find("paneAContent_->append(*heldAlert_)", alertAt);
    const std::string block = page.substr(alertAt, endAt - alertAt);
    UR_EXPECT_TRUE(Has(block, "T_(\"insufficient_balance_held_notice\""));
    UR_EXPECT_TRUE(Has(block, "T_(\"upgrade\", \"Upgrade\")"));
    UR_EXPECT_TRUE(Has(block, "T_(\"disconnect\", \"Disconnect\")"));
    UR_EXPECT_TRUE(Has(block, "on_open_upgrade()"));
    UR_EXPECT_TRUE(Has(block, "on_balance_disconnect()"));
    // always enabled, and never the connect path
    UR_EXPECT_FALSE(Has(block, "set_sensitive"));
    UR_EXPECT_FALSE(Has(block, "RelayConnectPress"));
  }
  UR_EXPECT_TRUE(Has(page, "heldAlert_->set_visible(balance_notice::HeldAlert(signals))"));
  UR_EXPECT_TRUE(Has(window, "connectPage_->ApplyBalanceNotice(signals)"));
  UR_EXPECT_TRUE(Has(window, "on_balance_disconnect = [this] { DisconnectFromBalanceNotice(); }"));
  UR_EXPECT_TRUE(Has(window, "connectPage_->on_open_upgrade"));
}
