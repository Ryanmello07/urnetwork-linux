// Earnings › Leaderboard › Protocol as wired into the app: the Protocol tab
// first and the default, before Points and Data, each tab (the active one
// included) scrolling its list to the top; the list in pane B and the banner,
// own-status card and explainer in pane C, or on top of the list while pane C
// is folded away; the requests made on the GUI's Api (snTop, snTopMe) with
// their answers copied out on the sdk thread, posted to the main loop and
// given up on there; nothing asked without a session; the Top 200 tile on
// Earnings reading GET /sn/top/me instead of GET /sn/head, with no claim
// button while binding is closed; "Claim on Earnings" going to that tile; one
// "Show my network name" switch for both boards; a claimed row's sheet through
// the page's one-sheet gate; the sign-out and the destructor releasing the
// requests without touching a widget; and every row, the Top 200 line and the
// chart named for a screen reader. The decisions themselves are tested in
// ProtocolBoardPresentationTest and ProtocolBoardBindingTest; the board needs
// GTK and the SDK, so this reads its sources.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::CountOf;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

UR_TEST(ProtocolBoardWiring_ProtocolIsTheFirstTabAndTheDefault) {
  const std::string page = ReadCode("EarningsPage.cpp");
  const std::string ledger = FunctionBody(page, "void EarningsPage::BuildLedgerPane()");
  UR_EXPECT_TRUE(Contains(
      ledger, "Gtk::make_managed<Gtk::ToggleButton>(T_(\"leaderboard_tab_protocol\", \"Protocol\"));"));
  UR_EXPECT_TRUE(Contains(ledger, "protocolBoardTab_->set_active(true);"));
  UR_EXPECT_TRUE(Contains(
      ledger, "for (Gtk::ToggleButton* tab : {protocolBoardTab_, pointsBoardTab_, dataBoardTab_}) {"));
  UR_EXPECT_TRUE(
      Before(ledger, "T_(\"leaderboard_tab_protocol\", \"Protocol\")", "T_(\"points\", \"Points\")"));
  UR_EXPECT_TRUE(Before(ledger, "T_(\"points\", \"Points\")", "T_(\"data\", \"Data\")"));
  // no other board is the default any more
  UR_EXPECT_FALSE(Contains(ledger, "dataBoardTab_->set_active(true);"));
  // every tab, the active one included, scrolls its list to the top
  UR_EXPECT_TRUE(Contains(ledger, "tab->signal_clicked().connect([this, tab] {"));
  UR_EXPECT_TRUE(Contains(ledger, "if (tab->get_active()) ResetBoardList(tab == pointsBoardTab_);"));
  // the board's halves, and the default board's applied once both exist
  UR_EXPECT_TRUE(Contains(ledger, "leaderboardHost_->append(protocol_->ListHost());"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::BuildNetworkPane()"),
                          "content->append(protocol_->WideSide());"));
  UR_EXPECT_TRUE(
      Before(page, "append(*paneC_.root);\n  OnBoardTabChanged();", "EarningsPage::~EarningsPage()"));
  UR_EXPECT_TRUE(Contains(page, "protocol_->SetScroller(paneB_.scroller);"));
}

UR_TEST(ProtocolBoardWiring_TheBoardSwitchShowsOneBoardsHalves) {
  const std::string page = ReadCode("EarningsPage.cpp");
  const std::string tabs = FunctionBody(page, "void EarningsPage::OnBoardTabChanged()");
  UR_EXPECT_TRUE(Contains(tabs, "protocol_->ListHost().set_visible(protocol);"));
  UR_EXPECT_TRUE(Contains(tabs, "protocol_->WideSide().set_visible(protocol);"));
  UR_EXPECT_TRUE(
      Contains(tabs, "for (Gtk::Widget* widget : dataRankingWidgets_) widget->set_visible(data);"));
  UR_EXPECT_TRUE(Contains(tabs, "protocol_->SetShowing(looking && protocol);"));
  // the list is asked the first time it is looked at in a load, never per process
  UR_EXPECT_TRUE(Contains(
      tabs, "if (protocol && looking && !protocol_->ListRequested()) protocol_->LoadList();"));
  const std::string ledgerTab = FunctionBody(page, "void EarningsPage::OnLedgerTabChanged()");
  UR_EXPECT_TRUE(Contains(
      ledgerTab,
      "if (!history && protocolBoardShowing_ && !protocol_->ListRequested()) protocol_->LoadList();"));
  const std::string load = FunctionBody(page, "void EarningsPage::Load()");
  UR_EXPECT_TRUE(Contains(load, "protocol_->ForgetList();"));
  UR_EXPECT_TRUE(Contains(load, "if (protocolBoardShowing_) protocol_->LoadList();"));
  // pane C folded away: the side block goes on top of the list; and "Claim on
  // Earnings" is the one primary button only while pane A's tile (with its own
  // claim button) is folded away (mmm/DESIGNSTYLE.md "One primary button per
  // screen")
  const std::string fold = FunctionBody(page, "void EarningsPage::ApplyBreakpoint(int widthDip)");
  UR_EXPECT_TRUE(Contains(fold, "protocol_->SetCompact(lanes < 3);"));
  UR_EXPECT_TRUE(Contains(fold, "protocol_->SetClaimPrimary(lanes < 2);"));
  const std::string primary =
      FunctionBody(ReadCode("ProtocolBoard.cpp"), "void ProtocolSide::SetClaimPrimary(bool primary)");
  UR_EXPECT_TRUE(Contains(primary, "SetClass(*claimButton_, \"ur-pane-primary\", primary);"));
  UR_EXPECT_TRUE(Contains(primary, "SetClass(*claimButton_, \"ur-pane-secondary\", !primary);"));
  // the pane's count is the board's
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::ApplyLedgerMeta()"),
                          "protocolBoardShowing_ ? static_cast<int>(protocol_->List().rows.size())"));
}

// The requests go to host_.api() in the GUI process, one service per Api; an
// answer is copied out on the sdk thread and only posted, and it is handed to
// the binding only while the board is alive. The give-ups run on the main
// loop's timer, and not once the board is gone.
UR_TEST(ProtocolBoardWiring_TheRequestsAreTheGuiApisAndTheirAnswersOnlyPost) {
  const std::string board = ReadCode("ProtocolBoard.cpp");
  const std::string ensure = FunctionBody(board, "void ProtocolBoard::EnsureService()");
  UR_EXPECT_TRUE(Contains(ensure, "const uint64_t source = host_.api().handle();"));
  UR_EXPECT_TRUE(Contains(ensure, "if (binding_.Attached() && binding_.Source() == source) return;"));
  UR_EXPECT_TRUE(
      Contains(ensure, "binding_.Attach(std::make_unique<SdkService>(host_, alive_), source);"));
  UR_EXPECT_TRUE(Contains(board, "host_.api().snTop("));
  UR_EXPECT_TRUE(Contains(board, "host_.api().snTopMe("));
  UR_EXPECT_EQ(2u, CountOf(board, "PostToMain([alive, done, answer = std::move(answer)]() mutable {"));
  UR_EXPECT_EQ(2u, CountOf(board, "if (*alive) done(std::move(answer));"));
  UR_EXPECT_TRUE(Contains(
      board, "binding_(settings_, [alive = alive_](int delayMs, std::function<void()> fire) {"));
  UR_EXPECT_TRUE(Contains(board, "if (*alive) fire();"));
  UR_EXPECT_TRUE(Before(board, "ProtocolBoard::ProtocolBoard(SdkHost& host)",
                        "binding_.on_changed = [this] { Render(); };"));
  // every nullable is read through its has_ flag, never as a zero
  for (const char* has : {"s.has_claimed_cutoff", "s.has_precision_at_200", "f.has_validator_score",
                          "f.has_validator_rank", "f.has_validator_rank_of", "f.has_selected",
                          "f.has_incentive"}) {
    UR_EXPECT_TRUE_MSG(has, Contains(board, has));
  }
  // an answer without its snapshot is a failure, not an empty board
  UR_EXPECT_EQ(2u, CountOf(board, "result->snapshot.has_value();"));
  // a refused page's status reaches the binding (410: reload from the top),
  // and an expiry reload takes the view back to the top of the list
  UR_EXPECT_TRUE(Contains(board, "if (err) answer.status = pb::HttpStatusOf(*err);"));
  UR_EXPECT_TRUE(Contains(board, "binding_.on_expired = [this] { ScrollToTop(); };"));
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "void ProtocolBoard::Release()"),
                          "binding_.on_expired = nullptr;"));
  // no rankings yet is a line in the card's place, like signed out
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "void ProtocolSide::Render("),
                          "card.body == pb::CardBody::Empty"));
}

// No session: no service and no request, and the departed account's place goes.
UR_TEST(ProtocolBoardWiring_NothingIsAskedWithoutASession) {
  const std::string board = ReadCode("ProtocolBoard.cpp");
  for (const char* load : {"void ProtocolBoard::LoadMine()", "void ProtocolBoard::LoadList()"}) {
    const std::string body = FunctionBody(board, load);
    UR_EXPECT_TRUE_MSG(load, Contains(body, "if (previewMode_ || !SignedIn()) {"));
    UR_EXPECT_TRUE_MSG(load, Before(body, "binding_.Release();", "EnsureService();"));
  }
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "bool ProtocolBoard::SignedIn() const"),
                          "return can_call_api && can_call_api();"));
  const std::string page = ReadCode("EarningsPage.cpp");
  UR_EXPECT_TRUE(Contains(page, "protocol_->can_call_api = [this] { return CanCallApi(); };"));
  // the preview's sample only under the preview's own gate
  UR_EXPECT_EQ(1u, CountOf(page, "protocol_->ApplyPreviewSample();"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::ApplyPreviewSample()"),
                          "protocol_->ApplyPreviewSample();"));
}

// The Top 200 tile reads the board's own place (GET /sn/top/me), not GET
// /sn/head, and while binding is closed shows the note in place of the claim
// button.
UR_TEST(ProtocolBoardWiring_TheTop200TileReadsTheOwnPlace) {
  const std::string page = ReadCode("EarningsPage.cpp");
  UR_EXPECT_FALSE(Contains(page, "snHead("));
  UR_EXPECT_FALSE(Contains(page, "SnHeadInfo"));
  UR_EXPECT_FALSE(Contains(ReadCode("EarningsPage.hpp"), "SnHeadInfo"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::LoadEarnings()"),
                          "protocol_->LoadMine();"));
  const std::string tile = FunctionBody(page, "void EarningsPage::RebuildTop200()");
  UR_EXPECT_TRUE(Contains(
      tile,
      "protocol_board::TileFor(protocol_->Mine(), protocol_->SignedIn(), ProtocolBoardWords());"));
  UR_EXPECT_TRUE(Before(tile, "if (!tile.bindingOpen) {", "MakeGoldButton(tile.claimText);"));
  // no error state on the tile: a 404 or a failure hides it (TileFor)
  UR_EXPECT_TRUE(Before(tile, "top200Card_->set_visible(show);", "if (!show) return;"));
  UR_EXPECT_FALSE(Contains(tile, "something_went_wrong"));
  UR_EXPECT_FALSE(Contains(tile, "loadFailed"));
  UR_EXPECT_TRUE(Contains(tile, "soon->append(*MakeSizedLabel(tile.soonTitle, 14, \"ur-value\"));"));
  UR_EXPECT_TRUE(Contains(tile, "see->signal_clicked().connect([this] { ShowProtocolBoard(); });"));
  // the tile renders again when the own place answers
  UR_EXPECT_TRUE(Contains(page, "protocol_->on_mine_changed = [this] {\n    RebuildTop200();"));
  // "Claim on Earnings": the tile, or its claim link with pane A folded away
  UR_EXPECT_TRUE(Contains(page, "protocol_->on_claim = [this] { OnProtocolClaim(); };"));
  const std::string claim = FunctionBody(page, "void EarningsPage::OnProtocolClaim()");
  UR_EXPECT_TRUE(Contains(claim, "OpenLink(kTop200Url);"));
  UR_EXPECT_TRUE(
      Contains(claim, "if (top200ClaimButton_ != nullptr) top200ClaimButton_->grab_focus();"));
}

// One "Show my network name" for both boards (Q12): the Protocol card's switch
// asks through the Points board's request, and every programmatic write of it
// goes through the echo guard.
UR_TEST(ProtocolBoardWiring_OneSwitchForBothBoards) {
  const std::string page = ReadCode("EarningsPage.cpp");
  UR_EXPECT_TRUE(Contains(
      page, "protocol_->on_public = [this](bool requested) { RequestPointsPublic(requested); };"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::OnPointsPublicToggled()"),
                          "RequestPointsPublic(pointsPublicToggle_->get_active());"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::SetPointsToggle(bool on)"),
                          "if (protocol_) protocol_->SetPublic(on, !settingPointsPublic_);"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::RequestPointsPublic(bool requested)"),
                          "protocol_->SetPublic(requested, false);"));
  UR_EXPECT_TRUE(Contains(page, "protocol_->Reload();"));
  const std::string board = ReadCode("ProtocolBoard.cpp");
  const std::string set = FunctionBody(board, "void ProtocolSide::SetPublic(bool on, bool sensitive)");
  UR_EXPECT_TRUE(Before(set, "applyingSwitch_ = true;", "publicSwitch_->set_active(on);"));
  UR_EXPECT_TRUE(Contains(board, "if (!*alive || applyingSwitch_) return;"));
}

// The claimed row's sheet opens through the page's one-sheet gate and
// presenter, never over another modal.
UR_TEST(ProtocolBoardWiring_TheDetailSheetIsThePagesSheet) {
  const std::string page = ReadCode("EarningsPage.cpp");
  UR_EXPECT_TRUE(Contains(
      page,
      "protocol_->sheet_busy = [this] { return sheet_ != nullptr || (sheet_open && sheet_open()); };"));
  UR_EXPECT_TRUE(Contains(page, "PresentSheet(sheet);"));
  const std::string board = ReadCode("ProtocolBoard.cpp");
  const std::string open = FunctionBody(board, "void ProtocolBoard::OpenDetail(int64_t position)");
  UR_EXPECT_TRUE(Before(open, "if (sheet_busy && sheet_busy()) {", "present_sheet(MakeDetailSheet("));
  // only a claimed row opens one
  UR_EXPECT_TRUE(Contains(open, "if (row == list.rows.end() || row->fleets.empty()) return;"));
  UR_EXPECT_TRUE(Contains(
      FunctionBody(board, "Gtk::Widget* ProtocolBoard::MakeRow(const pb::RowView& view)"),
      "if (view.claimed) {"));
}

// A sign-out releases the departed account's requests and board; the page's
// destructor releases the board's requests and touches no widget (issue #13).
UR_TEST(ProtocolBoardWiring_SignOutAndTeardownRelease) {
  const std::string page = ReadCode("EarningsPage.cpp");
  UR_EXPECT_TRUE(Before(FunctionBody(page, "void EarningsPage::ResetForSignOut()"),
                        "protocol_->ResetForSignOut();", "Load();"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "EarningsPage::~EarningsPage()"),
                          "if (protocol_) protocol_->Release();"));
  const std::string board = ReadCode("ProtocolBoard.cpp");
  const std::string release = FunctionBody(board, "void ProtocolBoard::Release()");
  UR_EXPECT_TRUE(Contains(release, "*alive_ = false;"));
  UR_EXPECT_TRUE(Contains(release, "binding_.Release();"));
  for (const char* widgetCall :
       {"Render(", "RemoveAllChildren(", "ClearRows(", "->set_", "SetPublic("}) {
    UR_EXPECT_TRUE_MSG(widgetCall, !Contains(release, widgetCall));
  }
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "ProtocolBoard::~ProtocolBoard()"), "Release();"));
}

// Every row, the Top 200 line, the chart, the switch and the placeholders are
// named for a screen reader; the parts a name already says are hidden.
UR_TEST(ProtocolBoardWiring_EverythingIsNamedForAScreenReader) {
  const std::string board = ReadCode("ProtocolBoard.cpp");
  UR_EXPECT_TRUE(Contains(board, "kit::SetAccessibleLabel(*root, view.accessibleName);"));
  UR_EXPECT_TRUE(Contains(board, "kit::SetAccessibleLabel(*box, line.accessibleName);"));
  UR_EXPECT_TRUE(Contains(board, "kit::SetAccessibleLabel(*this, chart_.accessibleName);"));
  UR_EXPECT_TRUE(Contains(board, "kit::SetAccessibleLabel(*publicSwitch_, showName);"));
  UR_EXPECT_TRUE(Contains(board, "kit::SetAccessibleLabel(*positionBlock_, positionName);"));
  UR_EXPECT_TRUE(
      Contains(board, "kit::SetBusy(*listHost_, screen.body == pb::ListBody::Placeholder);"));
  UR_EXPECT_TRUE(Contains(board, "kit::SetBusy(*root_, placeholder);"));
  const std::string row =
      FunctionBody(board, "Gtk::Widget* ProtocolBoard::MakeRow(const pb::RowView& view)");
  UR_EXPECT_TRUE(Contains(row, "kit::MarkDecorative(*part);"));
  // the chart answers the keyboard too
  UR_EXPECT_TRUE(Contains(board, "case GDK_KEY_Left:"));
  UR_EXPECT_TRUE(Contains(board, "case GDK_KEY_Right:"));
}

// The three single lowercase words the catalog scan sees only as literal calls.
UR_TEST(ProtocolBoardWiring_TheLowercaseWordsAreLiteralLookups) {
  const std::string lookup = FunctionBody(ReadCode("ProtocolBoard.cpp"),
                                          "std::string ProtocolBoardLookup(const pb::Text& text)");
  UR_EXPECT_TRUE(Contains(lookup, "T_(\"leaderboard_protocol_projected\", \"projected\")"));
  UR_EXPECT_TRUE(Contains(lookup, "T_(\"leaderboard_protocol_detail_claimed\", \"claimed\")"));
  UR_EXPECT_TRUE(Contains(lookup, "T_(\"leaderboard_protocol_detail_stale\", \"stale\")"));
  UR_EXPECT_TRUE(Contains(lookup, "return T_(text.key, text.english);"));
}

// Every style the board names is in the brand stylesheet.
UR_TEST(ProtocolBoardWiring_EveryStyleIsInTheStylesheet) {
  const std::string theme = ReadCode("UrTheme.cpp");
  for (const char* style :
       {".ur-protocol-banner", ".ur-protocol-banner-text", ".ur-protocol-inside",
        "button.ur-pane-row.ur-protocol-inside", "button.ur-pane-row.ur-earn-own-row",
        "button.ur-protocol-row",
        ".ur-protocol-dimmed", ".ur-protocol-rule", ".ur-protocol-line", ".ur-protocol-chip",
        ".ur-protocol-chip.ur-protocol-chip-likely", ".ur-protocol-chip.ur-protocol-chip-borderline",
        ".ur-protocol-chip.ur-protocol-chip-muted", ".ur-protocol-uid", ".ur-protocol-you",
        ".ur-protocol-arrow", ".ur-protocol-borderline", ".ur-protocol-prompt"}) {
    UR_EXPECT_TRUE_MSG(style, Contains(theme, style));
  }
}

// The list's position indicator spans positions 1 to 400 (mmm/DESIGNSTYLE.md
// "Long ranked lists"): a slider to assistive tech over pane B's scroller,
// seeking by paging the list to the position; and the pane's scrollbar steps
// aside while either board's indicator shows.
UR_TEST(ProtocolBoardWiring_TheListCarriesAPositionIndicator) {
  const std::string page = ReadCode("EarningsPage.cpp");
  UR_EXPECT_TRUE(Before(page, "BuildPointsIndicator();", "protocol_->AttachIndicator(*paneBOverlay_);"));
  UR_EXPECT_TRUE(Before(page, "protocol_->AttachIndicator(*paneBOverlay_);", "append(*paneBOverlay_);"));
  UR_EXPECT_TRUE(Contains(page, "protocol_->on_indicator_changed = [this] { ApplyPaneBScrollPolicy(); };"));
  const std::string policy = FunctionBody(page, "void EarningsPage::ApplyPaneBScrollPolicy()");
  UR_EXPECT_TRUE(Contains(policy, "(protocol_ && protocol_->IndicatorShowing())"));
  UR_EXPECT_TRUE(Contains(policy, "pointsIndicator_->get_visible()"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void EarningsPage::UpdatePointsIndicator()"),
                          "ApplyPaneBScrollPolicy();"));
  UR_EXPECT_FALSE(Contains(FunctionBody(page, "void EarningsPage::UpdatePointsIndicator()"),
                           "set_policy("));
  const std::string board = ReadCode("ProtocolBoard.cpp");
  const std::string attach = FunctionBody(board, "void ProtocolBoard::AttachIndicator(Gtk::Overlay& overlay)");
  UR_EXPECT_TRUE(Contains(attach, "GTK_ACCESSIBLE_ROLE_SLIDER"));
  UR_EXPECT_TRUE(Contains(attach, "T_(\"leaderboard_position_indicator\", \"Leaderboard position\")"));
  // the total is the top 400, or fewer when fewer are ranked
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "int64_t ProtocolBoard::IndicatorTotal() const"),
                          "std::min<int64_t>(list.snapshot->totalRanked, pb::kListPositions)"));
  // a seek pages the list to the position, and a release or a key seeks
  const std::string seek = FunctionBody(board, "void ProtocolBoard::Seek(int64_t position)");
  UR_EXPECT_TRUE(Contains(seek, "*seekTarget_ + VisibleRows()"));
  UR_EXPECT_TRUE(Contains(seek, "binding_.LoadThrough(through)"));
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "void ProtocolBoard::OnDragEnd(double dy)"),
                          "Seek(dragPosition_);"));
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "bool ProtocolBoard::OnIndicatorKey(guint keyval)"),
                          "Seek(position);"));
  UR_EXPECT_TRUE(Contains(
      board, "ScrollToPosition(seekTarget_ ? std::min(*seekTarget_, reached) : reached);"));
  // the seek lands from the frame clock, which sees the finished layout (the
  // scroll range's own signal comes before the rows are placed), and never
  // touches a board that is gone
  const std::string scroll = FunctionBody(board, "void ProtocolBoard::ScrollToPosition(int64_t position)");
  UR_EXPECT_TRUE(Contains(scroll, "scroller_->add_tick_callback("));
  UR_EXPECT_TRUE(Before(scroll, "if (!*alive) return false;", "++pendingFrames_"));
  // no page is asked before the pane has a size
  UR_EXPECT_TRUE(Before(FunctionBody(board, "void ProtocolBoard::MaybeLoadMore()"),
                        "if (adjustment->get_page_size() <= 0) return;", "binding_.LoadMore();"));
  // the slider's value: the position, with its tier, as the Points board says it
  UR_EXPECT_TRUE(Contains(FunctionBody(board, "void ProtocolBoard::UpdateIndicator()"),
                          "GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT, valueText.c_str()"));
}
