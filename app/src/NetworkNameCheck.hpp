// The create-network form's network name availability check, kept free of GTK
// so it is unit tested with an injected check and scheduler
// (tests/NetworkNameCheckTest.cpp).
//
// The availability check is advisory: the server re-checks the name on create
// and refuses a taken one with an error the form shows. A check that fails
// (offline, timeout, api error) therefore says nothing about the name. It must
// not read as "taken" and must not hold Continue disabled, or a sign-up with a
// flaky check can never finish (in-app feedback theme login-signin). A failed
// check is re-run for the same name a bounded number of times.
//
// Not thread safe: every call, and every check completion, runs on one thread
// (the GTK loop).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace urnw {

enum class NetworkNameState { NotChecked, Validating, Valid, Taken, CheckFailed };

// ok: the check got an answer; available: the answer, when ok.
inline NetworkNameState NetworkNameStateForCheck(bool ok, bool available) {
  if (!ok) return NetworkNameState::CheckFailed;
  return available ? NetworkNameState::Valid : NetworkNameState::Taken;
}

// Whether the name lets the form continue. A failed check defers to the
// server's check on create.
inline bool NetworkNameAllowsCreate(NetworkNameState state) {
  return state == NetworkNameState::Valid || state == NetworkNameState::CheckFailed;
}

// Delay before re-running a failed check, by the number of consecutive failed
// checks for the same name; 0 stops re-checking (an edit always re-checks).
inline unsigned NetworkNameRecheckDelayMs(int failedCheckCount) {
  constexpr int kMaxRechecks = 3;
  if (failedCheckCount < 1 || kMaxRechecks < failedCheckCount) return 0;
  return 2000u * static_cast<unsigned>(failedCheckCount);
}

// Debounces the check per edit, drops answers for a superseded name, and
// re-runs failed checks.
class NetworkNameChecker {
 public:
  using CheckDone = std::function<void(bool ok, bool available)>;
  // runs the availability check; done runs later, on the owner's thread
  using Check = std::function<void(const std::string& name, CheckDone done)>;
  // runs the task after delayMs; at most one task is pending, and a later
  // Schedule or a Cancel replaces it
  using Schedule = std::function<void(unsigned delayMs, std::function<void()> task)>;
  using Cancel = std::function<void()>;
  using Changed = std::function<void(NetworkNameState state)>;

  NetworkNameChecker(Check check, Schedule schedule, Cancel cancel, Changed changed,
                     unsigned debounceMs)
      : check_(std::move(check)),
        schedule_(std::move(schedule)),
        cancel_(std::move(cancel)),
        changed_(std::move(changed)),
        debounceMs_(debounceMs) {}

  NetworkNameState State() const { return state_; }

  // No checkable name (empty, too short, or a reset form).
  void Clear() {
    Supersede();
    SetState(NetworkNameState::NotChecked);
  }

  // A checkable name was entered: check it after the debounce.
  void SetName(const std::string& name) {
    Supersede();
    SetState(NetworkNameState::Validating);
    const uint64_t generation = generation_;
    schedule_(debounceMs_, [this, name, generation] {
      if (generation != generation_) return;
      Run(name, 0, generation);
    });
  }

 private:
  void Supersede() {
    ++generation_;
    cancel_();
  }

  void SetState(NetworkNameState state) {
    state_ = state;
    changed_(state);
  }

  void Run(const std::string& name, int failedCheckCount, uint64_t generation) {
    check_(name, [this, name, failedCheckCount, generation](bool ok, bool available) {
      if (generation != generation_) return;  // the name changed since
      const NetworkNameState state = NetworkNameStateForCheck(ok, available);
      if (state == NetworkNameState::CheckFailed) {
        const unsigned delayMs = NetworkNameRecheckDelayMs(failedCheckCount + 1);
        if (0 < delayMs) {
          schedule_(delayMs, [this, name, failedCheckCount, generation] {
            if (generation != generation_) return;
            Run(name, failedCheckCount + 1, generation);
          });
        }
      }
      SetState(state);
    });
  }

  Check check_;
  Schedule schedule_;
  Cancel cancel_;
  Changed changed_;
  unsigned debounceMs_;
  NetworkNameState state_ = NetworkNameState::NotChecked;
  uint64_t generation_ = 0;
};

}  // namespace urnw
