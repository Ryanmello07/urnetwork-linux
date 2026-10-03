// Where Settings sends a user who wants to reach the project, decided pure so
// tests/SupportContactTest.cpp can pin it without GTK or the SDK.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace urnw::support {

constexpr const char* kSupportEmail = "support@ur.io";
constexpr const char* kSupportEmailUrl = "mailto:support@ur.io";

// One row of the About pane's Stay in touch card.
enum class StayInTouchLink {
  Discord,       // join_the_community_on_discord_https_discord_com
  SupportEmail,  // email_support_at + the address
  DepinHub,      // verified_project_on_depin_hub_https_depinhub_io
};

// The card's rows, in order. The Discord invite is unreachable in some
// regions, so the support address sits right under it.
constexpr std::array<StayInTouchLink, 3> kStayInTouchLinks = {
    StayInTouchLink::Discord,
    StayInTouchLink::SupportEmail,
    StayInTouchLink::DepinHub,
};

// Every place Discord is offered must offer the support address too.
template <std::size_t N>
bool OffersSupportEmailWithDiscord(const std::array<StayInTouchLink, N>& links) {
  const auto has = [&links](StayInTouchLink link) {
    return std::find(links.begin(), links.end(), link) != links.end();
  };
  return !has(StayInTouchLink::Discord) || has(StayInTouchLink::SupportEmail);
}

// The store's label ("Email support at") with the address as an inline
// markdown link, for MarkdownLinksToPango.
inline std::string SupportEmailMarkdown(const std::string& label) {
  return label + " [" + kSupportEmail + "](" + kSupportEmailUrl + ")";
}

}  // namespace urnw::support
