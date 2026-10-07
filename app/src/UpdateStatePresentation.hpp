// The line under Settings > General > "Check for updates", decided pure: what
// the updater's last check came to, in the store's words, and whether the row's
// Check now button may start another (windows SettingsPage::ApplyUpdateCheck,
// whose update state and Check now rows this is). The words are the sentences
// the developer page's update line already reads, so Settings says nothing the
// store does not carry. A release on offer is named by the update notice
// under the line, so the line does not name it again.
//
// No GTK: SettingsPage::ApplyUpdateState looks the key up and fills the
// version in, and tests/UpdateStatePresentationTest.cpp checks the table.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "UpdateSchedule.hpp"

namespace urnw::update {

// The version a line names in its {} placeholder.
enum class StateVersion {
  None,
  Newest,  // the newest stable release the check saw (Snapshot::newestVersion)
};

// What the line under Check now says, as a store key, and whether Check now
// may run.
struct UpdateStateLine {
  // The store key and its English; both "" when there is nothing to say.
  const char* textKey = "";
  const char* textEnglish = "";
  StateVersion version = StateVersion::None;
  // Read in the danger tone.
  bool failed = false;
  // Check now may start a check: not while one is in flight.
  bool canCheck = true;
};

// What the snapshot says about the last check.
struct UpdateStateInputs {
  CheckOutcome outcome = CheckOutcome::NeverRan;
  // The last completed check saw a stable release.
  bool newestKnown = false;
  // That release outranks this build, yet none is offered: it has no file
  // for this install or no usable digest, or this install cannot update
  // itself (SelectRelease's skips, in the log).
  bool newestOutranksOwn = false;
};

// Before any check has run in this session the line says nothing: a launch
// inside the six-hour throttle asks nothing, and "not checked" would not be
// true of the check an earlier launch made. It says nothing either while a
// release is offered, since the notice under it names that release, or when
// a newer release is out that this install is not offered, since "up to date"
// would not be true.
inline UpdateStateLine UpdateStateLineFor(const UpdateStateInputs& in) {
  UpdateStateLine line;
  switch (in.outcome) {
    case CheckOutcome::NeverRan:
      break;
    case CheckOutcome::InFlight:
      line.textKey = "dev_update_checking";
      line.textEnglish = "Checking for updates…";
      line.canCheck = false;
      break;
    case CheckOutcome::NoUpdate:
      if (!in.newestKnown) {
        line.textKey = "dev_update_no_releases";
        line.textEnglish = "No stable release has been published yet.";
      } else if (!in.newestOutranksOwn) {
        line.textKey = "dev_update_up_to_date";
        line.textEnglish = "Up to date. Newest release: v{}";
        line.version = StateVersion::Newest;
      }
      break;
    case CheckOutcome::UpdateFound:
      break;
    case CheckOutcome::DevBuild:
      line.textKey = "dev_update_dev_build";
      line.textEnglish =
          "Development build: the newest release is v{}, and a development build is never "
          "updated automatically.";
      line.version = StateVersion::Newest;
      break;
    case CheckOutcome::Failed:
      line.textKey = "dev_update_check_failed";
      line.textEnglish = "The update check failed — see the app log.";
      line.failed = true;
      break;
  }
  return line;
}

}  // namespace urnw::update
