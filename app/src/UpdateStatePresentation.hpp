// The line under Settings > General > "Check for updates", decided pure: what
// the updater's last check came to, in the store's words, and whether the row's
// Check now button may start another (windows SettingsPage::ApplyUpdateCheck,
// whose update state and Check now rows this is). The words are the sentences
// the developer page's update line already reads, so Settings says nothing the
// store does not carry. A release on offer is named by the update notice
// under the line, so the line does not name it again. When no check has
// reached GitHub for three days, or GitHub asked this network to wait, the
// line says so instead, in the words of the windows connect screen's warning
// (ConnectPage::ApplyUpdateChecker).
//
// No GTK: SettingsPage::ApplyUpdateState looks the keys up and fills the
// version, date or time in, and tests/UpdateStatePresentationTest.cpp checks
// the table.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "UpdateSchedule.hpp"

namespace urnw::update {

// What a sentence fills its {} placeholder with.
enum class StateArgument {
  None,
  NewestVersion,    // the newest stable release the check saw (Snapshot::newestVersion)
  LastSuccessDate,  // the local date of the last check that reached GitHub
  HoldEndTime,      // the local date and time GitHub asked this network to wait for
};

// The first sentence's color: muted, the warning amber, or the danger red.
enum class StateTone { Muted, Warning, Danger };

// What the line under Check now says, as store keys, and whether Check now
// may run.
struct UpdateStateLine {
  // The store key and its English; both "" when there is nothing to say.
  const char* textKey = "";
  const char* textEnglish = "";
  StateArgument argument = StateArgument::None;
  // A second sentence under the first, read muted; both "" when none.
  const char* detailKey = "";
  const char* detailEnglish = "";
  StateArgument detailArgument = StateArgument::None;
  // The first sentence's tone.
  StateTone tone = StateTone::Muted;
  // Check now may start a check: not while one is in flight, and not before
  // GitHub said it may be asked again.
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
  // Automatic checks are on and none has reached GitHub for three days
  // (UpdateSchedule.hpp CheckIsStale).
  bool stale = false;
  // GitHub asked this network to wait, and the time has not come.
  bool held = false;
};

// Before any check has run in this session the line says nothing: a launch
// inside the six-hour throttle asks nothing, and "not checked" would not be
// true of the check an earlier launch made. It says nothing either while a
// release is offered, since the notice under it names that release, or when
// a newer release is out that this install is not offered, since "up to date"
// would not be true. Stale and held are only said of a failed check, the one
// outcome they can follow.
inline UpdateStateLine UpdateStateLineFor(const UpdateStateInputs& in) {
  UpdateStateLine line;
  line.canCheck = in.outcome != CheckOutcome::InFlight && !in.held;
  if (in.outcome == CheckOutcome::Failed && in.stale) {
    // a release this app cannot see may be out, and saying only "failed"
    // would read like one bad try
    line.textKey = "upd_check_stale_title";
    line.textEnglish = "Couldn't check for updates since {}";
    line.argument = StateArgument::LastSuccessDate;
    line.tone = StateTone::Warning;
    if (in.held) {
      line.detailKey = "upd_check_held_message";
      line.detailEnglish =
          "GitHub asked this network to wait until {} before it is asked again, and the app "
          "waits until then.";
      line.detailArgument = StateArgument::HoldEndTime;
    } else {
      line.detailKey = "upd_check_stale_message";
      line.detailEnglish =
          "A newer release may be out. The app keeps trying every six hours, and the button "
          "tries now.";
    }
    return line;
  }
  if (in.outcome == CheckOutcome::Failed && in.held) {
    line.textKey = "upd_check_held_message";
    line.textEnglish =
        "GitHub asked this network to wait until {} before it is asked again, and the app waits "
        "until then.";
    line.argument = StateArgument::HoldEndTime;
    line.tone = StateTone::Warning;
    return line;
  }
  switch (in.outcome) {
    case CheckOutcome::NeverRan:
      break;
    case CheckOutcome::InFlight:
      line.textKey = "dev_update_checking";
      line.textEnglish = "Checking for updates…";
      break;
    case CheckOutcome::NoUpdate:
      if (!in.newestKnown) {
        line.textKey = "dev_update_no_releases";
        line.textEnglish = "No stable release has been published yet.";
      } else if (!in.newestOutranksOwn) {
        line.textKey = "dev_update_up_to_date";
        line.textEnglish = "Up to date. Newest release: v{}";
        line.argument = StateArgument::NewestVersion;
      }
      break;
    case CheckOutcome::UpdateFound:
      break;
    case CheckOutcome::DevBuild:
      line.textKey = "dev_update_dev_build";
      line.textEnglish =
          "Development build: the newest release is v{}, and a development build is never "
          "updated automatically.";
      line.argument = StateArgument::NewestVersion;
      break;
    case CheckOutcome::Failed:
      line.textKey = "dev_update_check_failed";
      line.textEnglish = "The update check failed — see the app log.";
      line.tone = StateTone::Danger;
      break;
  }
  return line;
}

}  // namespace urnw::update
