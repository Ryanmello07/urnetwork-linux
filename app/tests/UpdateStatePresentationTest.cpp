// The line under Settings > General > "Check for updates"
// (UpdateStatePresentation.hpp): each outcome of the last check in the store's
// words, the version it names, its tone and whether Check now may run,
// nothing where the notice below speaks or "up to date" would not be true,
// and every key with its English as po/en.po carries it.
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
         line.version == StateVersion::None && !line.failed;
}

UpdateStateLine LineFor(CheckOutcome outcome, bool newestKnown, bool newestOutranksOwn = false) {
  UpdateStateInputs in;
  in.outcome = outcome;
  in.newestKnown = newestKnown;
  in.newestOutranksOwn = newestOutranksOwn;
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
  UR_EXPECT_FALSE(line.failed);
}

UR_TEST(UpdateState_UpToDateNamesTheNewestReleaseOrTheEmptyRepo) {
  const UpdateStateLine current = LineFor(CheckOutcome::NoUpdate, true);
  UR_EXPECT_TRUE(Says(current, "dev_update_up_to_date", "Up to date. Newest release: v{}"));
  UR_EXPECT_TRUE(current.version == StateVersion::Newest);
  UR_EXPECT_TRUE(current.canCheck);
  // urnetwork/linux before its first stable release
  const UpdateStateLine empty = LineFor(CheckOutcome::NoUpdate, false);
  UR_EXPECT_TRUE(
      Says(empty, "dev_update_no_releases", "No stable release has been published yet."));
  UR_EXPECT_TRUE(empty.version == StateVersion::None);
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
  UR_EXPECT_TRUE(line.version == StateVersion::Newest);
}

UR_TEST(UpdateState_AFailedCheckReadsInTheDangerToneAndMayBeRetried) {
  const UpdateStateLine line = LineFor(CheckOutcome::Failed, false);
  UR_EXPECT_TRUE(
      Says(line, "dev_update_check_failed", "The update check failed — see the app log."));
  UR_EXPECT_TRUE(line.failed);
  UR_EXPECT_TRUE(line.canCheck);
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
  for (const CheckOutcome outcome : kOutcomes) {
    for (const bool newestKnown : {false, true}) {
      for (const bool newestOutranksOwn : {false, true}) {
        const UpdateStateLine line = LineFor(outcome, newestKnown, newestOutranksOwn);
        if (line.textKey[0] == '\0') continue;
        keys.insert(line.textKey);
        const std::string entry = std::string("msgctxt \"") + line.textKey + "\"\nmsgid \"" +
                                  line.textEnglish + "\"\n";
        UR_EXPECT_TRUE_MSG(entry, catalog.find(entry) != std::string::npos);
      }
    }
  }
  // checking, up to date, no releases yet, dev build, failed
  UR_EXPECT_EQ(5, static_cast<int>(keys.size()));
}
