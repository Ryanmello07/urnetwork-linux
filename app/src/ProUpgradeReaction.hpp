// The window's reaction to the balance store detecting a free -> Pro upgrade,
// kept free of GTK so it is unit tested (tests/ProUpgradeReactionTest.cpp).
//
// Upgrading to Pro never changes the provide control mode: the user's choice
// stands, and a silent reset to Never stops a paying user earning (in-app
// feedback theme earnings-not-updating). The upgrade only plays the Pro
// celebration, once per session.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace urnw {

struct ProUpgradeReaction {
  bool celebrate = false;
  // the provide control mode to hold after the upgrade
  std::string provideControlMode;
};

// detectedUpgrade: the store saw this session's free -> Pro flip.
// celebrated: the celebration already played this session.
inline ProUpgradeReaction ReactToProUpgrade(bool detectedUpgrade, bool celebrated,
                                            const std::string& provideControlMode) {
  ProUpgradeReaction reaction;
  reaction.celebrate = detectedUpgrade && !celebrated;
  reaction.provideControlMode = provideControlMode;
  return reaction;
}

}  // namespace urnw
