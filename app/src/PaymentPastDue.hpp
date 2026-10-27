// The past-due notice of a renewal whose payment did not go through, and the
// link its "Update payment method" opens, decided pure so
// tests/PaymentPastDueTest.cpp pins them without GTK or the SDK.
//
// Stripe retries a declined renewal for about two weeks, then cancels the
// subscription. While it retries, GET /subscription/balance carries
// payment_past_due ({store, fail_time, update_payment_url}); the field is left
// out when no renewal is failing, and by an older server. The notice shows
// exactly while the latest balance carries it: there is no time rule and no
// state of the app's own. A network with no email (a wallet or phone sign-in)
// hears of the failed renewal only here.
//
// "Update payment method" opens ur.io's Manage Subscription in the browser,
// signed in with a one-time auth code (POST /auth/code-create), the link
// apple's manageSubscriptionOnWebURL(authCode:) builds. ur.io spends the code,
// removes it from the address bar, and asks before it switches a browser that
// is signed in to another network. Without a code the page asks the user to
// sign in. The base is the constant below, never the wire's
// update_payment_url (the same page today), so a sign-in code is never
// attached to a url the server supplied.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <string_view>

namespace urnw::past_due {

// ur.io's Manage Subscription, whose "Update payment method" opens the store's
// billing portal for the signed-in network
inline constexpr const char* kManageSubscriptionUrl = "https://ur.io/app/subscription";

// The notice shows: `result` (urnet::SubscriptionBalanceResult) carries
// payment_past_due. The balance store maps the SDK's result through this alone.
template <class BalanceResult>
bool NoticeShows(const BalanceResult& result) {
  return result.payment_past_due.has_value();
}

// `value` percent-encoded as one query value, byte for byte as apple encodes
// the code (urlQueryAllowed without & = + # ?): an ascii letter or digit and
// ! $ ' ( ) * , - . / : ; @ _ ~ stay, and every other byte, each byte of a
// non-ascii character included, becomes %XX. So no character can add a
// parameter or a fragment, and "+" never reads as a space.
inline std::string EncodeQueryValue(const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  static constexpr std::string_view kKeptMarks = "!$'()*,-./:;@_~";
  std::string encoded;
  encoded.reserve(value.size() * 3);
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    const bool kept = ('a' <= byte && byte <= 'z') || ('A' <= byte && byte <= 'Z') ||
                      ('0' <= byte && byte <= '9') || kKeptMarks.find(c) != std::string_view::npos;
    if (kept) {
      encoded += c;
    } else {
      encoded += '%';
      encoded += kHex[byte >> 4];
      encoded += kHex[byte & 0x0F];
    }
  }
  return encoded;
}

// The link "Update payment method" opens: Manage Subscription carrying
// `authCode` as its one auth_code parameter, or the page alone for a code that
// is blank once the ascii white space around it is trimmed (the code could not
// be created).
inline std::string UpdatePaymentUrl(const std::string& authCode) {
  static constexpr const char* kWhiteSpace = " \t\n\v\f\r";
  const size_t first = authCode.find_first_not_of(kWhiteSpace);
  if (first == std::string::npos) return kManageSubscriptionUrl;
  const size_t last = authCode.find_last_not_of(kWhiteSpace);
  return std::string(kManageSubscriptionUrl) + "?auth_code=" +
         EncodeQueryValue(authCode.substr(first, last - first + 1));
}

}  // namespace urnw::past_due
