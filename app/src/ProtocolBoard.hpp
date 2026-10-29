// Earnings › Leaderboard › Protocol (apple/LEADERTOP.md; owner decision
// 2026-10-29): the board the SN25 validator would pick its Top 200 from, and
// the caller's own place on it. The Leaderboard tab's first board and its
// default, before Points and Data.
//
// Two halves, laid out as the window allows (mmm/DESIGNSTYLE.md "Layout
// follows the window", "Split layouts"), the way the Points board puts its
// own-stats block beside its rows:
//
//   - ListHost (pane B): the ranked list, the top 400 paged 50 at a time as
//     the pane scrolls, with the Top 200 line between 200 and 201, the rows
//     inside it tinted, UID badges on claimed rows, unmeasurable rows dimmed
//     with a small icon, and the caller's row highlighted with "You" and its
//     label chip. A claimed row opens its detail sheet: each fleet's UID,
//     generation and claim window, the validator's rank, score and selection,
//     the on-chain incentive ("stale" when flagged) and the board's replica
//     score.
//   - The side block: the status banner (claiming not open yet; rankings
//     last updated), the own-status card (tag and name or "Anonymous" with
//     the "Show my network name" switch shared with Points; "#173 projected
//     of N ranked networks"; the margin to the line with the trend; the label
//     chip; the 90-day progress chart with the 200 line; why this score; the
//     claim prompt; why the network is not measurable) and the explainer. In
//     pane C beside the list when the window is wide enough for it
//     (WideSide), else on top of the list (SetCompact), as the phone layout
//     stacks them. It is built twice, one copy per place, and both render the
//     same state.
//
// Every surface settles on placeholders, its content, its empty line or its
// failure with a retry; a failed refresh keeps the rows under a notice. The
// rules are ProtocolBoardPresentation.hpp and the requests
// ProtocolBoardBinding.hpp (the sdk's Api snTop and snTopMe, answered on the
// main loop, given up on after 20 s); this file is the GTK around them.
// EarningsPage owns the board, hands it the pane B scroller, and reads its
// own place for the Top 200 tile on Earnings.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtkmm.h>

#include "LeaderboardIndicator.hpp"
#include "ProtocolBoardBinding.hpp"
#include "ProtocolBoardPresentation.hpp"
#include "SdkHost.hpp"

namespace urnw {

// The words the board and the Top 200 tile look up: the catalog, the store's
// {} substitution and the locale's dates.
protocol_board::Words ProtocolBoardWords();
// One store word, looked up in the catalog.
std::string ProtocolBoardLookup(const protocol_board::Text& text);
// A label chip ("Estimate", "Borderline", ...) in its label's tone; the
// Earnings tile shows the same one.
Gtk::Label* MakeProtocolLabelChip(protocol_board::Label label, const std::string& text);

class ProtocolRankChart;

// The banner, the own-status card and the explainer: one copy per place the
// window can put them. `withHeader`: the copy in pane C opens with the
// board's group strip; the one on top of the list sits under the board's tab.
class ProtocolSide {
 public:
  ProtocolSide(std::shared_ptr<bool> alive, bool withHeader);

  Gtk::Box* root() const { return root_; }

  void Render(const std::vector<std::string>& banner, const protocol_board::Card& card,
              const std::string& explainer);
  // The "Show my network name" switch, written without its handler running
  // (the echo guard); insensitive while a change of it is out.
  void SetPublic(bool on, bool sensitive);
  void SetClaimPrimary(bool primary);

  std::function<void()> on_claim;                 // "Claim on Earnings"
  std::function<void(bool requested)> on_public;  // the switch, flipped by the user
  std::function<void()> on_retry;                 // the card's failed load

 private:
  void Build(bool withHeader);
  void ShowPoint(std::optional<size_t> index);

  std::shared_ptr<bool> alive_;
  Gtk::Box* root_ = nullptr;
  Gtk::Widget* bannerRow_ = nullptr;
  Gtk::Label* bannerText_ = nullptr;
  // the card's bodies: placeholders, a line (signed out, failed, not ranked),
  // or the card
  Gtk::Widget* placeholderRow_ = nullptr;
  Gtk::Widget* lineRow_ = nullptr;
  Gtk::Label* lineText_ = nullptr;
  Gtk::Button* retry_ = nullptr;
  Gtk::Box* card_ = nullptr;
  Gtk::Label* tag_ = nullptr;
  Gtk::Label* name_ = nullptr;
  Gtk::Switch* publicSwitch_ = nullptr;
  bool applyingSwitch_ = false;  // ECHO GUARD on the switch
  Gtk::Box* positionBlock_ = nullptr;
  Gtk::Label* position_ = nullptr;
  Gtk::Label* projected_ = nullptr;
  Gtk::Label* ofRanked_ = nullptr;
  Gtk::Label* label_ = nullptr;
  Gtk::Label* margin_ = nullptr;
  Gtk::Label* trend_ = nullptr;
  Gtk::Widget* chartRow_ = nullptr;
  Gtk::Label* chartTitle_ = nullptr;
  ProtocolRankChart* chart_ = nullptr;
  Gtk::Label* chartPoint_ = nullptr;
  Gtk::Label* why_ = nullptr;
  Gtk::Label* solo_ = nullptr;
  Gtk::Widget* notMeasurableRow_ = nullptr;
  Gtk::Label* notMeasurable_ = nullptr;
  Gtk::Widget* promptRow_ = nullptr;
  Gtk::Label* promptHeadline_ = nullptr;
  Gtk::Button* claimButton_ = nullptr;
  Gtk::Label* promptSoon_ = nullptr;
  Gtk::Label* promptBorderline_ = nullptr;
  Gtk::Label* promptWarning_ = nullptr;
  Gtk::Label* explainer_ = nullptr;
  protocol_board::Chart chartShown_;
};

class ProtocolBoard {
 public:
  explicit ProtocolBoard(SdkHost& host);
  ~ProtocolBoard();
  ProtocolBoard(const ProtocolBoard&) = delete;
  ProtocolBoard& operator=(const ProtocolBoard&) = delete;

  // Pane B's half (the compact side block, then the list) and pane C's.
  Gtk::Box& ListHost() { return *listHost_; }
  Gtk::Box& WideSide() { return *wide_->root(); }

  // The pane B scroller the list scrolls in: the next page is asked for near
  // its end, a seek lands on its row, and the tab reset scrolls it to the top.
  void SetScroller(Gtk::ScrolledWindow* scroller);
  // The window is too narrow for pane C: the side block goes on top of the list.
  void SetCompact(bool compact);
  // The list is on screen (the Leaderboard tab on its Protocol board).
  void SetShowing(bool showing);

  // The caller's own place (the card and the Top 200 tile): on every load.
  void LoadMine();
  // The list: the first time the board is looked at after a load (a one-shot
  // per LOOK, never per process).
  void LoadList();
  bool ListRequested() const { return listRequested_; }
  // A load of the page: the list is asked again on its next look.
  void ForgetList() { listRequested_ = false; }
  // A sign-out: the departed account's requests, board and place go.
  void ResetForSignOut();
  // The destructor's half: the requests only, no widget (EarningsPage's
  // teardown runs after the window disposed the widgets, issue #13).
  void Release();
  // The tab activated, the active one included: the list to its top.
  void ScrollToTop();

  // The draggable position indicator over the list's positions, 1 to 400
  // (mmm/DESIGNSTYLE.md "Long ranked lists"; the math LeaderboardIndicator.hpp),
  // floating at the right edge of pane B's overlay while the list is longer
  // than the pane. Releasing the thumb (or a key on it) pages the list to that
  // position and scrolls there; the rank and tier show beside the thumb while
  // it is dragged.
  void AttachIndicator(Gtk::Overlay& overlay);
  bool IndicatorShowing() const { return indicatorShown_; }
  // The indicator showed or hid: the pane's scrollbar follows (the indicator
  // is the list's scrollbar while it shows).
  std::function<void()> on_indicator_changed;

  // Signed in, as the board asks its questions (the preview's sample is).
  bool SignedIn() const;
  const protocol_board::MeState& Mine() const { return binding_.Mine(); }
  const protocol_board::ListState& List() const { return binding_.List(); }
  // Whether the caller's name shows on the boards, as its own place says
  // (nullopt before that lands, or when the network is not ranked).
  std::optional<bool> PublicName() const;
  // The shared "Show my network name" switch, as EarningsPage holds it.
  void SetPublic(bool on, bool sensitive);
  // "Claim on Earnings" is the screen's one primary button only while the
  // Earnings tile it goes to is off screen (pane A folded away); beside the
  // tile and its own claim button it is secondary.
  void SetClaimPrimary(bool primary);
  // The list and the own place again (after the name's visibility changed).
  void Reload();

  // ---- preview harness (--preview-ui) -------------------------------------
  void SetPreviewMode(bool on) { previewMode_ = on; }
  // The no-session preview: the signed-out board.
  void ShowPreviewState();
  // URNETWORK_PREVIEW_SAMPLE: obviously synthetic rows, answered through the
  // same binding and rendering. URNETWORK_PREVIEW_PROTOCOL=open|unmeasurable|
  // claimed|stale|empty|failed picks the state (default: claiming closed,
  // #173); URNETWORK_PREVIEW_PROTOCOL_LINE=1 scrolls to the Top 200 line;
  // URNETWORK_PREVIEW_PROTOCOL_DETAIL=1 opens a claimed row's sheet.
  void ApplyPreviewSample();
  // A seek: the list pages a screenful past `position` (so that row can sit
  // at the top of the pane) and scrolls it there. The position indicator's
  // release and keys seek; so does the preview.
  void Seek(int64_t position);

  // ---- the page's routing --------------------------------------------------
  std::function<bool()> can_call_api;  // EarningsPage::CanCallApi
  std::function<void()> on_claim;      // "Claim on Earnings"
  std::function<void(bool requested)> on_public;  // the shared switch
  std::function<void()> on_mine_changed;  // the Top 200 tile renders again
  std::function<void()> on_list_changed;  // the pane's count renders again
  // The page's one-sheet gate and presenter (EarningsPage::PresentSheet).
  std::function<bool()> sheet_busy;
  std::function<void(const std::shared_ptr<Gtk::Window>&)> present_sheet;

 private:
  void EnsureService();
  void Render();
  void RenderList(const protocol_board::Words& words, const protocol_board::ListState& list,
                  const protocol_board::ListScreen& screen);
  Gtk::Widget* MakeRow(const protocol_board::RowView& view);
  Gtk::Widget* MakeLine(const protocol_board::Line& line);
  void BuildList();
  void ClearRows();
  void MaybeLoadMore();
  void ScrollToPosition(int64_t position);
  // Lands a seek's row at the top of the pane: true once it did (or nothing
  // is pending); false while the row or the scroll range is not laid out yet.
  bool ApplyPendingScroll(bool last);
  void OpenDetail(int64_t position);
  // the position indicator
  int64_t IndicatorTotal() const;
  int64_t VisibleRows() const;
  void RefreshPosition();
  void UpdateIndicator();
  leaderboard::Thumb IndicatorThumb(double& trackTop, double& trackHeight) const;
  void DrawIndicator(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
  void OnDragBegin(double y, const Glib::RefPtr<Gtk::GestureDrag>& drag);
  void OnDragUpdate(double dy);
  void OnDragEnd(double dy);
  bool OnIndicatorKey(guint keyval);
  void ShowDragLabel(int64_t position);
  void HideDragLabel(bool fade);
  void RetryList();
  void RetryMine();

  SdkHost& host_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  protocol_board::Settings settings_ = protocol_board::DefaultSettings();
  protocol_board::Binding binding_;
  bool previewMode_ = false;
  bool samplePinned_ = false;
  bool compact_ = false;
  bool showing_ = false;
  bool listRequested_ = false;
  // the sample's sign-in (the preview has no session behind it)
  std::optional<bool> sampleSignedIn_;

  Gtk::Box* listHost_ = nullptr;
  Gtk::Box* compactSlot_ = nullptr;
  std::unique_ptr<ProtocolSide> compactSide_;
  std::unique_ptr<ProtocolSide> wide_;
  Gtk::Widget* notice_ = nullptr;  // a failed refresh, over the rows kept
  Gtk::Box* rows_ = nullptr;
  Gtk::Box* placeholders_ = nullptr;
  Gtk::Box* statusBox_ = nullptr;  // the line and its retry, centred together
  Gtk::Label* status_ = nullptr;
  Gtk::Button* retry_ = nullptr;
  Gtk::Spinner* footerSpinner_ = nullptr;
  Gtk::Button* footerRetry_ = nullptr;
  Gtk::ScrolledWindow* scroller_ = nullptr;
  sigc::connection scrollConn_;
  sigc::connection rangeConn_;
  // what the rows were drawn from: the snapshot and each row's reading, so a
  // page that extends them is appended and anything else redraws them
  std::string drawnSnapshot_;
  std::vector<std::string> drawnRows_;
  bool lineDrawn_ = false;
  std::vector<std::pair<int64_t, Gtk::Widget*>> rowWidgets_;  // position -> row, for a seek
  // the position indicator
  Gtk::DrawingArea* indicator_ = nullptr;
  Gtk::Box* indicatorLabel_ = nullptr;
  Gtk::Label* indicatorRank_ = nullptr;
  Gtk::Label* indicatorTier_ = nullptr;
  bool indicatorShown_ = false;
  bool dragging_ = false;
  double dragStartTop_ = 0;  // the thumb's top when the drag began
  double dragTop_ = 0;       // the thumb's top while dragging
  int64_t dragPosition_ = 0;
  int64_t firstVisible_ = 1;  // the first row in view
  uint64_t labelGen_ = 0;     // cancels a fade the next show overtakes
  // the position a seek asked for, while the list pages past it
  std::optional<int64_t> seekTarget_;
  // a seek's row, landed from the frame clock once laid out (or after a beat)
  std::optional<int64_t> pendingScroll_;
  int pendingFrames_ = 0;
  bool scrollTicking_ = false;
};

}  // namespace urnw
