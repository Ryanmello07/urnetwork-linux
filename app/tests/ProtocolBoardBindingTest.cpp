// The Protocol board's requests (ProtocolBoardBinding.hpp), with a fake
// service that keeps every request's answer callback until the test answers
// it, and a manual clock for the give-ups: one first page at a time with the
// rows kept over a refresh and its failure, pages of 50 with the cursor that
// pins the snapshot up to the top 400, a page from a recomputed snapshot
// starting the list over, a seek that pages until its position is loaded,
// the caller's own place kept over a failed refresh, every request given up
// on after its timeout with the late answer dropped, and no answer landing
// after a release or from a previous service.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ProtocolBoardBinding.hpp"

namespace p = urnw::protocol_board;

namespace {

// Every request the binding made, with the callback that answers it.
struct Calls {
  std::vector<std::pair<p::TopArgs, std::function<void(p::TopAnswer)>>> tops;
  std::vector<std::function<void(p::MeAnswer)>> mes;
  bool destroyed = false;
};

class FakeService final : public p::Service {
 public:
  explicit FakeService(std::shared_ptr<Calls> calls) : calls_(std::move(calls)) {}
  ~FakeService() override { calls_->destroyed = true; }
  void Top(const p::TopArgs& args, std::function<void(p::TopAnswer)> done) override {
    calls_->tops.emplace_back(args, std::move(done));
  }
  void TopMe(std::function<void(p::MeAnswer)> done) override {
    calls_->mes.push_back(std::move(done));
  }

 private:
  std::shared_ptr<Calls> calls_;
};

// The give-ups the binding armed, in order; the test fires them.
struct Clock {
  std::vector<std::pair<int, std::function<void()>>> armed;
  p::Schedule Schedule() {
    return [this](int delayMs, std::function<void()> fire) {
      armed.emplace_back(delayMs, std::move(fire));
    };
  }
};

// A binding attached to a fresh fake service, counting its renders.
struct Fixture {
  Clock clock;
  p::Binding binding{p::DefaultSettings(), clock.Schedule()};
  std::shared_ptr<Calls> calls = std::make_shared<Calls>();
  int renders = 0;
  std::vector<int64_t> reached;

  Fixture() {
    binding.on_changed = [this] { ++renders; };
    binding.on_loaded_through = [this](int64_t position) { reached.push_back(position); };
    binding.Attach(std::make_unique<FakeService>(calls), /*source=*/7);
  }
};

p::Snapshot SnapshotOf(const std::string& id) {
  p::Snapshot snapshot;
  snapshot.id = id;
  snapshot.totalRanked = 41230;
  snapshot.projectedCutoff = 41.2;
  return snapshot;
}

p::Row RowAt(int64_t position) {
  p::Row row;
  row.position = position;
  row.name = "net-" + std::to_string(position);
  row.measurable = true;
  row.projectedScore = 500.0 - static_cast<double>(position);
  return row;
}

// Rows `from`..`to`, inclusive.
std::vector<p::Row> RowsFrom(int64_t from, int64_t to) {
  std::vector<p::Row> rows;
  for (int64_t position = from; position <= to; ++position) rows.push_back(RowAt(position));
  return rows;
}

p::TopAnswer Page(const std::string& snapshot, std::vector<p::Row> rows, const std::string& next) {
  p::TopAnswer answer;
  answer.ok = true;
  answer.snapshot = SnapshotOf(snapshot);
  answer.rows = std::move(rows);
  answer.next = next;
  return answer;
}

std::vector<int64_t> PositionsOf(const p::ListState& list) {
  std::vector<int64_t> positions;
  for (const p::Row& row : list.rows) positions.push_back(row.position);
  return positions;
}

// The fixture with the first page (1..50) loaded, its cursor "c50".
void LoadFirst(Fixture& f) {
  f.binding.Refresh();
  f.calls->tops.back().second(Page("s1", RowsFrom(1, 50), "c50"));
}

p::MeAnswer MeOf(int64_t position) {
  p::MeAnswer answer;
  answer.ok = true;
  answer.snapshot = SnapshotOf("s1");
  p::Me me;
  me.row = RowAt(position);
  me.row.isMe = true;
  answer.me = me;
  return answer;
}

}  // namespace

UR_TEST(ProtocolBoardBinding_TheDefaultsAreTheAppsGiveUpAndPagesOf50) {
  const p::Settings settings = p::DefaultSettings();
  UR_EXPECT_EQ(20000, settings.requestTimeoutMs);
  UR_EXPECT_EQ(50, settings.pageSize);
  UR_EXPECT_EQ(400, p::kListPositions);
  // non-positive never gives up: nothing is armed
  Clock clock;
  p::Binding binding{p::Settings{}, clock.Schedule()};
  auto calls = std::make_shared<Calls>();
  binding.Attach(std::make_unique<FakeService>(calls), 1);
  UR_EXPECT_TRUE(binding.Refresh());
  UR_EXPECT_TRUE(binding.RefreshMe());
  UR_EXPECT_EQ(0u, clock.armed.size());
  // a page size left unset is the board's 50
  UR_EXPECT_EQ(50, calls->tops.back().first.limit);
}

UR_TEST(ProtocolBoardBinding_NothingIsSentWithoutAService) {
  Clock clock;
  p::Binding binding{p::DefaultSettings(), clock.Schedule()};
  int renders = 0;
  binding.on_changed = [&renders] { ++renders; };
  UR_EXPECT_FALSE(binding.Attached());
  UR_EXPECT_FALSE(binding.Refresh());
  UR_EXPECT_FALSE(binding.LoadMore());
  UR_EXPECT_FALSE(binding.LoadThrough(120));
  UR_EXPECT_FALSE(binding.RefreshMe());
  UR_EXPECT_EQ(0, renders);
  UR_EXPECT_EQ(0u, clock.armed.size());
}

// The first page: the latest snapshot, 50 rows, no cursor; the list loads in
// position order and keeps the cursor for the next page.
UR_TEST(ProtocolBoardBinding_TheFirstPageAsksTheLatestFifty) {
  Fixture f;
  UR_EXPECT_TRUE(f.binding.Refresh());
  UR_EXPECT_EQ(1u, f.calls->tops.size());
  const p::TopArgs& args = f.calls->tops[0].first;
  UR_EXPECT_EQ(50, args.limit);
  UR_EXPECT_TRUE(args.cursor.empty());
  UR_EXPECT_TRUE(args.snapshot.empty());
  UR_EXPECT_TRUE(args.filter == "all");
  UR_EXPECT_TRUE(f.binding.List().loading);
  UR_EXPECT_FALSE(f.binding.List().loaded);
  // one first page at a time
  UR_EXPECT_FALSE(f.binding.Refresh());
  UR_EXPECT_EQ(1u, f.calls->tops.size());
  // out of order, a duplicate and a position past the top 400 are put right
  std::vector<p::Row> rows = RowsFrom(1, 50);
  std::swap(rows[0], rows[1]);
  rows.push_back(RowAt(7));
  rows.push_back(RowAt(401));
  f.calls->tops[0].second(Page("s1", rows, "c50"));
  const p::ListState& list = f.binding.List();
  UR_EXPECT_TRUE(list.loaded);
  UR_EXPECT_FALSE(list.loading);
  UR_EXPECT_EQ(50u, list.rows.size());
  UR_EXPECT_TRUE(PositionsOf(list) == [] {
    std::vector<int64_t> expected;
    for (int64_t i = 1; i <= 50; ++i) expected.push_back(i);
    return expected;
  }());
  UR_EXPECT_TRUE(list.next == "c50");
  UR_EXPECT_TRUE(list.snapshot && list.snapshot->id == "s1");
}

// A refresh keeps the rows shown until it lands, a failed one keeps them, and
// a landed one replaces them with the new snapshot's first page.
UR_TEST(ProtocolBoardBinding_ARefreshKeepsTheRowsUntilItLands) {
  Fixture f;
  LoadFirst(f);
  UR_EXPECT_TRUE(f.binding.Refresh());
  UR_EXPECT_TRUE(f.binding.List().loading);
  UR_EXPECT_EQ(50u, f.binding.List().rows.size());
  p::TopAnswer failed;
  f.calls->tops.back().second(failed);
  UR_EXPECT_TRUE(f.binding.List().loadFailed);
  UR_EXPECT_TRUE(f.binding.List().loaded);
  UR_EXPECT_EQ(50u, f.binding.List().rows.size());
  UR_EXPECT_TRUE(f.binding.List().snapshot->id == "s1");
  // the next refresh lands: the new snapshot's rows, and the failure is over
  UR_EXPECT_TRUE(f.binding.Refresh());
  f.calls->tops.back().second(Page("s2", RowsFrom(1, 50), "d50"));
  UR_EXPECT_FALSE(f.binding.List().loadFailed);
  UR_EXPECT_TRUE(f.binding.List().snapshot->id == "s2");
  UR_EXPECT_TRUE(f.binding.List().next == "d50");
}

// The next page carries the cursor and the snapshot id; its rows go after
// the ones loaded, one page at a time.
UR_TEST(ProtocolBoardBinding_TheNextPageCarriesTheCursorAndTheSnapshot) {
  Fixture f;
  // nothing to page before a first page has landed
  UR_EXPECT_FALSE(f.binding.LoadMore());
  LoadFirst(f);
  UR_EXPECT_TRUE(f.binding.LoadMore());
  const p::TopArgs& args = f.calls->tops.back().first;
  UR_EXPECT_TRUE(args.cursor == "c50");
  UR_EXPECT_TRUE(args.snapshot == "s1");
  UR_EXPECT_EQ(50, args.limit);
  UR_EXPECT_TRUE(f.binding.List().loadingMore);
  UR_EXPECT_FALSE(f.binding.LoadMore());  // one page at a time
  // an overlap with the rows loaded is dropped
  f.calls->tops.back().second(Page("s1", RowsFrom(49, 100), "c100"));
  UR_EXPECT_EQ(100u, f.binding.List().rows.size());
  UR_EXPECT_EQ(100, f.binding.List().rows.back().position);
  UR_EXPECT_TRUE(f.binding.List().next == "c100");
  UR_EXPECT_FALSE(f.binding.List().loadingMore);
}

// The list stops at the top 400: the page that reaches it drops the cursor,
// and nothing more is asked.
UR_TEST(ProtocolBoardBinding_TheListStopsAtTheTop400) {
  Fixture f;
  f.binding.Refresh();
  f.calls->tops.back().second(Page("s1", RowsFrom(1, 200), "c200"));
  UR_EXPECT_TRUE(f.binding.LoadMore());
  f.calls->tops.back().second(Page("s1", RowsFrom(201, 410), "c410"));
  UR_EXPECT_EQ(400u, f.binding.List().rows.size());
  UR_EXPECT_EQ(400, f.binding.List().rows.back().position);
  UR_EXPECT_TRUE(f.binding.List().next.empty());
  UR_EXPECT_FALSE(f.binding.LoadMore());
  // the server's end before 400 ends it too
  Fixture g;
  g.binding.Refresh();
  g.calls->tops.back().second(Page("s1", RowsFrom(1, 37), ""));
  UR_EXPECT_FALSE(g.binding.LoadMore());
  // a page that adds nothing ends paging: its cursor would never move
  Fixture h;
  LoadFirst(h);
  UR_EXPECT_TRUE(h.binding.LoadMore());
  h.calls->tops.back().second(Page("s1", RowsFrom(1, 50), "c50"));
  UR_EXPECT_TRUE(h.binding.List().next.empty());
  UR_EXPECT_FALSE(h.binding.LoadMore());
}

// A failed page keeps the rows and says so; the next try asks again.
UR_TEST(ProtocolBoardBinding_AFailedPageKeepsTheRowsAndRetries) {
  Fixture f;
  LoadFirst(f);
  f.binding.LoadMore();
  f.calls->tops.back().second(p::TopAnswer{});
  UR_EXPECT_TRUE(f.binding.List().moreFailed);
  UR_EXPECT_FALSE(f.binding.List().loadingMore);
  UR_EXPECT_EQ(50u, f.binding.List().rows.size());
  UR_EXPECT_TRUE(f.binding.List().next == "c50");
  UR_EXPECT_TRUE(f.binding.LoadMore());
  UR_EXPECT_FALSE(f.binding.List().moreFailed);
  UR_EXPECT_TRUE(f.calls->tops.back().first.cursor == "c50");
}

// A page from another snapshot means the board was recomputed between the
// pages: the list starts over from the new snapshot's top.
UR_TEST(ProtocolBoardBinding_APageFromANewSnapshotStartsOver) {
  Fixture f;
  LoadFirst(f);
  f.binding.LoadMore();
  const size_t before = f.calls->tops.size();
  f.calls->tops.back().second(Page("s2", RowsFrom(51, 100), "x"));
  // nothing of the other snapshot was appended, and a first page is out
  UR_EXPECT_EQ(50u, f.binding.List().rows.size());
  UR_EXPECT_EQ(before + 1, f.calls->tops.size());
  UR_EXPECT_TRUE(f.calls->tops.back().first.cursor.empty());
  UR_EXPECT_TRUE(f.binding.List().loading);
}

// A refresh drops a next page still on its way: it belongs to the list being
// replaced.
UR_TEST(ProtocolBoardBinding_ARefreshDropsThePageOut) {
  Fixture f;
  LoadFirst(f);
  f.binding.LoadMore();
  auto page = f.calls->tops.back().second;
  UR_EXPECT_TRUE(f.binding.Refresh());
  UR_EXPECT_FALSE(f.binding.List().loadingMore);
  // no page while the first page is out
  UR_EXPECT_FALSE(f.binding.LoadMore());
  page(Page("s1", RowsFrom(51, 100), "c100"));
  UR_EXPECT_EQ(50u, f.binding.List().rows.size());
}

// A seek pages until its position is loaded, then says so once; a position
// loaded already is reached at once; one past the end reaches the end.
UR_TEST(ProtocolBoardBinding_LoadThroughPagesToThePosition) {
  Fixture f;
  UR_EXPECT_FALSE(f.binding.LoadThrough(120));  // no list yet
  LoadFirst(f);
  UR_EXPECT_TRUE(f.binding.LoadThrough(30));
  UR_EXPECT_TRUE(f.reached == std::vector<int64_t>{30});
  UR_EXPECT_TRUE(f.binding.LoadThrough(120));
  f.calls->tops.back().second(Page("s1", RowsFrom(51, 100), "c100"));
  UR_EXPECT_TRUE(f.reached.size() == 1u);  // not yet
  UR_EXPECT_TRUE(f.calls->tops.back().first.cursor == "c100");
  f.calls->tops.back().second(Page("s1", RowsFrom(101, 150), "c150"));
  UR_EXPECT_TRUE(f.reached == (std::vector<int64_t>{30, 120}));
  // the list ends before the position: the end is where it lands
  UR_EXPECT_TRUE(f.binding.LoadThrough(390));
  f.calls->tops.back().second(Page("s1", RowsFrom(151, 170), ""));
  UR_EXPECT_TRUE(f.reached == (std::vector<int64_t>{30, 120, 170}));
  UR_EXPECT_FALSE(f.binding.LoadThrough(390));
  // a seek past the top 400 is the 400th
  Fixture g;
  g.binding.Refresh();
  g.calls->tops.back().second(Page("s1", RowsFrom(1, 400), "c400"));
  UR_EXPECT_TRUE(g.binding.LoadThrough(5000));
  UR_EXPECT_TRUE(g.reached == std::vector<int64_t>{400});
}

// A failed page drops the seek: nothing is reached, and the next seek starts
// clean.
UR_TEST(ProtocolBoardBinding_AFailedPageDropsTheSeek) {
  Fixture f;
  LoadFirst(f);
  f.binding.LoadThrough(120);
  f.calls->tops.back().second(p::TopAnswer{});
  UR_EXPECT_TRUE(f.reached.empty());
  UR_EXPECT_TRUE(f.binding.LoadMore());
  f.calls->tops.back().second(Page("s1", RowsFrom(51, 100), "c100"));
  UR_EXPECT_TRUE(f.reached.empty());
}

// The caller's place: asked once at a time, kept over a failed refresh, and
// an answer that says the network is not ranked is an answer.
UR_TEST(ProtocolBoardBinding_TheOwnPlaceIsKeptOverAFailedRefresh) {
  Fixture f;
  UR_EXPECT_TRUE(f.binding.RefreshMe());
  UR_EXPECT_TRUE(f.binding.Mine().loading);
  UR_EXPECT_FALSE(f.binding.RefreshMe());
  f.calls->mes.back()(MeOf(173));
  UR_EXPECT_TRUE(f.binding.Mine().loaded);
  UR_EXPECT_TRUE(f.binding.Mine().me && f.binding.Mine().me->row.position == 173);
  UR_EXPECT_TRUE(f.binding.Mine().snapshot && f.binding.Mine().snapshot->id == "s1");
  UR_EXPECT_TRUE(f.binding.RefreshMe());
  f.calls->mes.back()(p::MeAnswer{});
  UR_EXPECT_TRUE(f.binding.Mine().loadFailed);
  UR_EXPECT_TRUE(f.binding.Mine().me && f.binding.Mine().me->row.position == 173);
  // not ranked: loaded, with no place
  UR_EXPECT_TRUE(f.binding.RefreshMe());
  p::MeAnswer unranked;
  unranked.ok = true;
  f.calls->mes.back()(unranked);
  UR_EXPECT_TRUE(f.binding.Mine().loaded);
  UR_EXPECT_FALSE(f.binding.Mine().loadFailed);
  UR_EXPECT_FALSE(f.binding.Mine().me.has_value());
}

// The list's own `me` row (the caller wherever it ranks) is kept with the
// first page.
UR_TEST(ProtocolBoardBinding_TheListsOwnRowIsKept) {
  Fixture f;
  f.binding.Refresh();
  p::TopAnswer answer = Page("s1", RowsFrom(1, 50), "c50");
  p::Row me = RowAt(4210);
  me.isMe = true;
  answer.me = me;
  f.calls->tops.back().second(answer);
  UR_EXPECT_TRUE(f.binding.ListMe() && f.binding.ListMe()->position == 4210);
}

// Every request is given up on after the app's 20 s: its progress goes, the
// give-up reads as a failure, and the late answer is dropped.
UR_TEST(ProtocolBoardBinding_EveryRequestIsGivenUpOn) {
  Fixture f;
  f.binding.Refresh();
  f.binding.RefreshMe();
  UR_EXPECT_EQ(2u, f.clock.armed.size());
  for (const auto& armed : f.clock.armed) UR_EXPECT_EQ(20000, armed.first);
  f.clock.armed[0].second();
  f.clock.armed[1].second();
  UR_EXPECT_FALSE(f.binding.List().loading);
  UR_EXPECT_TRUE(f.binding.List().loadFailed);
  UR_EXPECT_FALSE(f.binding.Mine().loading);
  UR_EXPECT_TRUE(f.binding.Mine().loadFailed);
  // the late answers land nowhere
  f.calls->tops[0].second(Page("s1", RowsFrom(1, 50), "c50"));
  f.calls->mes[0](MeOf(9));
  UR_EXPECT_FALSE(f.binding.List().loaded);
  UR_EXPECT_FALSE(f.binding.Mine().loaded);
  // a page too
  LoadFirst(f);
  f.binding.LoadMore();
  f.clock.armed.back().second();
  UR_EXPECT_TRUE(f.binding.List().moreFailed);
  f.calls->tops.back().second(Page("s1", RowsFrom(51, 100), "c100"));
  UR_EXPECT_EQ(50u, f.binding.List().rows.size());
  // a give-up after its answer changes nothing
  Fixture g;
  LoadFirst(g);
  const int renders = g.renders;
  g.clock.armed[0].second();
  UR_EXPECT_EQ(renders, g.renders);
  UR_EXPECT_FALSE(g.binding.List().loadFailed);
}

// A release orphans every answer and forgets the account's board and place.
UR_TEST(ProtocolBoardBinding_AReleaseOrphansEveryAnswer) {
  Fixture f;
  LoadFirst(f);
  f.binding.RefreshMe();
  f.binding.LoadMore();
  f.binding.Release();
  UR_EXPECT_TRUE(f.calls->destroyed);
  UR_EXPECT_FALSE(f.binding.Attached());
  UR_EXPECT_FALSE(f.binding.List().loaded);
  UR_EXPECT_TRUE(f.binding.List().rows.empty());
  UR_EXPECT_FALSE(f.binding.Mine().loaded);
  const int renders = f.renders;
  f.calls->mes.back()(MeOf(9));
  f.calls->tops.back().second(Page("s1", RowsFrom(51, 100), "c100"));
  UR_EXPECT_FALSE(f.binding.Mine().loaded);
  UR_EXPECT_TRUE(f.binding.List().rows.empty());
  UR_EXPECT_EQ(renders, f.renders);
  for (const auto& armed : f.clock.armed) armed.second();
  UR_EXPECT_EQ(renders, f.renders);
}

// Another Api is another service; the first one's answers land nowhere.
UR_TEST(ProtocolBoardBinding_AttachIsPerApi) {
  Fixture f;
  UR_EXPECT_EQ(7u, f.binding.Source());
  f.binding.RefreshMe();
  auto late = f.calls->mes.back();
  auto second = std::make_shared<Calls>();
  f.binding.Attach(std::make_unique<FakeService>(second), 8);
  UR_EXPECT_TRUE(f.calls->destroyed);
  UR_EXPECT_EQ(8u, f.binding.Source());
  late(MeOf(9));
  UR_EXPECT_FALSE(f.binding.Mine().loaded);
  UR_EXPECT_TRUE(f.binding.RefreshMe());
  UR_EXPECT_EQ(1u, second->mes.size());
  // no service at all detaches
  f.binding.Attach(nullptr, 9);
  UR_EXPECT_FALSE(f.binding.Attached());
  UR_EXPECT_EQ(0u, f.binding.Source());
}

// The Top 200 tile hears only the own place's answers, never the list's
// pages or a request going out.
UR_TEST(ProtocolBoardBinding_TheTileHearsOnlyTheOwnPlace) {
  Fixture f;
  int mine = 0;
  f.binding.on_mine_changed = [&mine] { ++mine; };
  LoadFirst(f);
  f.binding.LoadMore();
  f.calls->tops.back().second(Page("s1", RowsFrom(51, 100), "c100"));
  UR_EXPECT_EQ(0, mine);
  f.binding.RefreshMe();
  UR_EXPECT_EQ(0, mine);
  f.calls->mes.back()(MeOf(173));
  UR_EXPECT_EQ(1, mine);
  // its give-up too, once
  f.binding.RefreshMe();
  f.clock.armed.back().second();
  UR_EXPECT_EQ(2, mine);
  f.calls->mes.back()(MeOf(9));
  UR_EXPECT_EQ(2, mine);
}

// Every change is announced, so the page renders each state.
UR_TEST(ProtocolBoardBinding_EveryChangeIsAnnounced) {
  Fixture f;
  int before = f.renders;
  f.binding.Refresh();
  UR_EXPECT_EQ(before + 1, f.renders);
  f.calls->tops.back().second(Page("s1", RowsFrom(1, 50), "c50"));
  UR_EXPECT_EQ(before + 2, f.renders);
  f.binding.LoadMore();
  UR_EXPECT_EQ(before + 3, f.renders);
  f.calls->tops.back().second(Page("s1", RowsFrom(51, 100), "c100"));
  UR_EXPECT_EQ(before + 4, f.renders);
  f.binding.RefreshMe();
  f.calls->mes.back()(MeOf(9));
  UR_EXPECT_EQ(before + 6, f.renders);
}
