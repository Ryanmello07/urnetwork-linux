// The referral invitation the share buttons copy (support inbox 1698): the
// localized message, which names the code, then the code's ur.io/c link on
// its own line, in the shape of the sdk's ConnectLinkUrl for bonus=<code>.
// ur.io/c opens the link in the Android app (or Play, with the link as the
// install referrer) and in web signup everywhere else. The code stays in the
// message: installs without a Play referrer still type it. Pure so
// tests/ReferralShareTest.cpp covers it without GTK or the SDK.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace urnw {

// https://<link host>/c?bonus=<code>, or "" for an empty code or one that is
// not plain [A-Za-z0-9-] (server codes always are), so a code never adds a
// parameter to the link.
inline std::string ReferralLinkUrl(const std::string& linkHostName, const std::string& code) {
  const bool plain = !code.empty() && std::all_of(code.begin(), code.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-';
  });
  if (!plain) return std::string();
  return "https://" + linkHostName + "/c?bonus=" + code;
}

// The message, then the link on its own line; the message alone without a
// link.
inline std::string ReferralShareText(const std::string& message, const std::string& link) {
  if (link.empty()) return message;
  return message + "\n" + link;
}

}  // namespace urnw
