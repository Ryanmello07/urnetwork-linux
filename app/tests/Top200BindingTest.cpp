// The Top 200 fleet binding sheet's pure rules (Top200Binding.hpp): the
// binding check before any sdk call, the payload built by concatenation, the
// member entry, and the app link's decode limits. The vector is sn's
// fleet_sign_test.go fixture (mmm tests/top200-fleet-binding.test.mjs uses the
// same): client seed bytes 0x01..0x20, a fake signer answers its signature.
//
// SPDX-License-Identifier: MPL-2.0
#include "Top200Binding.hpp"
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using namespace urnw::top200;

const std::string kVectorClientId = "019b2e10-5a7c-4d3e-8f01-23456789abcd";
const std::string kVectorClientKey =
    "0x79b5562e8fe654f94078b112e8a98ba7901f853ae695bed7e0e3910bad049664";
const std::string kVectorBinding =
    "{\"chain_id\":945,\"netuid\":25,\"coordinator\":\"0xc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3\","
    "\"fleet_id\":\"0xf0f1f2f3f4f5f6f7f8f9fafbfcfdfeffe0e1e2e3e4e5e6e7e8e9eaebecedeeef\","
    "\"hotkey\":\"0x0a1c77edb48519069bbb154bf088c7f17678839de458e0679370ae989af3f55c\","
    "\"client_id\":\"019b2e10-5a7c-4d3e-8f01-23456789abcd\","
    "\"client_key\":\"0x79b5562e8fe654f94078b112e8a98ba7901f853ae695bed7e0e3910bad049664\","
    "\"generation\":3,\"valid_from_epoch\":100,\"valid_to_epoch\":200,"
    "\"commitment_hash\":\"0x9e1d4fa4a979f45cf003f26d826837250b08b45b8e896f98316cfc60005d3471\"}";
const std::string kVectorSignature =
    "0x8a3e5b990f1a280f0eef9455c81dfbfd251a5ac5a9bc021770cf0a4dee1b4aafab04762d6ca8b587c1e04642d4"
    "6cff87844b89aefcaddf26700fdbc849efbc0a";
const std::string kVectorPayload = "{\"binding\":" + kVectorBinding +
                                   ",\"client_signature\":\"" + kVectorSignature + "\"}";

// The vector's binding with one field's JSON text replaced.
std::string WithField(const std::string& field, const std::string& valueJson) {
  std::string text = kVectorBinding;
  const std::string key = "\"" + field + "\":";
  const size_t start = text.find(key) + key.size();
  size_t end = start;
  if (text[start] == '"') {
    end = text.find('"', start + 1) + 1;
  } else {
    while (end < text.size() && text[end] != ',' && text[end] != '}') ++end;
  }
  return text.substr(0, start) + valueJson + text.substr(end);
}

BindingError Check(const std::string& text) {
  return ValidateBinding(text, kVectorClientId, kVectorClientKey).error;
}

// The fake signer: answers the vector's signature for the vector's binding.
std::string FakeSign(const std::string& binding) {
  return binding == kVectorBinding ? kVectorSignature : std::string();
}

UR_TEST(top200VectorBindingValidatesForItsDevice) {
  const BindingCheck check = ValidateBinding(kVectorBinding, kVectorClientId, kVectorClientKey);
  UR_EXPECT_TRUE(check.error == BindingError::None);
  UR_EXPECT_TRUE(check.binding.has_value());
  UR_EXPECT_TRUE(check.trimmed == kVectorBinding);
  if (!check.binding) return;
  UR_EXPECT_EQ(945, check.binding->chainId);
  UR_EXPECT_EQ(25, check.binding->netuid);
  UR_EXPECT_EQ(3, check.binding->generation);
  UR_EXPECT_EQ(100, check.binding->validFromEpoch);
  UR_EXPECT_EQ(200, check.binding->validToEpoch);
  UR_EXPECT_TRUE(check.binding->fleetId ==
                 "0xf0f1f2f3f4f5f6f7f8f9fafbfcfdfeffe0e1e2e3e4e5e6e7e8e9eaebecedeeef");
  UR_EXPECT_TRUE(check.binding->hotkey ==
                 "0x0a1c77edb48519069bbb154bf088c7f17678839de458e0679370ae989af3f55c");
}

UR_TEST(top200VectorPayloadIsByteIdentical) {
  const BindingCheck check =
      ValidateBinding("\n  " + kVectorBinding + " \r\n", kVectorClientId, kVectorClientKey);
  UR_EXPECT_TRUE(check.error == BindingError::None);
  const SignOutcome outcome = FinishSign(check.trimmed, FakeSign(check.trimmed), "");
  UR_EXPECT_TRUE(outcome.ok);
  UR_EXPECT_TRUE_MSG(outcome.payload, outcome.payload == kVectorPayload);
  UR_EXPECT_TRUE(BuildPayload(kVectorBinding, kVectorSignature) == kVectorPayload);
}

UR_TEST(top200PayloadKeepsThePastedTextNotAReserialization) {
  // whitespace inside the object is the operator's text and stays as pasted
  const std::string spaced = WithField("netuid", " 25 ");
  const BindingCheck check = ValidateBinding(spaced, kVectorClientId, kVectorClientKey);
  UR_EXPECT_TRUE(check.error == BindingError::None);
  UR_EXPECT_TRUE(FinishSign(check.trimmed, "0xab", "").payload ==
                 "{\"binding\":" + spaced + ",\"client_signature\":\"0xab\"}");
}

UR_TEST(top200SignFailuresAreNeverAPayload) {
  const SignOutcome empty = FinishSign(kVectorBinding, "", "");
  UR_EXPECT_FALSE(empty.ok);
  UR_EXPECT_TRUE(empty.payload.empty());
  UR_EXPECT_TRUE(empty.detail == kEmptySignatureDetail);
  const SignOutcome thrown = FinishSign(kVectorBinding, "", "device service not connected");
  UR_EXPECT_FALSE(thrown.ok);
  UR_EXPECT_TRUE(thrown.detail == "device service not connected");
}

UR_TEST(top200FullPayloadIsNotABinding) {
  UR_EXPECT_TRUE(Check(kVectorPayload) == BindingError::Invalid);
}

UR_TEST(top200NonBindingsAreInvalid) {
  const std::string cases[] = {
      "",
      "   ",
      "not json",
      "[]",
      "945",
      "{}",
      kVectorBinding.substr(0, kVectorBinding.size() - 1),  // truncated
      kVectorBinding + "x",
  };
  for (const auto& text : cases) {
    UR_EXPECT_TRUE_MSG(text, Check(text) == BindingError::Invalid);
  }
}

UR_TEST(top200TextBeforeTheObjectIsInvalid) {
  // nlohmann skips a leading UTF-8 BOM but the sdk's Go decoder refuses it:
  // the review must not show a binding the sign then refuses
  UR_EXPECT_TRUE(Check("\xEF\xBB\xBF" + kVectorBinding) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check("  \xEF\xBB\xBF" + kVectorBinding) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check("\xEF\xBB\xBF  " + kVectorBinding) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check("x" + kVectorBinding) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check("\xC2\xA0" + kVectorBinding) == BindingError::Invalid);  // NBSP
  // JSON whitespace around the object is still trimmed
  const BindingCheck padded =
      ValidateBinding(" \t\r\n" + kVectorBinding + "\n ", kVectorClientId, kVectorClientKey);
  UR_EXPECT_TRUE(padded.error == BindingError::None);
  UR_EXPECT_TRUE(padded.trimmed == kVectorBinding);
}

UR_TEST(top200PastedTextLengthLimit) {
  UR_EXPECT_EQ(4096, kMaxBindingTextChars);
  // a binding padded inside the object to exactly the limit still validates;
  // one character more is refused before parsing
  const std::string inner = kVectorBinding.substr(1);
  const std::string atLimit =
      "{" + std::string(kMaxBindingTextChars - kVectorBinding.size(), ' ') + inner;
  UR_EXPECT_EQ(kMaxBindingTextChars, atLimit.size());
  UR_EXPECT_TRUE(Check(atLimit) == BindingError::None);
  const std::string over = "{" + std::string(kMaxBindingTextChars + 1 - kVectorBinding.size(), ' ') +
                           inner;
  UR_EXPECT_TRUE(Check(over) == BindingError::Invalid);
  // the limit is on the trimmed text: surrounding whitespace does not count
  UR_EXPECT_TRUE(Check(std::string(8192, ' ') + atLimit + std::string(8192, '\n')) ==
                 BindingError::None);
  // and counts characters, not bytes
  UR_EXPECT_EQ(3, Utf8CharCount("a\xC3\xA9\xE2\x80\xA6"));
  UR_EXPECT_TRUE(Check(std::string(1 << 20, '{')) == BindingError::Invalid);
}

UR_TEST(top200SignButtonWaitsForTheIdentity) {
  // reviewed binding, identity read still out: Sign stays disabled
  UR_EXPECT_FALSE(SignButton(true, false, false, false).sensitive);
  UR_EXPECT_FALSE(SignButton(true, false, true, false).sensitive);
  // identity answered without a key: still disabled
  UR_EXPECT_FALSE(SignButton(true, true, false, false).sensitive);
  // nothing reviewed: disabled
  UR_EXPECT_FALSE(SignButton(false, true, true, false).sensitive);
  // ready
  const SignButtonState ready = SignButton(true, true, true, false);
  UR_EXPECT_TRUE(ready.sensitive);
  UR_EXPECT_FALSE(ready.busy);
  // a sign out (or waiting for the worker) shows the spinner, never a live button
  const SignButtonState signing = SignButton(true, true, true, true);
  UR_EXPECT_FALSE(signing.sensitive);
  UR_EXPECT_TRUE(signing.busy);
}

UR_TEST(top200MissingExtraAndDuplicateFieldsAreInvalid) {
  // a missing field
  const std::string missing =
      "{\"chain_id\":945" + kVectorBinding.substr(kVectorBinding.find(",\"coordinator\""));
  UR_EXPECT_TRUE(Check(missing) == BindingError::Invalid);
  // a twelfth field
  UR_EXPECT_TRUE(Check(kVectorBinding.substr(0, kVectorBinding.size() - 1) + ",\"extra\":1}") ==
                 BindingError::Invalid);
  // the sdk's decoder would read "HOTKEY" as hotkey: a second spelling is refused
  UR_EXPECT_TRUE(Check(kVectorBinding.substr(0, kVectorBinding.size() - 1) +
                       ",\"HOTKEY\":\"0x" + std::string(64, '1') + "\"}") ==
                 BindingError::Invalid);
  // a duplicate key: the review must show what is signed
  const std::string duplicate = "{\"hotkey\":\"0x" + std::string(64, '1') + "\"," +
                                kVectorBinding.substr(1);
  UR_EXPECT_TRUE(Check(duplicate) == BindingError::Invalid);
}

UR_TEST(top200NumberRules) {
  UR_EXPECT_TRUE(Check(WithField("chain_id", "0")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("chain_id", "-945")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("chain_id", "945.0")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("chain_id", "\"945\"")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("netuid", "65536")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("netuid", "65535")) == BindingError::None);
  UR_EXPECT_TRUE(Check(WithField("netuid", "0")) == BindingError::None);
  UR_EXPECT_TRUE(Check(WithField("generation", "0")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("valid_from_epoch", "0")) == BindingError::None);
  UR_EXPECT_TRUE(Check(WithField("valid_from_epoch", "200")) == BindingError::None);
  UR_EXPECT_TRUE(Check(WithField("valid_from_epoch", "201")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("valid_to_epoch", "1e3")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("valid_to_epoch", "99999999999999999999999")) ==
                 BindingError::Invalid);
}

UR_TEST(top200HexRules) {
  UR_EXPECT_TRUE(Check(WithField("coordinator", "\"0xc0c1c2\"")) == BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("fleet_id", "\"f0f1f2f3f4f5f6f7f8f9fafbfcfdfeffe0e1e2e3e4e5e6e7e8e9eaebecedeeef\"")) ==
                 BindingError::Invalid);  // no 0x
  UR_EXPECT_TRUE(Check(WithField("hotkey", "\"0x" + std::string(63, 'a') + "g\"")) ==
                 BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("commitment_hash", "\"0x" + std::string(66, 'a') + "\"")) ==
                 BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("hotkey", "12")) == BindingError::Invalid);
  // hex case does not matter
  UR_EXPECT_TRUE(Check(WithField("fleet_id", "\"0xF0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFFE0E1E2E3E4E5E6E7E8E9EAEBECEDEEEF\"")) ==
                 BindingError::None);
  UR_EXPECT_TRUE(Check(WithField("client_key", "\"0x79B5562E8FE654F94078B112E8A98BA7901F853AE695BED7E0E3910BAD049664\"")) ==
                 BindingError::None);
}

UR_TEST(top200ClientIdForms) {
  // 0x + 32 hex names the same 16 bytes as the uuid
  UR_EXPECT_TRUE(Check(WithField("client_id", "\"0x019b2e105a7c4d3e8f0123456789ABCD\"")) ==
                 BindingError::None);
  UR_EXPECT_TRUE(Check(WithField("client_id", "\"019b2e105a7c4d3e8f0123456789abcd\"")) ==
                 BindingError::Invalid);
  UR_EXPECT_TRUE(Check(WithField("client_id", "\"019b2e10-5a7c-4d3e-8f01-23456789abc\"")) ==
                 BindingError::Invalid);
  UR_EXPECT_TRUE(ClientIdHex(kVectorClientId).value_or("") == "019b2e105a7c4d3e8f0123456789abcd");
  UR_EXPECT_FALSE(ClientIdHex("019b2e10_5a7c-4d3e-8f01-23456789abcd").has_value());
}

UR_TEST(top200NoKeyAfterTheBindingIsWellFormed) {
  const BindingCheck check = ValidateBinding(kVectorBinding, kVectorClientId, "");
  UR_EXPECT_TRUE(check.error == BindingError::NoKey);
  // a non-binding is invalid even with no key
  UR_EXPECT_TRUE(ValidateBinding("{}", kVectorClientId, "").error == BindingError::Invalid);
}

UR_TEST(top200OtherDeviceIsRefused) {
  // another client key: the sdk would sign it (it checks only client_id)
  UR_EXPECT_TRUE(ValidateBinding(kVectorBinding, kVectorClientId, "0x" + std::string(64, 'a'))
                     .error == BindingError::OtherDevice);
  // another client id
  UR_EXPECT_TRUE(ValidateBinding(kVectorBinding, "019b2e10-5a7c-4d3e-8f01-23456789abce",
                                 kVectorClientKey)
                     .error == BindingError::OtherDevice);
  // no client id for this device
  UR_EXPECT_TRUE(ValidateBinding(kVectorBinding, "", kVectorClientKey).error ==
                 BindingError::OtherDevice);
  // the device's id in 0x form still matches
  UR_EXPECT_TRUE(ValidateBinding(kVectorBinding, "0x019b2e105a7c4d3e8f0123456789abcd",
                                 kVectorClientKey)
                     .error == BindingError::None);
}

UR_TEST(top200MemberEntry) {
  UR_EXPECT_TRUE(MemberEntry(kVectorClientId, kVectorClientKey) ==
                 "{\"client_id\":\"0x019b2e105a7c4d3e8f0123456789abcd\",\"client_key\":\"" +
                     kVectorClientKey + "\"}");
  UR_EXPECT_TRUE(MemberEntry("019B2E10-5A7C-4D3E-8F01-23456789ABCD", kVectorClientKey) ==
                 MemberEntry(kVectorClientId, kVectorClientKey));
  UR_EXPECT_TRUE(MemberEntry(kVectorClientId, "").empty());
  UR_EXPECT_TRUE(MemberEntry("", kVectorClientKey).empty());
}

UR_TEST(top200ShortKey) {
  UR_EXPECT_TRUE(ShortKey(kVectorClientKey) == "0x79b5562e…049664");
  UR_EXPECT_TRUE(ShortKey("0x1234") == "0x1234");
}

std::string Base64Url(const std::string& bytes) {
  static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const uint32_t n = (uint32_t(uint8_t(bytes[i])) << 16) | (uint32_t(uint8_t(bytes[i + 1])) << 8) |
                       uint32_t(uint8_t(bytes[i + 2]));
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += alphabet[(n >> 6) & 63];
    out += alphabet[n & 63];
  }
  if (bytes.size() - i == 1) {
    const uint32_t n = uint32_t(uint8_t(bytes[i])) << 16;
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
  } else if (bytes.size() - i == 2) {
    const uint32_t n = (uint32_t(uint8_t(bytes[i])) << 16) | (uint32_t(uint8_t(bytes[i + 1])) << 8);
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += alphabet[(n >> 6) & 63];
  }
  return out;
}

UR_TEST(top200LinkPrefillsTheBinding) {
  const SignLink link = ParseSignLink("urnetwork://top200/sign?binding=" + Base64Url(kVectorBinding));
  UR_EXPECT_TRUE(link.matched);
  UR_EXPECT_TRUE(link.binding.value_or("") == kVectorBinding);
  // the decoded text runs the same check as a paste
  UR_EXPECT_TRUE(Check(*link.binding) == BindingError::None);
  const SignLink other = ParseSignLink("urnetwork://top200/sign?x=1&binding=" + Base64Url("{}"));
  UR_EXPECT_TRUE(other.binding.value_or("") == "{}");
}

UR_TEST(top200LinkOtherRoutesAreNotMatched) {
  UR_EXPECT_FALSE(ParseSignLink("urnetwork://onboarding/connect").matched);
  UR_EXPECT_FALSE(ParseSignLink("urnetwork://top200/signx?binding=e30").matched);
  UR_EXPECT_FALSE(ParseSignLink("ur://top200/sign?binding=e30").matched);
  UR_EXPECT_TRUE(ParseSignLink("urnetwork://top200/sign/?binding=e30").binding.value_or("") == "{}");
}

UR_TEST(top200LinkRejectsBadBindings) {
  const std::string bad[] = {
      "urnetwork://top200/sign",
      "urnetwork://top200/sign?",
      "urnetwork://top200/sign?binding=",
      "urnetwork://top200/sign?other=e30",
      "urnetwork://top200/sign?binding=e30=",       // padding
      "urnetwork://top200/sign?binding=e3+v",       // base64, not base64url
      "urnetwork://top200/sign?binding=e%33%30",    // percent escapes
      "urnetwork://top200/sign?binding=e",          // impossible length
      "urnetwork://top200/sign?binding=e31",        // set trailing bits
      "urnetwork://top200/sign?binding=e30&binding=e30",
      "urnetwork://top200/sign?binding=" + Base64Url("\xff\xfe"),          // not UTF-8
      "urnetwork://top200/sign?binding=" + Base64Url("\xc0\xaf"),          // overlong
      "urnetwork://top200/sign?binding=" + Base64Url("\xed\xa0\x80"),      // surrogate
  };
  for (const auto& url : bad) {
    const SignLink link = ParseSignLink(url);
    UR_EXPECT_TRUE_MSG(url, link.matched);
    UR_EXPECT_TRUE_MSG(url, !link.binding.has_value());
  }
}

UR_TEST(top200LinkLengthLimit) {
  // 3072 bytes encode to exactly 4096 characters: accepted; one more group is not
  const std::string atLimit = Base64Url(std::string(3072, 'a'));
  UR_EXPECT_EQ(4096, atLimit.size());
  UR_EXPECT_TRUE(ParseSignLink("urnetwork://top200/sign?binding=" + atLimit).binding.has_value());
  const std::string over = Base64Url(std::string(3073, 'a'));
  UR_EXPECT_TRUE(kMaxLinkBindingChars < over.size());
  UR_EXPECT_FALSE(ParseSignLink("urnetwork://top200/sign?binding=" + over).binding.has_value());
}

UR_TEST(top200Utf8) {
  UR_EXPECT_TRUE(IsValidUtf8("plain"));
  UR_EXPECT_TRUE(IsValidUtf8("… \xf0\x9f\x98\x80"));
  UR_EXPECT_FALSE(IsValidUtf8("\xe2\x80"));          // truncated
  UR_EXPECT_FALSE(IsValidUtf8("\xf4\x90\x80\x80"));  // past U+10FFFF
}

// The sheet, its button, its link and its host calls need GTK and the SDK, so
// their wiring is read from the sources: a rule with no caller protects
// nothing.
std::string ReadTop200Source(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool Top200SourceHas(const std::string& source, const std::string& needle) {
  return source.find(needle) != std::string::npos;
}

UR_TEST(top200SignIsWiredEndToEnd) {
  const std::string earnings = ReadTop200Source("EarningsPage.cpp");
  const size_t rebuild = earnings.find("void EarningsPage::RebuildTop200()");
  const size_t rebuildEnd = earnings.find("\nvoid EarningsPage::", rebuild + 1);
  UR_EXPECT_TRUE(rebuild != std::string::npos && rebuildEnd != std::string::npos);
  if (rebuild == std::string::npos || rebuildEnd == std::string::npos) return;
  const std::string block = earnings.substr(rebuild, rebuildEnd - rebuild);
  // the button in both the bound card and the eligible tile, once binding is
  // open (ProtocolBoardPresentation.hpp TileFor's showSign; LEADERTOP: it
  // stays hidden while binding is closed)
  UR_EXPECT_TRUE(Top200SourceHas(block, "OpenTop200SignSheet(std::nullopt, false)"));
  size_t signs = 0;
  const std::string sign = "if (tile.showSign) card->append(*signButton());";
  for (size_t at = block.find(sign); at != std::string::npos; at = block.find(sign, at + 1)) {
    ++signs;
  }
  UR_EXPECT_EQ(2u, signs);

  const std::string host = ReadTop200Source("SdkHost.cpp");
  UR_EXPECT_TRUE(Top200SourceHas(host, "url.rfind(\"urnetwork://top200/\", 0) == 0"));
  UR_EXPECT_TRUE(Top200SourceHas(host, "device_->getSnClientKey()"));
  UR_EXPECT_TRUE(Top200SourceHas(host, "device_->signSnFleetBinding(bindingJson)"));

  const std::string window = ReadTop200Source("MainWindow.cpp");
  UR_EXPECT_TRUE(Top200SourceHas(window, "top200::ParseSignLink(url)"));
  UR_EXPECT_TRUE(Top200SourceHas(window, "OpenTop200SignSheet(link.binding, /*fromLink=*/true)"));

  // the sheet checks before it signs and builds the payload from the reply
  const std::string sheet = ReadTop200Source("Top200SignSheet.cpp");
  UR_EXPECT_TRUE(Top200SourceHas(sheet, "top200::ValidateBinding(text, clientId_, clientKey_)"));
  UR_EXPECT_TRUE(Top200SourceHas(sheet, "host_.RequestSignSnFleetBinding("));
  UR_EXPECT_TRUE(Top200SourceHas(sheet, "top200::FinishSign(trimmed, signature, error)"));
  UR_EXPECT_TRUE(Top200SourceHas(sheet, "top200::MemberEntry(clientId_, clientKey_)"));
  // the review shows the hotkey in full, never shortened
  UR_EXPECT_TRUE(Top200SourceHas(sheet, "hotkeyValue_->set_text(binding.hotkey);"));
  UR_EXPECT_FALSE(Top200SourceHas(sheet, "ShortKey(binding.hotkey)"));
  // the button follows SignButton, and a busy worker retries the tapped sign
  UR_EXPECT_TRUE(Top200SourceHas(
      sheet, "top200::SignButton(reviewed, identityLoaded_, !clientKey_.empty(), signing_)"));
  const size_t start = sheet.find("void Top200SignSheet::StartSign(");
  const size_t startEnd = sheet.find("\nvoid Top200SignSheet::", start + 1);
  UR_EXPECT_TRUE(start != std::string::npos && startEnd != std::string::npos);
  if (start != std::string::npos && startEnd != std::string::npos) {
    const std::string body = sheet.substr(start, startEnd - start);
    UR_EXPECT_TRUE(Top200SourceHas(body, "signRetry_ = Glib::signal_timeout().connect("));
    UR_EXPECT_TRUE(Top200SourceHas(body, "StartSign();"));
  }
  // the link only pre-fills: nothing in Prefill signs
  const size_t prefill = sheet.find("void Top200SignSheet::Prefill(");
  const size_t prefillEnd = sheet.find("\nvoid Top200SignSheet::", prefill + 1);
  UR_EXPECT_TRUE(prefill != std::string::npos && prefillEnd != std::string::npos);
  if (prefill != std::string::npos && prefillEnd != std::string::npos) {
    const std::string body = sheet.substr(prefill, prefillEnd - prefill);
    UR_EXPECT_FALSE(Top200SourceHas(body, "OnSign"));
    UR_EXPECT_FALSE(Top200SourceHas(body, "RequestSignSnFleetBinding"));
  }
}

}  // namespace
