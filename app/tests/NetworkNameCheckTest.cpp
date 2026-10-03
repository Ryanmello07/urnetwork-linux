// The create-network form's network name check: a failed check is neither
// "taken" nor a reason to hold Continue disabled.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "NetworkNameCheck.hpp"

using urnw::NetworkNameState;

UR_TEST(failedNetworkNameCheckIsNotTaken) {
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(false, false) == NetworkNameState::CheckFailed);
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(false, true) == NetworkNameState::CheckFailed);
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(true, false) == NetworkNameState::Taken);
  UR_EXPECT_TRUE(urnw::NetworkNameStateForCheck(true, true) == NetworkNameState::Valid);
}

UR_TEST(failedNetworkNameCheckKeepsCreateUsable) {
  UR_EXPECT_TRUE(urnw::NetworkNameAllowsCreate(NetworkNameState::Valid));
  UR_EXPECT_TRUE(urnw::NetworkNameAllowsCreate(NetworkNameState::CheckFailed));
  UR_EXPECT_FALSE(urnw::NetworkNameAllowsCreate(NetworkNameState::Taken));
  UR_EXPECT_FALSE(urnw::NetworkNameAllowsCreate(NetworkNameState::Validating));
  UR_EXPECT_FALSE(urnw::NetworkNameAllowsCreate(NetworkNameState::NotChecked));
}

UR_TEST(failedNetworkNameCheckIsRetriedABoundedNumberOfTimes) {
  UR_EXPECT_TRUE(urnw::NetworkNameRecheckDelayMs(0) == 0u);
  UR_EXPECT_TRUE(0u < urnw::NetworkNameRecheckDelayMs(1));
  UR_EXPECT_TRUE(urnw::NetworkNameRecheckDelayMs(1) <= urnw::NetworkNameRecheckDelayMs(2));
  UR_EXPECT_TRUE(0u < urnw::NetworkNameRecheckDelayMs(3));
  UR_EXPECT_TRUE(urnw::NetworkNameRecheckDelayMs(4) == 0u);
}
