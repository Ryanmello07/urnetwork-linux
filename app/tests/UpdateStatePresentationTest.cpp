// The line under Settings > General > "Check for updates"
// (UpdateStatePresentation.hpp): each outcome of the last check in the store's
// words, what it fills in, its tone and whether Check now may run, nothing
// where the notice below speaks or "up to date" would not be true, the
// warnings for checks that have not reached GitHub for three days or that
// GitHub asked to wait, and every key with its English as po/en.po carries it.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "UpdateStatePresentation.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using namespace urnw::update;

bool Says(const UpdateStateLine& line, const std::string& key, const std::string& english) {
  return line.textKey == key && line.textEnglish == english;
}

bool SaysNothing(const UpdateStateLine& line) {
  return std::string(line.textKey).empty() && std::string(line.textEnglish).empty() &&
         line.argument == StateArgument::None && std::string(line.detailKey).empty() &&
         line.tone == StateTone::Muted;
}

UpdateStateLine LineFor(CheckOutcome outcome, bool newestKnown, bool newestOutranksOwn = false,
                        bool stale = false, bool held = false) {
  UpdateStateInputs in;
  in.outcome = outcome;
  in.newestKnown = newestKnown;
  in.newestOutranksOwn = newestOutranksOwn;
  in.stale = stale;
  in.held = held;
  return UpdateStateLineFor(in);
}

constexpr CheckOutcome kOutcomes[] = {CheckOutcome::NeverRan,    CheckOutcome::InFlight,
                                      CheckOutcome::NoUpdate,    CheckOutcome::UpdateFound,
                                      CheckOutcome::DevBuild,    CheckOutcome::Failed};

}  // namespace

UR_TEST(UpdateState_NothingIsSaidBeforeACheckHasRun) {
  for (const bool newestKnown : {false, true}) {
    const UpdateStateLine line = LineFor(CheckOutcome::NeverRan, newestKnown);
    UR_EXPECT_TRUE(SaysNothing(line));
    UR_EXPECT_TRUE(line.canCheck);
  }
}

UR_TEST(UpdateState_ACheckInFlightSaysSoAndHoldsCheckNow) {
  const UpdateStateLine line = LineFor(CheckOutcome::InFlight, true);
  UR_EXPECT_TRUE(Says(line, "dev_update_checking", "Checking for updates…"));
  UR_EXPECT_FALSE(line.canCheck);
  UR_EXPECT_TRUE(line.tone == StateTone::Muted);
}

UR_TEST(UpdateState_UpToDateNamesTheNewestReleaseOrTheEmptyRepo) {
  const UpdateStateLine current = LineFor(CheckOutcome::NoUpdate, true);
  UR_EXPECT_TRUE(Says(current, "dev_update_up_to_date", "Up to date. Newest release: v{}"));
  UR_EXPECT_TRUE(current.argument == StateArgument::NewestVersion);
  UR_EXPECT_TRUE(current.canCheck);
  // urnetwork/linux before its first stable release
  const UpdateStateLine empty = LineFor(CheckOutcome::NoUpdate, false);
  UR_EXPECT_TRUE(
      Says(empty, "dev_update_no_releases", "No stable release has been published yet."));
  UR_EXPECT_TRUE(empty.argument == StateArgument::None);
}

UR_TEST(UpdateState_AnOfferedReleaseIsLeftToTheNoticeBelow) {
  // the update notice under the line names it ("Update available: v...",
  // "Ready to relaunch: v..."), and saying it twice in a row helps nobody;
  // never the developer line's "see Settings"
  for (const bool newestOutranksOwn : {false, true}) {
    const UpdateStateLine line = LineFor(CheckOutcome::UpdateFound, true, newestOutranksOwn);
    UR_EXPECT_TRUE(SaysNothing(line));
    UR_EXPECT_TRUE(line.canCheck);
  }
}

UR_TEST(UpdateState_ANewerReleaseThisInstallIsNotOfferedIsNotUpToDate) {
  // the newest release outranks this build but was skipped (no file for
  // this install, no usable digest, an install kind that cannot update):
  // "Up to date. Newest release: v<newer>" would not be true
  const UpdateStateLine line = LineFor(CheckOutcome::NoUpdate, true, /*newestOutranksOwn=*/true);
  UR_EXPECT_TRUE(SaysNothing(line));
  UR_EXPECT_TRUE(line.canCheck);
  // it changes nothing else: every release outranks a dev build, and a
  // failure says so either way
  UR_EXPECT_TRUE(Says(LineFor(CheckOutcome::DevBuild, true, true), "dev_update_dev_build",
                      "Development build: the newest release is v{}, and a development build "
                      "is never updated automatically."));
  UR_EXPECT_TRUE(Says(LineFor(CheckOutcome::Failed, true, true), "dev_update_check_failed",
                      "The update check failed — see the app log."));
}

UR_TEST(UpdateState_ADevBuildNamesTheNewestReleaseAndIsNeverOfferedIt) {
  const UpdateStateLine line = LineFor(CheckOutcome::DevBuild, true);
  UR_EXPECT_TRUE(Says(line, "dev_update_dev_build",
                      "Development build: the newest release is v{}, and a development build "
                      "is never updated automatically."));
  UR_EXPECT_TRUE(line.argument == StateArgument::NewestVersion);
}

UR_TEST(UpdateState_AFailedCheckReadsInTheDangerToneAndMayBeRetried) {
  const UpdateStateLine line = LineFor(CheckOutcome::Failed, false);
  UR_EXPECT_TRUE(
      Says(line, "dev_update_check_failed", "The update check failed — see the app log."));
  UR_EXPECT_TRUE(line.tone == StateTone::Danger);
  UR_EXPECT_TRUE(std::string(line.detailKey).empty());
  UR_EXPECT_TRUE(line.canCheck);
}

UR_TEST(UpdateState_ChecksThatHaveNotReachedGitHubForThreeDaysAreSaid) {
  const UpdateStateLine line = LineFor(CheckOutcome::Failed, false, false, /*stale=*/true);
  UR_EXPECT_TRUE(Says(line, "upd_check_stale_title", "Couldn't check for updates since {}"));
  UR_EXPECT_TRUE(line.argument == StateArgument::LastSuccessDate);
  UR_EXPECT_TRUE(line.tone == StateTone::Warning);
  UR_EXPECT_TRUE(std::string(line.detailKey) == "upd_check_stale_message");
  UR_EXPECT_TRUE(std::string(line.detailEnglish) ==
                 "A newer release may be out. The app keeps trying every six hours, and the "
                 "button tries now.");
  UR_EXPECT_TRUE(line.detailArgument == StateArgument::None);
  UR_EXPECT_TRUE(line.canCheck);  // "the button tries now"
}

UR_TEST(UpdateState_WhileGitHubAsksToWaitTheLineSaysUntilWhenAndCheckNowWaits) {
  // stale and held: the warning, and instead of a promise that the button
  // tries, until when nothing will
  const UpdateStateLine stale = LineFor(CheckOutcome::Failed, true, false, true, /*held=*/true);
  UR_EXPECT_TRUE(Says(stale, "upd_check_stale_title", "Couldn't check for updates since {}"));
  UR_EXPECT_TRUE(std::string(stale.detailKey) == "upd_check_held_message");
  UR_EXPECT_TRUE(std::string(stale.detailEnglish) ==
                 "GitHub asked this network to wait until {} before it is asked again, and the "
                 "app waits until then.");
  UR_EXPECT_TRUE(stale.detailArgument == StateArgument::HoldEndTime);
  UR_EXPECT_FALSE(stale.canCheck);
  // held without three days of failures: the hold alone, as a warning
  const UpdateStateLine held = LineFor(CheckOutcome::Failed, true, false, false, true);
  UR_EXPECT_TRUE(Says(held, "upd_check_held_message",
                      "GitHub asked this network to wait until {} before it is asked again, and "
                      "the app waits until then."));
  UR_EXPECT_TRUE(held.argument == StateArgument::HoldEndTime);
  UR_EXPECT_TRUE(held.tone == StateTone::Warning);
  UR_EXPECT_TRUE(std::string(held.detailKey).empty());
  UR_EXPECT_FALSE(held.canCheck);
  // Check now waits for the hold whatever the outcome reads
  for (const CheckOutcome outcome : kOutcomes) {
    UR_EXPECT_FALSE(LineFor(outcome, true, false, false, true).canCheck);
  }
}

UR_TEST(UpdateState_StaleAndHeldAreSaidOnlyOfAFailedCheck) {
  // a check in flight says so, and a success has already cleared both
  for (const CheckOutcome outcome : kOutcomes) {
    if (outcome == CheckOutcome::Failed) continue;
    const UpdateStateLine plain = LineFor(outcome, true);
    const UpdateStateLine flagged = LineFor(outcome, true, false, true, true);
    UR_EXPECT_TRUE(std::string(plain.textKey) == flagged.textKey);
    UR_EXPECT_TRUE(std::string(flagged.detailKey).empty());
  }
}

// Every key and English source the line can show is the catalog's, byte for
// byte (I18n.hpp: the English text is the msgid the catalog is keyed on).
UR_TEST(UpdateState_KeysAreTheCatalogs) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../po/en.po", std::ios::binary);
  UR_EXPECT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string catalog = buffer.str();
  std::set<std::string> keys;
  const auto expectInCatalog = [&](const char* key, const char* english) {
    if (key[0] == '\0') return;
    keys.insert(key);
    const std::string entry =
        std::string("msgctxt \"") + key + "\"\nmsgid \"" + english + "\"\n";
    UR_EXPECT_TRUE_MSG(entry, catalog.find(entry) != std::string::npos);
  };
  for (const CheckOutcome outcome : kOutcomes) {
    for (const bool newestKnown : {false, true}) {
      for (const bool newestOutranksOwn : {false, true}) {
        for (const bool stale : {false, true}) {
          for (const bool held : {false, true}) {
            const UpdateStateLine line =
                LineFor(outcome, newestKnown, newestOutranksOwn, stale, held);
            expectInCatalog(line.textKey, line.textEnglish);
            expectInCatalog(line.detailKey, line.detailEnglish);
          }
        }
      }
    }
  }
  // checking, up to date, no releases yet, dev build, failed, and the stale
  // title, its message and the hold
  UR_EXPECT_EQ(8, static_cast<int>(keys.size()));
}
