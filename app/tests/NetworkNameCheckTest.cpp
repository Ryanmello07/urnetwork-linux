// The create-network form's network name check: a failed check is neither
// "taken" nor a reason to hold Continue disabled, and it is re-run. The check
// and the scheduler are fakes, so nothing here depends on time or the network.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "NetworkNameCheck.hpp"

#include <deque>
#include <functional>
#include <string>
#include <vector>

using urnw::NetworkNameChecker;
using urnw::NetworkNameState;

namespace {

constexpr unsigned kDebounceMs = 250;

// Records checks and the single pending scheduled task; tests resolve them.
struct Harness {
  struct PendingCheck {
    std::string name;
    NetworkNameChecker::CheckDone done;
  };
  std::deque<PendingCheck> checks;
  std::function<void()> scheduled;
  std::vector<unsigned> scheduledDelays;
  std::vector<NetworkNameState> states;
  NetworkNameChecker checker{
      [this](const std::string& name, NetworkNameChecker::CheckDone done) {
        checks.push_back(PendingCheck{name, std::move(done)});
      },
      [this](unsigned delayMs, std::function<void()> task) {
        scheduledDelays.push_back(delayMs);
        scheduled = std::move(task);
      },
      [this] { scheduled = nullptr; },
      [this](NetworkNameState state) { states.push_back(state); },
      kDebounceMs};

  // runs the pending scheduled task, as its timer firing would
  bool Fire() {
    if (!scheduled) return false;
    auto task = std::move(scheduled);
    scheduled = nullptr;
    task();
    return true;
  }

  // answers the oldest pending check; false when none is pending
  bool Answer(bool ok, bool available) {
    if (checks.empty()) return false;
    auto check = std::move(checks.front());
    checks.pop_front();
    check.done(ok, available);
    return true;
  }
};

}  // namespace

UR_TEST(failedNetworkNameCheckIsNotTaken) {
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(false, false) == NetworkNameState::CheckFailed);
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(false, true) == NetworkNameState::CheckFailed);
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(true, false) == NetworkNameState::Taken);
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(true, true) == NetworkNameState::Valid);
}

UR_TEST(failedNetworkNameCheckKeepsCreateUsable) {
  Harness h;
  h.checker.SetName("mynetwork");
  UR_EXPECT_FALSE(urnw::NetworkNameAllowsCreate(h.checker.State()));
  UR_EXPECT_TRUE(h.Fire());
  UR_EXPECT_EQ(1u, h.checks.size());
  h.Answer(false, false);
  UR_EXPECT_TRUE(h.checker.State() == NetworkNameState::CheckFailed);
  UR_EXPECT_TRUE(urnw::NetworkNameAllowsCreate(h.checker.State()));
}

UR_TEST(takenNetworkNameBlocksCreateAndIsNotRechecked) {
  Harness h;
  h.checker.SetName("mynetwork");
  h.Fire();
  h.Answer(true, false);
  UR_EXPECT_TRUE(h.checker.State() == NetworkNameState::Taken);
  UR_EXPECT_FALSE(urnw::NetworkNameAllowsCreate(h.checker.State()));
  UR_EXPECT_FALSE(h.Fire());
}

UR_TEST(failedNetworkNameCheckIsRecheckedUntilItAnswers) {
  Harness h;
  h.checker.SetName("mynetwork");
  h.Fire();
  h.Answer(false, false);
  UR_EXPECT_TRUE(h.Fire());  // the recheck timer
  UR_EXPECT_EQ(1u, h.checks.size());
  UR_EXPECT_TRUE(!h.checks.empty() && h.checks.front().name == "mynetwork");
  UR_EXPECT_TRUE(h.Answer(true, true));
  UR_EXPECT_TRUE(h.checker.State() == NetworkNameState::Valid);
  UR_EXPECT_FALSE(h.Fire());
}

UR_TEST(failedNetworkNameCheckIsRecheckedABoundedNumberOfTimes) {
  Harness h;
  h.checker.SetName("mynetwork");
  h.Fire();
  int checkCount = 0;
  while (!h.checks.empty()) {
    ++checkCount;
    h.Answer(false, false);
    h.Fire();
  }
  // the first check plus three rechecks, with growing delays
  UR_EXPECT_EQ(4, checkCount);
  UR_EXPECT_EQ(4u, h.scheduledDelays.size());
  if (h.scheduledDelays.size() == 4u) {
    UR_EXPECT_EQ(kDebounceMs, h.scheduledDelays[0]);
    UR_EXPECT_TRUE(h.scheduledDelays[1] < h.scheduledDelays[2]);
    UR_EXPECT_TRUE(h.scheduledDelays[2] < h.scheduledDelays[3]);
  }
  UR_EXPECT_TRUE(h.checker.State() == NetworkNameState::CheckFailed);
  UR_EXPECT_TRUE(urnw::NetworkNameAllowsCreate(h.checker.State()));
}

UR_TEST(editingTheNameDropsTheStaleAnswerAndRecheck) {
  Harness h;
  h.checker.SetName("mynetwork");
  h.Fire();
  h.checker.SetName("mynetwork2");
  h.Answer(false, false);  // the old name's answer arrives late
  UR_EXPECT_TRUE(h.checker.State() == NetworkNameState::Validating);
  UR_EXPECT_TRUE(h.Fire());  // the new name's debounce, not a recheck
  UR_EXPECT_EQ(1u, h.checks.size());
  UR_EXPECT_TRUE(!h.checks.empty() && h.checks.back().name == "mynetwork2");
  h.Answer(true, true);
  UR_EXPECT_TRUE(h.checker.State() == NetworkNameState::Valid);
}

UR_TEST(clearingTheNameCancelsAPendingRecheck) {
  Harness h;
  h.checker.SetName("mynetwork");
  h.Fire();
  h.Answer(false, false);
  h.checker.Clear();
  UR_EXPECT_FALSE(h.Fire());
  UR_EXPECT_TRUE(h.checker.State() == NetworkNameState::NotChecked);
}
