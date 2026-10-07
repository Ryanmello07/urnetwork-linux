// The connect page's activity pane carries Windows' two status rows under the
// transport bar: the ip family status row (connect/IPV6.md D2), fed by the
// stats push's provider grid, and the extender panel (EXTENDER.md K4), fed by
// the device's extender status. Both are plain rows, always shown: with no
// session the row reads three "disconnected" columns and the panel the
// disconnected network. The rules themselves are IpFamilyStatusTest.cpp's and
// ExtenderStatusPresentationTest.cpp's; the page, the panel and the host need
// GTK and the SDK, so this reads their sources. So do the routing-decision
// rows under them, which are reconciled in place (KeyedReconcileTest.cpp has
// the plan), aged by the clock, and filtered by verdict and search
// (ConnectionFilterTest.cpp has the rules).
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadActivitySource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it.
std::string ActivityBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Mentions(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Every needle occurs, in this order.
bool InSequence(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// Windows' order: the remote chart, the transport bar, the ip family status
// row, the extender panel, then the connections group.
UR_TEST(ConnectActivity_TheTwoRowsSitUnderTheTransportBar) {
  const std::string pane =
      ActivityBody(ReadActivitySource("ConnectPage.cpp"), "void ConnectPage::BuildPaneB()");
  UR_EXPECT_TRUE(InSequence(pane, {"paneB_.content->append(*chartRow);",
                                   "paneB_.content->append(*transportRow);",
                                   "ipFamilyStatusRow_ = Gtk::make_managed<IpFamilyStatusRow>();",
                                   "paneB_.content->append(*ipFamilyStatusRow_);",
                                   "extenderPanel_ = Gtk::make_managed<ExtenderPanel>();",
                                   "paneB_.content->append(*extenderPanel_);",
                                   "T_(\"connections\", \"Connections\")"}));
  // nothing of either row comes before the transport bar
  const size_t transport = pane.find("paneB_.content->append(*transportRow);");
  UR_EXPECT_TRUE(transport != std::string::npos);
  UR_EXPECT_TRUE(pane.find("ipFamilyStatusRow_") > transport);
  UR_EXPECT_TRUE(pane.find("extenderPanel_") > transport);
  // plain rows: neither is a button or opens anything
  UR_EXPECT_TRUE(!Mentions(pane, "ipFamilyStatusRow_->on_"));
  UR_EXPECT_TRUE(!Mentions(pane, "extenderPanel_->on_"));
}

// The row rides the stats push's grid, the panel the extender status: on its
// own event, and re-read on the page's clock, where a device that went away is
// noticed.
UR_TEST(ConnectActivity_TheRowsAreFedTheirFeeds) {
  const std::string page = ReadActivitySource("ConnectPage.cpp");
  UR_EXPECT_TRUE(Mentions(ActivityBody(page, "void ConnectPage::ApplyStats("),
                          "ipFamilyStatusRow_->SetGrid(stats.gridPoints);"));
  const std::string events = ActivityBody(page, "void ConnectPage::OnHostEvent(");
  UR_EXPECT_TRUE(InSequence(events, {"case DrawerEvent::ExtenderStatus:",
                                     "extenderPanel_->SetStatus(host_.GetExtenderStatus());",
                                     "break;"}));
  UR_EXPECT_TRUE(Mentions(ActivityBody(page, "void ConnectPage::RefreshFeeds("),
                          "extenderPanel_->SetStatus(host_.GetExtenderStatus());"));
}

// The host raises the event from the device's own listener and reads the
// device's status, with nothing to read without a device.
UR_TEST(ConnectActivity_TheHostRaisesTheExtenderStatus) {
  const std::string host = ReadActivitySource("SdkHost.cpp");
  UR_EXPECT_TRUE(InSequence(host, {"device_->addExtenderStatusChangeListener(",
                                   "EmitDrawerEvent(DrawerEvent::ExtenderStatus);"}));
  const std::string read =
      ActivityBody(host, "std::optional<urnet::ExtenderStatus> SdkHost::GetExtenderStatus()");
  UR_EXPECT_TRUE(InSequence(read, {"if (!device_) return std::nullopt;",
                                   "return device_->getExtenderStatus();"}));
  UR_EXPECT_TRUE(Mentions(ReadActivitySource("SdkHost.hpp"), "ExtenderStatus,"));
}

// Windows' visibility rule: the panel is always shown, and "0 of 0" is faint.
UR_TEST(ConnectActivity_ThePanelNeverHides) {
  const std::string panel = ReadActivitySource("ExtenderPanel.cpp");
  const std::string render = ActivityBody(panel, "void ExtenderPanel::Render()");
  UR_EXPECT_TRUE(!render.empty());
  UR_EXPECT_TRUE(!Mentions(render, "set_visible(panel_"));
  UR_EXPECT_TRUE(!Mentions(render, "set_visible(false)"));
  UR_EXPECT_TRUE(Mentions(render, "panel_.countFaint() ? \"ur-label-faint\" : \"dim-label\""));
  // the dot uses the connect status line's colors
  UR_EXPECT_TRUE(Mentions(panel, "case extender::StatusDot::Yellow: return kUrYellow;"));
}

// The decision rows are reconciled in place by key: only the Advanced Mode
// flip, which changes the row type, still clears the list. A push rewrites
// the rows it keeps, so an Advanced row keeps its focus and hover.
UR_TEST(ConnectActivity_TheRowsReconcileInPlace) {
  const std::string page = ReadActivitySource("ConnectPage.cpp");
  const std::string list = ActivityBody(page, "void ConnectPage::ApplyConnectionsList(");
  UR_EXPECT_TRUE(InSequence(list, {"if (connectionRowsSelectable_ != advanced_) {",
                                   "RemoveAllChildren(*connectionsHost_);",
                                   "connectionRowsSelectable_ = advanced_;", "}",
                                   "reconcile::Plan(onScreen, keys)"}));
  const size_t clear = list.find("RemoveAllChildren(");
  UR_EXPECT_TRUE(clear != std::string::npos);
  UR_EXPECT_TRUE(list.find("RemoveAllChildren(", clear + 1) == std::string::npos);
  UR_EXPECT_TRUE(Mentions(list, "case reconcile::StepKind::Update:"));
  UR_EXPECT_TRUE(Mentions(list, "UpdateConnectionRow(*at, *visible[step.wantedIndex]);"));
  UR_EXPECT_TRUE(Mentions(list, "connectionsHost_->remove(*at->root);"));
  UR_EXPECT_TRUE(Mentions(list, "connectionsHost_->reorder_child_after("));
  UR_EXPECT_TRUE(Mentions(list, "connectionsHost_->insert_child_after("));
  // the key: the decision's id, or its time and title when it has none
  UR_EXPECT_TRUE(InSequence(ActivityBody(page, "std::string ConnectionRowKey("),
                            {"return \"a:\" + *action.BlockActionId;",
                             "return \"t:\" + std::to_string(action.Time)"}));
}

// The meta line leads with the decision's age, which the 1s clock re-renders
// from the row's own counters without a feed push.
UR_TEST(ConnectActivity_TheClockAgesTheRows) {
  const std::string page = ReadActivitySource("ConnectPage.cpp");
  UR_EXPECT_TRUE(InSequence(ActivityBody(page, "std::string ConnectionRowMeta("),
                            {"if (0 < timeMs) meta = RelativeTime((nowMs - timeMs) / 1000)",
                             "FormatByteCountCompact(byteCount)",
                             "FormatCountCompact(packetCount)"}));
  UR_EXPECT_TRUE(InSequence(ActivityBody(page, "void ConnectPage::Tick()"),
                            {"if (tickCount_ % 10 == 0) {", "RefreshConnectionRowTimes();"}));
  const std::string ages = ActivityBody(page, "void ConnectPage::RefreshConnectionRowTimes()");
  UR_EXPECT_TRUE(Mentions(ages, "WriteConnectionRowMeta(row, nowMs);"));
  UR_EXPECT_TRUE(!Mentions(ages, "host_."));
}

// Under the Connections header: the verdict segments, then the search row,
// then the list; Clear rides the header's trailing slot.
UR_TEST(ConnectActivity_TheFilterRowsSitOverTheList) {
  const std::string pane =
      ActivityBody(ReadActivitySource("ConnectPage.cpp"), "void ConnectPage::BuildPaneB()");
  UR_EXPECT_TRUE(InSequence(pane, {"T_(\"connections\", \"Connections\")",
                                   "connectionsClear_ = Gtk::make_managed<Gtk::Button>(T_(\"clear\", \"Clear\"));",
                                   "connectionsHeader.trailing->append(*connectionsClear_);",
                                   "T_(\"adv_filter_all\", \"All\")",
                                   "T_(\"blocked\", \"Blocked\")",
                                   "T_(\"adv_filter_tunnelled\", \"Tunnelled\")",
                                   "T_(\"adv_filter_bypassed\", \"Bypassed\")",
                                   "kit::MakePaneSearchRow(T_(\"adv_search_connections\"",
                                   "paneB_.content->append(*connectionsScroll_);"}));
  // a segment's or the field's own handler stands down under the echo guard
  UR_EXPECT_TRUE(InSequence(pane, {"if (updatingControls_ || !button->get_active()) return;",
                                   "OnConnectionsVerdictChanged(verdict);"}));
  UR_EXPECT_TRUE(InSequence(pane, {"connectionsSearch_->signal_changed().connect(",
                                   "if (updatingControls_) return;",
                                   "connection_filter::NormalizeQuery(",
                                   "ApplyConnectionsList(/*resetScroll=*/true);"}));
}

// The filter runs over the cached feed inside the one pass, before the cap,
// and a filter change reads its result from the top.
UR_TEST(ConnectActivity_TheFilterRunsInsideTheReconcile) {
  const std::string page = ReadActivitySource("ConnectPage.cpp");
  const std::string list = ActivityBody(page, "void ConnectPage::ApplyConnectionsList(");
  UR_EXPECT_TRUE(InSequence(list, {"connection_filter::Passes(verdictFilter_, connectionsQuery_,",
                                   "++passed;",
                                   "if (visible.size() >= kMaxConnectionRows) continue;",
                                   "reconcile::Plan(onScreen, keys)",
                                   "if (resetScroll && connectionsScroll_)",
                                   "connection_filter::CountFor(",
                                   "T_(\"of_total\", \"of {}\")",
                                   "connectionsClear_->set_visible(filtered);"}));
  UR_EXPECT_TRUE(!Mentions(list, "host_."));
  UR_EXPECT_TRUE(Mentions(ActivityBody(page, "void ConnectPage::ApplySessionCardsVisibility()"),
                          "connection_filter::ShowList("));
}

// Clear puts the verdict and the search back behind the echo guard, so the
// controls' own handlers stay quiet, and runs the pass once.
UR_TEST(ConnectActivity_ClearResetsEverythingInOnePass) {
  const std::string clear = ActivityBody(ReadActivitySource("ConnectPage.cpp"),
                                         "void ConnectPage::OnConnectionsClearFilters()");
  UR_EXPECT_TRUE(InSequence(clear, {"verdictFilter_ = connection_filter::Verdict::All;",
                                    "connectionsQuery_.clear();",
                                    "updatingControls_ = true;",
                                    "verdictAll_->set_active(true);",
                                    "connectionsSearch_->set_text(\"\");",
                                    "updatingControls_ = wasUpdating;",
                                    "ApplyConnectionsList(/*resetScroll=*/true);"}));
  const size_t pass = clear.find("ApplyConnectionsList(");
  UR_EXPECT_TRUE(pass != std::string::npos &&
                 clear.find("ApplyConnectionsList(", pass + 1) == std::string::npos);
}
