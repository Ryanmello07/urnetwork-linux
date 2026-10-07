// The keyed reconcile (KeyedReconcile.hpp) the connect page's activity list
// applies to its rows: replayed on a vector, the steps always produce the
// wanted list, and they keep every row whose key is still wanted.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "KeyedReconcile.hpp"

using urnw::reconcile::Plan;
using urnw::reconcile::Step;
using urnw::reconcile::StepKind;

namespace {

// A row on screen: its key and the generation it was built in, so a replay
// can tell a kept row from a rebuilt one.
struct Row {
  std::string key;
  int built = 0;
};

// The steps applied to rows built from `current` in generation 0; inserts
// build in generation 1.
std::vector<Row> Replay(const std::vector<std::string>& current,
                        const std::vector<std::string>& wanted, const std::vector<Step>& steps) {
  std::vector<Row> rows;
  for (const auto& key : current) rows.push_back(Row{key, 0});
  for (const Step& step : steps) {
    switch (step.kind) {
      case StepKind::Remove:
        rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(step.index));
        break;
      case StepKind::Update:
        break;
      case StepKind::Move: {
        Row row = rows[step.from];
        rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(step.from));
        rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(step.index), row);
        break;
      }
      case StepKind::Insert:
        rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(step.index),
                    Row{wanted[step.wantedIndex], 1});
        break;
    }
  }
  return rows;
}

std::vector<std::string> Keys(const std::vector<Row>& rows) {
  std::vector<std::string> keys;
  for (const Row& row : rows) keys.push_back(row.key);
  return keys;
}

int Count(const std::vector<Step>& steps, StepKind kind) {
  int count = 0;
  for (const Step& step : steps) count += step.kind == kind ? 1 : 0;
  return count;
}

}  // namespace

// A push that changes only the counters keeps every row: one Update each.
UR_TEST(KeyedReconcile_AnUnchangedListOnlyUpdates) {
  const std::vector<std::string> keys = {"a", "b", "c"};
  const auto steps = Plan(keys, keys);
  UR_EXPECT_EQ(3, static_cast<int>(steps.size()));
  UR_EXPECT_EQ(3, Count(steps, StepKind::Update));
  for (size_t i = 0; i < steps.size(); ++i) {
    UR_EXPECT_EQ(i, steps[i].index);
    UR_EXPECT_EQ(i, steps[i].wantedIndex);
  }
}

// A new decision at the top is one insert; the rows under it are kept.
UR_TEST(KeyedReconcile_ANewKeyOnTopIsOneInsert) {
  const std::vector<std::string> current = {"a", "b", "c"};
  const std::vector<std::string> wanted = {"n", "a", "b", "c"};
  const auto steps = Plan(current, wanted);
  UR_EXPECT_EQ(1, Count(steps, StepKind::Insert));
  UR_EXPECT_EQ(0, Count(steps, StepKind::Remove));
  UR_EXPECT_EQ(0, Count(steps, StepKind::Move));
  UR_EXPECT_TRUE(steps.front().kind == StepKind::Insert);
  UR_EXPECT_EQ(0, steps.front().index);
  const auto rows = Replay(current, wanted, steps);
  UR_EXPECT_TRUE(Keys(rows) == wanted);
  UR_EXPECT_EQ(1, rows[0].built);
  for (size_t i = 1; i < rows.size(); ++i) UR_EXPECT_EQ(0, rows[i].built);
}

// The cap trims the oldest rows off the bottom, back to front.
UR_TEST(KeyedReconcile_TrimmingPastTheCapRemovesTheTail) {
  const std::vector<std::string> current = {"a", "b", "c", "d"};
  const std::vector<std::string> wanted = {"n", "a", "b"};
  const auto steps = Plan(current, wanted);
  UR_EXPECT_EQ(2, Count(steps, StepKind::Remove));
  UR_EXPECT_TRUE(steps[0].kind == StepKind::Remove && steps[0].index == 3);
  UR_EXPECT_TRUE(steps[1].kind == StepKind::Remove && steps[1].index == 2);
  UR_EXPECT_TRUE(Keys(Replay(current, wanted, steps)) == wanted);
}

// A decision the feed moved to the top is one Move of its own row.
UR_TEST(KeyedReconcile_AMovedKeyIsOneMove) {
  const std::vector<std::string> current = {"a", "b", "c"};
  const std::vector<std::string> wanted = {"c", "a", "b"};
  const auto steps = Plan(current, wanted);
  UR_EXPECT_EQ(1, Count(steps, StepKind::Move));
  UR_EXPECT_EQ(0, Count(steps, StepKind::Insert));
  UR_EXPECT_EQ(0, Count(steps, StepKind::Remove));
  UR_EXPECT_TRUE(steps[0].kind == StepKind::Move && steps[0].from == 2 && steps[0].index == 0);
  const auto rows = Replay(current, wanted, steps);
  UR_EXPECT_TRUE(Keys(rows) == wanted);
  for (const Row& row : rows) UR_EXPECT_EQ(0, row.built);
}

// A filter that hides rows removes exactly those, and keeps the rest.
UR_TEST(KeyedReconcile_FilteredOutKeysAreRemoved) {
  const std::vector<std::string> current = {"a", "b", "c", "d", "e"};
  const std::vector<std::string> wanted = {"b", "d"};
  const auto steps = Plan(current, wanted);
  UR_EXPECT_EQ(3, Count(steps, StepKind::Remove));
  UR_EXPECT_EQ(2, Count(steps, StepKind::Update));
  const auto rows = Replay(current, wanted, steps);
  UR_EXPECT_TRUE(Keys(rows) == wanted);
  for (const Row& row : rows) UR_EXPECT_EQ(0, row.built);
  // and clearing the filter brings them back as new rows
  const auto back = Plan(wanted, current);
  UR_EXPECT_EQ(3, Count(back, StepKind::Insert));
  UR_EXPECT_TRUE(Keys(Replay(wanted, current, back)) == current);
}

UR_TEST(KeyedReconcile_EmptyListsAreHandled) {
  UR_EXPECT_TRUE(Plan({}, {}).empty());
  const std::vector<std::string> keys = {"a", "b"};
  UR_EXPECT_EQ(2, Count(Plan({}, keys), StepKind::Insert));
  UR_EXPECT_EQ(2, Count(Plan(keys, {}), StepKind::Remove));
  UR_EXPECT_TRUE(Keys(Replay(keys, {}, Plan(keys, {}))).empty());
}

// A key on screen more often than it is wanted loses its extra rows.
UR_TEST(KeyedReconcile_DuplicateKeysStillConverge) {
  const std::vector<std::string> current = {"a", "a", "b", "a"};
  const std::vector<std::string> wanted = {"b", "a"};
  const auto steps = Plan(current, wanted);
  UR_EXPECT_TRUE(Keys(Replay(current, wanted, steps)) == wanted);
  const std::vector<std::string> more = {"a", "b", "a", "a"};
  UR_EXPECT_TRUE(Keys(Replay(wanted, more, Plan(wanted, more))) == more);
}

// Any pair of lists: the replay is the wanted list, a Move always comes from
// further down, and every row whose key is wanted once on both sides is kept.
UR_TEST(KeyedReconcile_RandomListsAlwaysConverge) {
  uint32_t seed = 0x2545f491u;
  auto next = [&seed](uint32_t bound) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed % bound;
  };
  auto list = [&next](uint32_t alphabet, bool unique) {
    std::vector<std::string> keys;
    const uint32_t size = next(12);
    for (uint32_t i = 0; i < size; ++i) {
      const std::string key(1, static_cast<char>('a' + next(alphabet)));
      bool seen = false;
      for (const auto& k : keys) seen = seen || k == key;
      if (unique && seen) continue;
      keys.push_back(key);
    }
    return keys;
  };
  for (int round = 0; round < 2000; ++round) {
    const bool unique = round % 4 != 0;
    const auto current = list(10, unique);
    const auto wanted = list(10, unique);
    const auto steps = Plan(current, wanted);
    const auto rows = Replay(current, wanted, steps);
    if (Keys(rows) != wanted) {
      UR_FAIL("round " + std::to_string(round) + " did not converge");
      return;
    }
    for (const Step& step : steps) {
      if (step.kind == StepKind::Move) UR_EXPECT_TRUE(step.index < step.from);
    }
    if (!unique) continue;
    for (const Row& row : rows) {
      bool wasOnScreen = false;
      for (const auto& key : current) wasOnScreen = wasOnScreen || key == row.key;
      if (wasOnScreen && row.built != 0) {
        UR_FAIL("round " + std::to_string(round) + " rebuilt kept row " + row.key);
        return;
      }
    }
  }
}
