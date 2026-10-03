// The create-network form's network name availability check, kept free of GTK
// so the decisions are unit tested (tests/NetworkNameCheckTest.cpp).
//
// The availability check is advisory: the server re-checks the name on create
// and refuses a taken one with an error the form shows. A check that fails
// (offline, timeout, api error) therefore says nothing about the name. It must
// not read as "taken" and must not hold Continue disabled, or a sign-up with a
// flaky check can never finish (in-app feedback theme login-signin).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

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

}  // namespace urnw
