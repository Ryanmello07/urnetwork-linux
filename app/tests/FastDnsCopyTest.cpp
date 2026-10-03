// The host-network dns fallback is the opt-in "Fast DNS on connect" setting, and its copy says
// what it costs: lookups answered over the local network while the tunnel's dns starts.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "FastDnsCopy.hpp"

namespace urnw {
namespace {

UR_TEST(FastDnsLabelIsTheOptInSetting) {
  UR_EXPECT_TRUE(std::string(kFastDnsLabel.key) == "fast_dns_on_connect");
  UR_EXPECT_TRUE(std::string(kFastDnsLabel.english) == "Fast DNS on connect");
}

UR_TEST(FastDnsDescriptionNamesTheLocalNetworkExposure) {
  const std::string english = kFastDnsDescription.english;
  UR_EXPECT_TRUE(std::string(kFastDnsDescription.key) == "fast_dns_on_connect_description");
  UR_EXPECT_TRUE(english.find("over the local network while the tunnel's DNS starts") !=
                 std::string::npos);
  UR_EXPECT_TRUE(english.find("reveal your lookups to the local network") != std::string::npos);
  UR_EXPECT_TRUE(english.find("When off, DNS only resolves through the tunnel") !=
                 std::string::npos);
}

}  // namespace
}  // namespace urnw
