// The past-due notice of a failed renewal and its "Update payment method"
// (PaymentPastDue.hpp). The notice shows exactly while the balance carries
// payment_past_due, and stays hidden for a balance without it, an older
// server's included. The action opens ur.io's Manage Subscription with the
// one-time auth code percent-encoded as its one auth_code value, as apple's
// manageSubscriptionOnWebURL(authCode:) builds it, or without a code when none
// could be created; its base is the app's constant, never the wire's
// update_payment_url. The store, the window and the Account page need GTK and
// the SDK, so their wiring is read as source.
//
// SPDX-License-Identifier: MPL-2.0
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "PaymentPastDue.hpp"
#include "TestHarness.hpp"
#include "WiringSource.hpp"

using urnw::past_due::EncodeQueryValue;
using urnw::past_due::kManageSubscriptionUrl;
using urnw::past_due::NoticeShows;
using urnw::past_due::UpdatePaymentUrl;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::CountOf;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

namespace {

// urnet::PaymentPastDue and the fields of urnet::SubscriptionBalanceResult
// read here, by the generated urnetwork_sdk.hpp's names and types, with its
// generated json conversions: an absent or null key leaves the field unset,
// and an unknown key is skipped.
struct PaymentPastDue {
  std::string store{};
  std::string fail_time{};
  std::string update_payment_url{};
};

struct SubscriptionBalanceResult {
  int64_t start_balance_byte_count{};
  int64_t balance_byte_count{};
  int64_t open_transfer_byte_count{};
  std::string update_time{};
  std::optional<PaymentPastDue> payment_past_due;
};

template <class T>
void TakePlain(const nlohmann::json& j, const char* name, T& value) {
  if (auto it = j.find(name); it != j.end() && !it->is_null()) it->get_to(value);
}

void from_json(const nlohmann::json& j, PaymentPastDue& v) {
  if (!j.is_object()) return;
  TakePlain(j, "store", v.store);
  TakePlain(j, "fail_time", v.fail_time);
  TakePlain(j, "update_payment_url", v.update_payment_url);
}

void from_json(const nlohmann::json& j, SubscriptionBalanceResult& v) {
  if (!j.is_object()) return;
  TakePlain(j, "start_balance_byte_count", v.start_balance_byte_count);
  TakePlain(j, "balance_byte_count", v.balance_byte_count);
  TakePlain(j, "open_transfer_byte_count", v.open_transfer_byte_count);
  TakePlain(j, "update_time", v.update_time);
  if (auto it = j.find("payment_past_due"); it != j.end() && !it->is_null()) {
    PaymentPastDue pastDue{};
    it->get_to(pastDue);
    v.payment_past_due = std::move(pastDue);
  }
}

SubscriptionBalanceResult ParseBalance(const char* text) {
  return nlohmann::json::parse(text).get<SubscriptionBalanceResult>();
}

// A Pro network whose Stripe renewal is failing. The wire's
// update_payment_url is a host the app must never open.
constexpr const char* kPastDueBalanceJson = R"({
  "start_balance_byte_count": 1073741824,
  "balance_byte_count": 536870912,
  "open_transfer_byte_count": 0,
  "current_subscription": {"subscription_id": "sub-test-1", "store": "stripe", "plan": "monthly"},
  "update_time": "2026-10-26T12:00:00Z",
  "payment_past_due": {
    "store": "stripe",
    "fail_time": "2026-10-20T08:00:00Z",
    "update_payment_url": "https://billing.example/update"
  }
})";

// The same network from a server that predates payment_past_due.
constexpr const char* kOlderServerBalanceJson = R"({
  "start_balance_byte_count": 1073741824,
  "balance_byte_count": 536870912,
  "open_transfer_byte_count": 0,
  "current_subscription": {"subscription_id": "sub-test-1", "store": "stripe", "plan": "monthly"},
  "update_time": "2026-10-26T12:00:00Z"
})";

// A server that writes the field out as null.
constexpr const char* kNullPastDueBalanceJson = R"({
  "start_balance_byte_count": 1073741824,
  "balance_byte_count": 536870912,
  "update_time": "2026-10-26T12:00:00Z",
  "payment_past_due": null
})";

// Each needle occurs in `text`, in this order.
bool InOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    const size_t found = text.find(needle, at);
    if (found == std::string::npos) {
      UR_FAIL(std::string("missing or out of order: ") + needle);
      return false;
    }
    at = found + 1;
  }
  return true;
}

bool Has(const std::string& text, const std::string& needle) {
  if (Contains(text, needle)) return true;
  UR_FAIL("missing: " + needle);
  return false;
}

// Every .cpp, .hpp and .h under src/ with its comments blanked, by its path
// under src/, read once for the tests that scan them all; empty when src/ is
// not there.
const std::vector<std::pair<std::string, std::string>>& EverySource() {
  static const std::vector<std::pair<std::string, std::string>> sources = [] {
    std::vector<std::pair<std::string, std::string>> read;
    const std::filesystem::path src(UR_SRC_DIR);
    if (!std::filesystem::is_directory(src)) return read;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
      if (!entry.is_regular_file()) continue;
      const std::string ext = entry.path().extension().string();
      if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
      const std::string name = std::filesystem::relative(entry.path(), src).string();
      read.emplace_back(name, ReadCode(name));
    }
    return read;
  }();
  return sources;
}

}  // namespace

UR_TEST(paymentPastDueNoticeShowsWhileTheBalanceCarriesIt) {
  const SubscriptionBalanceResult result = ParseBalance(kPastDueBalanceJson);
  UR_EXPECT_TRUE(result.payment_past_due.has_value());
  UR_EXPECT_TRUE(NoticeShows(result));
}

// a renewal that is not failing leaves the field out, and so does a server
// from before it; one written out as null is the same
UR_TEST(paymentPastDueNoticeHiddenWithoutIt) {
  UR_EXPECT_FALSE(NoticeShows(ParseBalance(kOlderServerBalanceJson)));
  UR_EXPECT_FALSE(NoticeShows(ParseBalance(kNullPastDueBalanceJson)));
  UR_EXPECT_FALSE(NoticeShows(SubscriptionBalanceResult{}));
}

// no time rule: while the server reports the renewal failing, the notice
// shows, whenever it failed and whatever store it names
UR_TEST(paymentPastDueNoticeHasNoTimeRule) {
  for (const char* failTime : {"2026-10-20T08:00:00Z", "2001-01-01T00:00:00Z",
                               "2099-12-31T23:59:59Z", ""}) {
    PaymentPastDue pastDue{};
    pastDue.store = "stripe";
    pastDue.fail_time = failTime;
    SubscriptionBalanceResult result{};
    result.payment_past_due = pastDue;
    UR_EXPECT_TRUE_MSG(failTime, NoticeShows(result));
  }
  SubscriptionBalanceResult unnamed{};
  unnamed.payment_past_due = PaymentPastDue{};
  UR_EXPECT_TRUE(NoticeShows(unnamed));
}

UR_TEST(paymentPastDueUpdatePaymentOpensManageSubscriptionWithTheCode) {
  // the server's codes are base64url with "=" padding
  UR_EXPECT_TRUE(UpdatePaymentUrl("AbC-dEf_123=") ==
                 "https://ur.io/app/subscription?auth_code=AbC-dEf_123%3D");
  UR_EXPECT_TRUE(UpdatePaymentUrl("Zm9vYmFy") == "https://ur.io/app/subscription?auth_code=Zm9vYmFy");
}

// "&", "=", "+", "#", "?" and spaces stay inside the one value: the code adds
// no parameter and no fragment, and "+" never reads as a space
UR_TEST(paymentPastDueCodeCannotChangeTheLink) {
  const std::string url = UpdatePaymentUrl("a&next=x#y+z?w v");
  UR_EXPECT_TRUE(url ==
                 "https://ur.io/app/subscription?auth_code=a%26next%3Dx%23y%2Bz%3Fw%20v");
  UR_EXPECT_EQ(size_t{1}, CountOf(url, "?"));
  UR_EXPECT_EQ(size_t{1}, CountOf(url, "="));
  UR_EXPECT_EQ(size_t{0}, CountOf(url, "&"));
  UR_EXPECT_EQ(size_t{0}, CountOf(url, "#"));
  UR_EXPECT_EQ(size_t{0}, CountOf(url, "+"));
  UR_EXPECT_EQ(size_t{0}, CountOf(url, " "));
}

// apple's encoding, byte for byte: urlQueryAllowed without & = + # ?
UR_TEST(paymentPastDueCodeEncodesAsApple) {
  // letters, digits and these marks stay
  UR_EXPECT_TRUE(EncodeQueryValue("AZaz09") == "AZaz09");
  UR_EXPECT_TRUE(EncodeQueryValue("!$'()*,-./:;@_~") == "!$'()*,-./:;@_~");
  // every other ascii mark, "%" included, is encoded
  UR_EXPECT_TRUE(EncodeQueryValue("&=+#? %") == "%26%3D%2B%23%3F%20%25");
  UR_EXPECT_TRUE(EncodeQueryValue("\"<>[\\]^`{|}") == "%22%3C%3E%5B%5C%5D%5E%60%7B%7C%7D");
  // and so is a control byte, and each byte of a non-ascii character
  UR_EXPECT_TRUE(EncodeQueryValue(std::string("\n\t\x7f\0", 4)) == "%0A%09%7F%00");
  UR_EXPECT_TRUE(EncodeQueryValue("\xC3\xA9") == "%C3%A9");
}

// a code that could not be created (an error, or a blank answer) opens the
// page without one, and ur.io asks the user to sign in
UR_TEST(paymentPastDueBlankCodeOpensThePageWithoutOne) {
  UR_EXPECT_TRUE(UpdatePaymentUrl("") == "https://ur.io/app/subscription");
  UR_EXPECT_TRUE(UpdatePaymentUrl(" \n\t") == "https://ur.io/app/subscription");
  UR_EXPECT_FALSE(Contains(UpdatePaymentUrl(""), "auth_code"));
  // the white space around a code is not part of it
  UR_EXPECT_TRUE(UpdatePaymentUrl(" AbC_1=\n") == "https://ur.io/app/subscription?auth_code=AbC_1%3D");
}

// the base is the app's constant whatever the wire says, and no source of the
// app reads the wire's url at all: a sign-in code is never attached to a url
// the server supplied
UR_TEST(paymentPastDueLinkBaseIsNeverTheWires) {
  UR_EXPECT_TRUE(std::string(kManageSubscriptionUrl) == "https://ur.io/app/subscription");
  const SubscriptionBalanceResult result = ParseBalance(kPastDueBalanceJson);
  UR_EXPECT_TRUE(result.payment_past_due->update_payment_url == "https://billing.example/update");
  for (const char* code : {"", "AbC-dEf_123=", "a&next=x"}) {
    const std::string url = UpdatePaymentUrl(code);
    UR_EXPECT_TRUE_MSG(code, url.rfind(kManageSubscriptionUrl, 0) == 0);
    UR_EXPECT_TRUE_MSG(code, !Contains(url, "billing.example"));
  }
  UR_EXPECT_TRUE_MSG("the scan saw the sources", EverySource().size() > 10);
  for (const auto& [name, code] : EverySource()) {
    UR_EXPECT_TRUE_MSG(name + " reads the wire's update_payment_url",
                       !Contains(code, "update_payment_url"));
  }
}

// The store maps the SDK's result through NoticeShows, in one place, and
// forgets it with the session.
UR_TEST(paymentPastDueStoreMapsTheResultOnce) {
  const std::string store = ReadCode("SubscriptionBalance.cpp");
  UR_EXPECT_TRUE(!store.empty());
  UR_EXPECT_EQ(size_t{1}, CountOf(store, "past_due::NoticeShows("));
  Has(FunctionBody(store, "void SubscriptionBalanceStore::FetchSubscriptionBalance() {"),
      "paymentPastDue_ = past_due::NoticeShows(*result);");
  Has(FunctionBody(store, "void SubscriptionBalanceStore::Start() {"), "paymentPastDue_ = false;");
  Has(FunctionBody(store, "void SubscriptionBalanceStore::Stop() {"), "paymentPastDue_ = false;");
  Has(ReadCode("SubscriptionBalance.hpp"), "bool PaymentPastDue() const { return paymentPastDue_; }");
  // no other source decides it
  UR_EXPECT_TRUE_MSG("the scan saw the sources", EverySource().size() > 10);
  size_t calls = 0;
  for (const auto& [name, code] : EverySource()) {
    if (name != "PaymentPastDue.hpp") calls += CountOf(code, "NoticeShows(");
  }
  UR_EXPECT_EQ(size_t{1}, calls);
}

// The window relays it to Account with the rest of the snapshot.
UR_TEST(paymentPastDueWindowRelaysItToAccount) {
  const std::string window = ReadCode("MainWindow.cpp");
  InOrder(window, {"snapshot.paymentPastDue = balance_.PaymentPastDue();",
                   "accountPage_->ApplyBalance(snapshot);"});
}

// Account's plan pane shows the notice exactly while the snapshot carries it,
// under the plan value and above the crypto year's notice, in the crypto
// notice's styling: the caption line, then the pane's primary button.
UR_TEST(paymentPastDueAccountShowsTheNotice) {
  const std::string page = ReadCode("AccountPage.cpp");
  const std::string apply =
      FunctionBody(page, "void AccountPage::ApplyBalance(const AccountBalance& snapshot) {");
  Has(apply, "if (paymentPastDueRow_) paymentPastDueRow_->set_visible(balance_.paymentPastDue);");
  const std::string build = FunctionBody(page, "void AccountPage::BuildPlanPane() {");
  InOrder(build,
          {"planValue_->add_controller(planTap);", "T_(\"payment_past_due_notice\",",
           "\"Your UR Pro payment didn't go through. Update your payment method to keep Pro.\")",
           "line->add_css_class(\"ur-caption\");",
           "T_(\"payment_past_due_update_payment\", \"Update payment method\")",
           "updatePaymentButton_->add_css_class(\"ur-pane-primary\");",
           "updatePaymentButton_->signal_clicked().connect([this] { OpenUpdatePayment(); });",
           "paymentPastDueRow_->set_visible(false);", "T_(\"crypto_term_renew\", \"Renew\")"});
}

// The action creates a one-time auth code, then opens Manage Subscription with
// it, or without one when it could not be created: an error, a blank code or
// no answer in time. An answer for a session that has ended opens nothing, and
// neither the code nor the link carrying it is logged.
UR_TEST(paymentPastDueUpdatePaymentSignsInWithAOneTimeCode) {
  const std::string page = ReadCode("AccountPage.cpp");
  const std::string open = FunctionBody(page, "void AccountPage::OpenUpdatePayment() {");
  InOrder(open, {"if (openingUpdatePayment_) return;", "if (!CanCallApi()) {",
                 "openingUpdatePayment_ = true;", "updatePaymentButton_->set_sensitive(false);",
                 "args.duration_minutes = kAuthCodeMinutes;", "args.uses = kAuthCodeUses;",
                 "updatePaymentFlow_.Begin(kApiTimeoutMs, [this] { FinishUpdatePayment(std::string()); });",
                 "host_.api().authCodeCreate(", "if (*epoch != seen) return;",
                 "if (!updatePaymentFlow_.Settle(flow,",
                 "const std::string code = result && !err && !result->error",
                 "? result->auth_code.value_or(std::string())", ": std::string();",
                 "FinishUpdatePayment(code);"});
  UR_EXPECT_FALSE(Contains(open, "code.c_str()"));
  const std::string finish =
      FunctionBody(page, "void AccountPage::FinishUpdatePayment(const std::string& authCode) {");
  InOrder(finish, {"openingUpdatePayment_ = false;", "updatePaymentButton_->set_sensitive(true);",
                   "const std::string url = past_due::UpdatePaymentUrl(authCode);",
                   "g_app_info_launch_default_for_uri(url.c_str(), nullptr, &launchError)",
                   "T_(\"something_went_wrong\", \"Something went wrong.\")"});
  UR_EXPECT_FALSE(Contains(finish, "authCode.c_str()"));
  UR_EXPECT_EQ(size_t{1}, CountOf(finish, "url.c_str()"));
  // a sign-out or a reload drops the code in flight and frees the button
  const std::string release = FunctionBody(page, "void AccountPage::ReleaseInFlight() {");
  InOrder(release, {"updatePaymentFlow_.Abandon();", "openingUpdatePayment_ = false;",
                    "updatePaymentButton_->set_sensitive(true);"});
  Has(FunctionBody(page, "AccountPage::~AccountPage() {"), "updatePaymentFlow_.Abandon();");
}
