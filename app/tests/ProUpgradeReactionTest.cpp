// The free -> Pro upgrade keeps every provide control mode and celebrates once.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "ProUpgradeReaction.hpp"

#include <string>

namespace {

const char* const kProvideControlModes[] = {"auto", "always", "network", "never", "manual"};

#define UR_EXPECT_MODE(expected, actual)                                              \
  do {                                                                                \
    if ((expected) != (actual))                                                       \
      UR_FAIL(std::string("provide control mode: expected \"") + (expected) +        \
              "\", got \"" + (actual) + "\"");                                       \
  } while (0)

}  // namespace

UR_TEST(proUpgradeKeepsEveryProvideControlMode) {
  for (const char* mode : kProvideControlModes) {
    const urnw::ProUpgradeReaction reaction = urnw::ReactToProUpgrade(true, false, mode);
    UR_EXPECT_MODE(std::string(mode), reaction.provideControlMode);
    UR_EXPECT_TRUE(reaction.celebrate);
  }
}

UR_TEST(proUpgradeAlreadyCelebratedKeepsEveryProvideControlMode) {
  for (const char* mode : kProvideControlModes) {
    const urnw::ProUpgradeReaction reaction = urnw::ReactToProUpgrade(true, true, mode);
    UR_EXPECT_MODE(std::string(mode), reaction.provideControlMode);
    UR_EXPECT_FALSE(reaction.celebrate);
  }
}

UR_TEST(noUpgradeChangesNothing) {
  for (const char* mode : kProvideControlModes) {
    const urnw::ProUpgradeReaction reaction = urnw::ReactToProUpgrade(false, false, mode);
    UR_EXPECT_MODE(std::string(mode), reaction.provideControlMode);
    UR_EXPECT_FALSE(reaction.celebrate);
  }
}
