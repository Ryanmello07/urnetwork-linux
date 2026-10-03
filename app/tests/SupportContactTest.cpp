// Where Settings sends a user who wants to reach the project: the Stay in
// touch card offers support@ur.io wherever it offers the Discord invite, which
// is unreachable in some regions, and the address row links it with mailto.
//
// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <string>

#include "SupportContact.hpp"
#include "TestHarness.hpp"

namespace {

using namespace urnw::support;

UR_TEST(stayInTouchOffersTheSupportAddressBesideDiscord) {
  UR_EXPECT_TRUE(OffersSupportEmailWithDiscord(kStayInTouchLinks));
  const auto discord =
      std::find(kStayInTouchLinks.begin(), kStayInTouchLinks.end(), StayInTouchLink::Discord);
  const auto email = std::find(kStayInTouchLinks.begin(), kStayInTouchLinks.end(),
                               StayInTouchLink::SupportEmail);
  UR_EXPECT_TRUE(discord != kStayInTouchLinks.end());
  // right under Discord
  UR_EXPECT_TRUE(email == discord + 1);
}

UR_TEST(stayInTouchDiscordAloneFailsTheRule) {
  const std::array<StayInTouchLink, 3> discordOnly = {
      StayInTouchLink::Discord, StayInTouchLink::DepinHub, StayInTouchLink::DepinHub};
  UR_EXPECT_FALSE(OffersSupportEmailWithDiscord(discordOnly));
}

UR_TEST(supportEmailRowLinksTheAddressWithMailto) {
  UR_EXPECT_TRUE(std::string(kSupportEmail) == "support@ur.io");
  UR_EXPECT_TRUE(std::string(kSupportEmailUrl) == "mailto:support@ur.io");
  UR_EXPECT_TRUE(SupportEmailMarkdown("Email support at") ==
                 "Email support at [support@ur.io](mailto:support@ur.io)");
}

}  // namespace
