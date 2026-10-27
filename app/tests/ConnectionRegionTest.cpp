// The connection region picker's model, its one stored value shared by the
// login control and the DNS settings row, and the can't-reach prompt's
// classification (src/ConnectionRegion.hpp). Support inbox 7956, 7957, 7973:
// users behind networks that block URnetwork's own names had no way to say
// where they connect from.
// SPDX-License-Identifier: MPL-2.0
#include "ConnectionRegion.hpp"

#include <string>
#include <vector>

#include "TestHarness.hpp"

namespace {

using urnw::region::CantReachPrompt;
using urnw::region::IsCantReachError;
using urnw::region::Option;
using urnw::region::Options;
using urnw::region::Picker;
using urnw::region::Store;

// The SDK's NetworkSpace behaviour: a code without a recommendation is stored
// as undisclosed.
class FakeStore : public Store {
 public:
  explicit FakeStore(std::vector<std::string> known) : known_(std::move(known)) {}
  std::string Get() override { return value; }
  std::string Set(const std::string& countryCode) override {
    ++sets;
    value.clear();
    for (const auto& k : known_) {
      if (k == countryCode) value = countryCode;
    }
    return value;
  }
  std::string value;
  int sets = 0;

 private:
  std::vector<std::string> known_;
};

const std::vector<std::string> kSdkCodes = {"cn", "ir", "kz", "ru", "tm", "tr"};

std::string Named(const std::string& code) { return "Country " + code; }

}  // namespace

UR_TEST(ConnectionRegion_OptionsAreExactlyTheSdkRegionsThenUndisclosed) {
  const auto options = Options({"CN", "ir", "", "cn", "kz"}, Named, "Other / Undisclosed");
  const std::vector<Option> expected = {
      {"cn", "Country cn"}, {"ir", "Country ir"}, {"kz", "Country kz"}, {"", "Other / Undisclosed"}};
  UR_EXPECT_TRUE(options == expected);
}

UR_TEST(ConnectionRegion_OptionsFallBackToTheCodeWithoutAName) {
  const auto options = Options({"tm"}, [](const std::string&) { return std::string(); }, "Other");
  UR_EXPECT_EQ(size_t(2), options.size());
  UR_EXPECT_TRUE(options[0].name == "tm");
}

UR_TEST(ConnectionRegion_SelectionIsTheStoredValueAndUndisclosedIsEmpty) {
  FakeStore store(kSdkCodes);
  Picker picker(store, Options(kSdkCodes, Named, "Other / Undisclosed"));
  // nothing stored: Other / Undisclosed, the last row
  UR_EXPECT_EQ(picker.options().size() - 1, picker.SelectedIndex());

  UR_EXPECT_TRUE(picker.Select(3) == "ru");
  UR_EXPECT_TRUE(store.value == "ru");
  UR_EXPECT_EQ(size_t(3), picker.SelectedIndex());

  UR_EXPECT_TRUE(picker.Select(picker.options().size() - 1).empty());
  UR_EXPECT_TRUE(store.value.empty());
  UR_EXPECT_TRUE(picker.Selected().code.empty());
}

UR_TEST(ConnectionRegion_AStoredCodeNoLongerOfferedReadsAsUndisclosed) {
  FakeStore store(kSdkCodes);
  store.value = "zz";
  Picker picker(store, Options(kSdkCodes, Named, "Other"));
  UR_EXPECT_TRUE(picker.Selected().code.empty());
}

UR_TEST(ConnectionRegion_LoginControlAndSettingsShareOneStoredValue) {
  FakeStore store(kSdkCodes);
  const auto options = Options(kSdkCodes, Named, "Other / Undisclosed");
  Picker login(store, options);
  Picker settings(store, options);

  login.Select(0);  // cn at sign-in
  UR_EXPECT_TRUE(settings.Selected().code == "cn");

  settings.Select(1);  // ir in Custom DNS
  UR_EXPECT_TRUE(login.Selected().code == "ir");
  UR_EXPECT_TRUE(store.value == "ir");
}

UR_TEST(ConnectionRegion_NetworkFailuresAreCantReach) {
  UR_EXPECT_TRUE(IsCantReachError(
      "Post \"https://api.bringyour.com/auth/login\": dial tcp: lookup api.bringyour.com: no such host"));
  UR_EXPECT_TRUE(IsCantReachError("Could not resolve api.bringyour.com"));
  UR_EXPECT_TRUE(IsCantReachError("dial tcp 1.2.3.4:443: i/o timeout"));
  UR_EXPECT_TRUE(IsCantReachError("dial tcp 1.2.3.4:443: connect: connection refused"));
  UR_EXPECT_TRUE(IsCantReachError("read tcp: connection reset by peer"));
  UR_EXPECT_TRUE(IsCantReachError("net/http: TLS handshake timeout"));
  UR_EXPECT_TRUE(IsCantReachError("context deadline exceeded"));
}

// What the sdk's api calls answer when nothing reached URnetwork (support inbox
// 8050): connect ends a request whose attempts all failed -- a lookup a censor
// drops, a dial to a blackholed address, a timeout -- with its
// HttpRequestExhaustedError, whose text names none of those causes
// (net_http_exhaustion.go). A sign-in from such a network fails with exactly
// this text, and it must offer the connection region like any no-answer error.
UR_TEST(ConnectionRegion_AnExhaustedApiRequestIsCantReach) {
  UR_EXPECT_TRUE(IsCantReachError("http request attempts exhausted"));
  CantReachPrompt prompt;
  prompt.OnFailure("http request attempts exhausted");
  UR_EXPECT_TRUE(prompt.visible());
}

UR_TEST(ConnectionRegion_RefusalsAreNotCantReach) {
  UR_EXPECT_FALSE(IsCantReachError(""));
  UR_EXPECT_FALSE(IsCantReachError("401 Unauthorized: {\"message\":\"timeout awaiting response headers\"}"));
  UR_EXPECT_FALSE(IsCantReachError("400 Bad Request: invalid user auth"));
  UR_EXPECT_FALSE(IsCantReachError("Invalid user or password."));
  UR_EXPECT_FALSE(IsCantReachError("verification required"));
  UR_EXPECT_FALSE(IsCantReachError("superseded by signing out"));
  UR_EXPECT_FALSE(IsCantReachError("no result"));
}

UR_TEST(ConnectionRegion_ThePromptShowsOnlyForCantReachAndNeverChangesTheRegion) {
  FakeStore store(kSdkCodes);
  store.value = "tr";
  CantReachPrompt prompt;
  prompt.OnFailure("Invalid user or password.");
  UR_EXPECT_FALSE(prompt.visible());
  prompt.OnFailure("dial tcp: lookup api.bringyour.com: no such host");
  UR_EXPECT_TRUE(prompt.visible());
  prompt.OnFailure("401 Unauthorized: ");
  UR_EXPECT_TRUE(prompt.visible());  // a later refusal does not hide it
  prompt.Dismiss();
  UR_EXPECT_FALSE(prompt.visible());
  UR_EXPECT_EQ(0, store.sets);
  UR_EXPECT_TRUE(store.value == "tr");
}
