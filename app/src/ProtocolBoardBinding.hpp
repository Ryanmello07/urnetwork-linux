// The Protocol board's half of its requests (sdk Api SnTop and SnTopMe), over
// the calls it makes, so their order, their guards and their give-ups are
// tested with a fake service (tests/ProtocolBoardBindingTest.cpp) and the
// page carries none of it:
//
//   - Attach adopts the service of the account's Api; a different Api later
//     (a server or extender change replaces it) gets a service of its own.
//   - Refresh asks the list's first page (GET /sn/top, 50 rows), unless a
//     first page is out already. The rows shown stay until it lands, and a
//     failed refresh keeps them (the page says the refresh failed). A next
//     page still on its way is dropped: it belongs to the list being
//     replaced.
//   - LoadMore asks the next page with the last page's cursor, which pins the
//     snapshot, until the list holds the top 400 (Q5) or the server has no
//     more. One page at a time, never while a first page is out. A page from
//     another snapshot means the board moved on: the list starts over.
//     LoadThrough keeps paging until a position is loaded (the position
//     indicator's seek, the "You" row's jump) and says when it is.
//   - RefreshMe asks the caller's own place (GET /sn/top/me), unless that is
//     out already; a failure keeps the last answer.
//   - Every request is given up on after Settings::requestTimeoutMs: the sdk
//     is known to drop callbacks, and a request that never answers must not
//     leave its progress up for good. The give-up is final: the late answer
//     is dropped.
//   - Release drops the service and orphans every answer still on its way: an
//     answer carries the token of its request, and only a request still
//     pending takes one. No answer lands after a release, a sign-out or the
//     page's destruction, and a new account's page never shows the previous
//     account's place.
//
// Pure C++17 (no GTK, no SDK); ProtocolBoard.cpp adapts the sdk's Api to
// Service and the main loop's timer to Schedule. Main loop only.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ProtocolBoardPresentation.hpp"

namespace urnw::protocol_board {

// The board's tunables (connect/CODESTYLE.md "Tunables live in settings").
struct Settings {
  // How long a request may go unanswered before it is given up on, in ms.
  // 20 s is the app's give-up for a plain api call (docs/parity/CONTRACT.md
  // rule 3). Non-positive never gives up.
  int requestTimeoutMs = 0;
  // Rows per page (GET /sn/top's limit; the server allows 1..200).
  int pageSize = 0;
};

inline Settings DefaultSettings() {
  Settings settings;
  settings.requestTimeoutMs = 20 * 1000;
  settings.pageSize = kPageSize;
  return settings;
}

// GET /sn/top's query.
struct TopArgs {
  int limit = kPageSize;
  std::string cursor;            // "" for the first page
  std::string filter = "all";    // all | unclaimed | claimed
  std::string snapshot;          // "" for the latest
};

// An answer, as plain data. `ok`: the sdk's result arrived with neither a
// transport error nor the server's error.
struct TopAnswer {
  bool ok = false;
  std::optional<Snapshot> snapshot;
  std::vector<Row> rows;
  std::optional<Row> me;  // the caller's row wherever it ranks; nullopt signed out or unranked
  std::string next;       // the next page's cursor; "" at the end
};

struct MeAnswer {
  bool ok = false;
  std::optional<Snapshot> snapshot;
  std::optional<Me> me;  // nullopt when the network is not ranked
};

// The api calls (sdk Api). Each answers at most once, on the main loop.
class Service {
 public:
  virtual ~Service() = default;
  virtual void Top(const TopArgs& args, std::function<void(TopAnswer)> done) = 0;
  virtual void TopMe(std::function<void(MeAnswer)> done) = 0;
};

// Runs `fire` once, `delayMs` from now, on the main loop.
using Schedule = std::function<void(int delayMs, std::function<void()> fire)>;

class Binding {
 public:
  Binding(Settings settings, Schedule schedule)
      : settings_(settings), schedule_(std::move(schedule)) {
    if (settings_.pageSize <= 0) settings_.pageSize = kPageSize;
  }
  ~Binding() { Release(); }
  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;

  // After every change of the list or of `me`.
  std::function<void()> on_changed;
  // A LoadThrough's position is loaded (or the list ends before it): the page
  // scrolls there.
  std::function<void(int64_t position)> on_loaded_through;
  // The caller's own place answered (or was given up on): the Earnings page's
  // Top 200 tile, which reads only that, renders again. After on_changed.
  std::function<void()> on_mine_changed;

  // `source` names what the service calls (the Api's handle): a different one
  // later is a different Api, which needs a service of its own. A service
  // still attached is released first.
  void Attach(std::unique_ptr<Service> service, uint64_t source) {
    Release();
    if (!service) return;
    service_ = std::move(service);
    source_ = source;
  }

  bool Attached() const { return service_ != nullptr; }
  uint64_t Source() const { return source_; }

  // Detached: every answer still on its way is dropped, and the departed
  // account's board and place go with it. Nothing is announced (the page
  // renders after).
  void Release() {
    listToken_ = 0;
    pageToken_ = 0;
    meToken_ = 0;
    through_ = 0;
    list_ = ListState{};
    listMe_.reset();
    me_ = MeState{};
    source_ = 0;
    service_.reset();
  }

  const ListState& List() const { return list_; }
  const MeState& Mine() const { return me_; }
  // The caller's row as the list's last first page placed it (wherever it ranks).
  const std::optional<Row>& ListMe() const { return listMe_; }

  // Asks the list's first page. False when nothing was sent: no service, or a
  // first page is out already (its answer is new enough).
  bool Refresh() {
    if (!service_ || listToken_ != 0) return false;
    const uint64_t token = ++tokens_;
    listToken_ = token;
    // the list being replaced takes its next page and its seek with it
    pageToken_ = 0;
    through_ = 0;
    list_.loadingMore = false;
    list_.moreFailed = false;
    list_.loading = true;
    Changed();
    TopArgs args;
    args.limit = settings_.pageSize;
    service_->Top(args, [this, token](TopAnswer answer) { OnTop(token, std::move(answer)); });
    GiveUpLater([this, token] { OnTop(token, TopAnswer{}); });
    return true;
  }

  // Asks the next page. False when nothing was sent: no service, no list yet,
  // a first page or a page out, the server's end, or the top 400 loaded.
  bool LoadMore() {
    if (!CanLoadMore()) return false;
    const uint64_t token = ++tokens_;
    pageToken_ = token;
    list_.loadingMore = true;
    list_.moreFailed = false;
    Changed();
    TopArgs args;
    args.limit = settings_.pageSize;
    args.cursor = list_.next;
    if (list_.snapshot) args.snapshot = list_.snapshot->id;
    service_->Top(args, [this, token](TopAnswer answer) { OnPage(token, std::move(answer)); });
    GiveUpLater([this, token] { OnPage(token, TopAnswer{}); });
    return true;
  }

  // Pages until `position` is loaded (at most the top 400). True when a page
  // was sent for it, or it is loaded already, which on_loaded_through hears at
  // once. False when it cannot be reached: no list, the list ends before it,
  // or a first page is out.
  bool LoadThrough(int64_t position) {
    if (!service_ || !list_.loaded || listToken_ != 0) return false;
    const int64_t target = std::clamp<int64_t>(position, 1, kListPositions);
    if (Loaded(target)) {
      through_ = 0;
      if (on_loaded_through) on_loaded_through(target);
      return true;
    }
    if (!CanLoadMore() && pageToken_ == 0) return false;
    through_ = target;
    if (pageToken_ == 0) LoadMore();
    return true;
  }

  // Asks the caller's own place. False when nothing was sent: no service, or
  // that request is out already.
  bool RefreshMe() {
    if (!service_ || meToken_ != 0) return false;
    const uint64_t token = ++tokens_;
    meToken_ = token;
    me_.loading = true;
    Changed();
    service_->TopMe([this, token](MeAnswer answer) { OnMe(token, std::move(answer)); });
    GiveUpLater([this, token] { OnMe(token, MeAnswer{}); });
    return true;
  }

  // The position the loaded rows reach (0 for none).
  int64_t LastPosition() const { return list_.rows.empty() ? 0 : list_.rows.back().position; }

 private:
  bool Loaded(int64_t position) const { return LastPosition() >= position; }

  bool CanLoadMore() const {
    return service_ && list_.loaded && listToken_ == 0 && pageToken_ == 0 && !list_.next.empty() &&
           LastPosition() < kListPositions;
  }

  void Changed() {
    if (on_changed) on_changed();
  }

  void GiveUpLater(std::function<void()> giveUp) {
    if (settings_.requestTimeoutMs <= 0 || !schedule_) return;
    schedule_(settings_.requestTimeoutMs, std::move(giveUp));
  }

  // The rows a page adds after `after`: in position order, each position once,
  // none past the top 400.
  static std::vector<Row> Accept(std::vector<Row> rows, int64_t after) {
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& a, const Row& b) { return a.position < b.position; });
    std::vector<Row> accepted;
    int64_t last = after;
    for (Row& row : rows) {
      if (row.position <= last || row.position > kListPositions) continue;
      last = row.position;
      accepted.push_back(std::move(row));
    }
    return accepted;
  }

  // The cursor to keep: none once the top 400 is in, or when a page added
  // nothing (a cursor that does not move would page forever).
  std::string NextAfter(const std::string& next, bool added) const {
    if (!added || LastPosition() >= kListPositions) return std::string();
    return next;
  }

  // A first page's answer, or its give-up (a failed answer with the same token).
  void OnTop(uint64_t token, TopAnswer answer) {
    if (token == 0 || token != listToken_) return;
    listToken_ = 0;
    list_.loading = false;
    if (answer.ok) {
      list_.rows = Accept(std::move(answer.rows), 0);
      list_.snapshot = std::move(answer.snapshot);
      list_.next = NextAfter(answer.next, !list_.rows.empty());
      list_.loaded = true;
      list_.loadFailed = false;
      list_.moreFailed = false;
      listMe_ = std::move(answer.me);
    } else {
      list_.loadFailed = true;
    }
    Changed();
  }

  // A next page's answer, or its give-up.
  void OnPage(uint64_t token, TopAnswer answer) {
    if (token == 0 || token != pageToken_) return;
    pageToken_ = 0;
    list_.loadingMore = false;
    if (!answer.ok) {
      list_.moreFailed = true;
      through_ = 0;  // a seek that cannot be served is dropped
      Changed();
      return;
    }
    const bool moved = answer.snapshot && list_.snapshot && !answer.snapshot->id.empty() &&
                       !list_.snapshot->id.empty() && answer.snapshot->id != list_.snapshot->id;
    if (moved) {
      // the board was recomputed between the pages: start over from its top
      Changed();
      Refresh();
      return;
    }
    std::vector<Row> added = Accept(std::move(answer.rows), LastPosition());
    const bool any = !added.empty();
    for (Row& row : added) list_.rows.push_back(std::move(row));
    list_.next = NextAfter(answer.next, any);
    list_.moreFailed = false;
    Changed();
    if (through_ == 0) return;
    if (Loaded(through_) || !CanLoadMore()) {
      const int64_t reached = std::min(through_, LastPosition());
      through_ = 0;
      if (on_loaded_through) on_loaded_through(reached);
      return;
    }
    LoadMore();
  }

  // The caller's place, or its give-up.
  void OnMe(uint64_t token, MeAnswer answer) {
    if (token == 0 || token != meToken_) return;
    meToken_ = 0;
    me_.loading = false;
    if (answer.ok) {
      me_.me = std::move(answer.me);
      me_.snapshot = std::move(answer.snapshot);
      me_.loaded = true;
      me_.loadFailed = false;
    } else {
      me_.loadFailed = true;
    }
    Changed();
    if (on_mine_changed) on_mine_changed();
  }

  Settings settings_;
  Schedule schedule_;
  std::unique_ptr<Service> service_;
  uint64_t source_ = 0;
  ListState list_;
  std::optional<Row> listMe_;
  MeState me_;
  uint64_t tokens_ = 0;     // requests sent so far; never reused, a release included
  uint64_t listToken_ = 0;  // the first page out, or 0
  uint64_t pageToken_ = 0;  // the next page out, or 0
  uint64_t meToken_ = 0;    // the own place out, or 0
  int64_t through_ = 0;     // the position a LoadThrough pages to, or 0
};

}  // namespace urnw::protocol_board
