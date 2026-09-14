// The provider extender row's reading of ExtenderProvideStatus (EXTENDER.md
// N3, N7) and the earnings page's statistics sections (O8). The texts are
// composed from the English sources, which ExtenderProvide_KeysAreTheCatalogs
// pins against po/en.po.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ExtenderProvidePresentation.hpp"

using urnw::extender::CountRateLabel;
using urnw::extender::ProvideDot;
using urnw::extender::ProvideRow;
using urnw::extender::ProvideRowFor;
using urnw::extender::ProvideRowGuess;
using urnw::extender::StatsSections;
using urnw::extender::StatsSectionsFor;

#define UR_EXPECT_TEXT(expected, actual)                                                    \
  do {                                                                                      \
    const std::string ur_e_ = (expected);                                                   \
    const std::string ur_a_ = (actual);                                                     \
    if (ur_e_ != ur_a_)                                                                     \
      UR_FAIL(std::string(#actual) + ": expected \"" + ur_e_ + "\", got \"" + ur_a_ + "\""); \
  } while (0)

namespace {

// The catalog with no translation loaded: every key reads its English source.
std::string English(const char* /*key*/, const char* english) { return english; }

// The store's single {} substitution (I18n.hpp's Format with one argument).
std::string FormatOne(const std::string& pattern, const std::string& argument) {
  const size_t at = pattern.find("{}");
  if (at == std::string::npos) return pattern;
  return pattern.substr(0, at) + argument + pattern.substr(at + 2);
}

std::string TextOf(const ProvideRow& row) {
  return urnw::extender::StateTextFor(row, &English, &FormatOne);
}

// A supported status as the SDK sends it, with the setting read beside it.
ProvideRow Row(const std::string& state, const std::string& errorCase = std::string(),
               const std::string& reason = std::string(), bool activatedV4 = false,
               bool activatedV6 = false, bool refused = false, bool provideExtender = true) {
  return ProvideRowFor(true, true, state, errorCase, reason, activatedV4, activatedV6, refused,
                       provideExtender);
}

}  // namespace

// No status (no device) is not "off": the row is hidden, not drawn grey.
UR_TEST(ExtenderProvide_NoStatusIsHidden) {
  const ProvideRow row = ProvideRowFor(false, true, "active", "", "", true, true, false, true);
  UR_EXPECT_FALSE(row.visible);
  UR_EXPECT_TRUE(TextOf(row).empty());
}

// An unsupported role is hidden, never disabled, whatever else the status
// says -- and a hidden row reads nothing else, so every hidden push compares
// equal and the widget draws nothing for it.
UR_TEST(ExtenderProvide_UnsupportedIsHiddenInEveryState) {
  for (const char* state : {"off", "not_providing", "setting_up", "active", "error", ""}) {
    const ProvideRow row = ProvideRowFor(true, false, state, "listen", "tcp: bind: permission denied",
                                         true, true, true, true);
    UR_EXPECT_TRUE_MSG(state, !row.visible);
    UR_EXPECT_TRUE_MSG(state, TextOf(row).empty());
    UR_EXPECT_TRUE_MSG(state, row == ProvideRow());
  }
}

UR_TEST(ExtenderProvide_OffIsGreyOff) {
  const ProvideRow row = Row("off");
  UR_EXPECT_TRUE(row.visible);
  UR_EXPECT_TRUE(row.dot == ProvideDot::Grey);
  UR_EXPECT_TEXT("off", row.textKey);
  UR_EXPECT_TEXT("Off", TextOf(row));
  UR_EXPECT_FALSE(row.errorText);
}

UR_TEST(ExtenderProvide_NotProvidingIsGrey) {
  const ProvideRow row = Row("not_providing");
  UR_EXPECT_TRUE(row.dot == ProvideDot::Grey);
  UR_EXPECT_TEXT("extender_not_providing", row.textKey);
  UR_EXPECT_TEXT("Not providing", TextOf(row));
  UR_EXPECT_FALSE(row.errorText);
}

UR_TEST(ExtenderProvide_SettingUpIsYellow) {
  const ProvideRow row = Row("setting_up");
  UR_EXPECT_TRUE(row.dot == ProvideDot::Yellow);
  UR_EXPECT_TEXT("extender_setting_up", row.textKey);
  UR_EXPECT_TEXT("Setting up", TextOf(row));
  UR_EXPECT_FALSE(row.errorText);
}

// The three family texts, from the two activation flags.
UR_TEST(ExtenderProvide_ActiveNamesTheFamilies) {
  const ProvideRow both = Row("active", "", "", true, true);
  UR_EXPECT_TRUE(both.dot == ProvideDot::Green);
  UR_EXPECT_FALSE(both.errorText);
  UR_EXPECT_TEXT("extender_active", both.textKey);
  UR_EXPECT_TEXT("ipv4_and_ipv6", both.familiesKey);
  UR_EXPECT_TEXT("Active · IPv4 and IPv6", TextOf(both));

  const ProvideRow v4 = Row("active", "", "", true, false);
  UR_EXPECT_TEXT("ipv4", v4.familiesKey);
  UR_EXPECT_TEXT("Active · IPv4", TextOf(v4));

  const ProvideRow v6 = Row("active", "", "", false, true);
  UR_EXPECT_TEXT("ipv6", v6.familiesKey);
  UR_EXPECT_TEXT("Active · IPv6", TextOf(v6));
}

// Active with the other family's last attempt refused: the refusal follows on
// the same line, the dot stays green and the line stays muted.
UR_TEST(ExtenderProvide_ActiveCarriesTheOtherFamilysRefusal) {
  const ProvideRow row =
      Row("active", "", "the operator refused the activation", true, false, true);
  UR_EXPECT_TRUE(row.dot == ProvideDot::Green);
  UR_EXPECT_FALSE(row.errorText);
  UR_EXPECT_TEXT("extender_activation_refused", row.detailKey);
  UR_EXPECT_TEXT("Active · IPv4 · Activation refused: the operator refused the activation",
                 TextOf(row));
}

UR_TEST(ExtenderProvide_ActiveCarriesTheOtherFamilysFailure) {
  const ProvideRow row = Row("active", "",
                             "dial tcp6 [2001:db8::1]:443: connect: network is unreachable", false,
                             true, false);
  UR_EXPECT_TRUE(row.dot == ProvideDot::Green);
  UR_EXPECT_FALSE(row.errorText);
  UR_EXPECT_TEXT("extender_activation_failed", row.detailKey);
  UR_EXPECT_TEXT(
      "Active · IPv6 · Activation failed: dial tcp6 [2001:db8::1]:443: connect: network is unreachable",
      TextOf(row));
}

// An active reading with no Reason carries no second clause.
UR_TEST(ExtenderProvide_ActiveWithoutAReasonHasNoDetail) {
  const ProvideRow row = Row("active", "", "", true, true, true);
  UR_EXPECT_TEXT("", row.detailKey);
  UR_EXPECT_TEXT("Active · IPv4 and IPv6", TextOf(row));
}

// Every error case is red, in the error color, and labeled by ErrorCase alone:
// neither the refused flag nor the activation flags move the label.
UR_TEST(ExtenderProvide_EachErrorCaseByErrorCaseAlone) {
  struct Case {
    const char* errorCase;
    const char* reason;
    const char* key;
    const char* text;
  };
  const std::vector<Case> cases = {
      {"revoked", "", "extender_revoked", "Revoked by the operator"},
      {"start", "no extender directory in this network space", "extender_start_failed",
       "Could not start: no extender directory in this network space"},
      {"listen", "tcp: bind: permission denied; quic: bind: permission denied",
       "extender_listen_failed",
       "Could not listen: tcp: bind: permission denied; quic: bind: permission denied"},
      {"activation_refused", "not reachable from the operator", "extender_activation_refused",
       "Activation refused: not reachable from the operator"},
      {"activation_failed", "context deadline exceeded", "extender_activation_failed",
       "Activation failed: context deadline exceeded"},
  };
  for (const auto& c : cases) {
    for (bool refused : {false, true}) {
      for (bool families : {false, true}) {
        const ProvideRow row = Row("error", c.errorCase, c.reason, families, families, refused);
        UR_EXPECT_TRUE_MSG(c.errorCase, row.visible);
        UR_EXPECT_TRUE_MSG(c.errorCase, row.dot == ProvideDot::Red);
        UR_EXPECT_TRUE_MSG(c.errorCase, row.errorText);
        UR_EXPECT_TEXT(c.key, row.textKey);
        UR_EXPECT_TEXT(c.text, TextOf(row));
        UR_EXPECT_TEXT("", row.familiesKey);
        UR_EXPECT_TEXT("", row.detailKey);
      }
    }
  }
}

// A refusal and a request error with the same text are two readings.
UR_TEST(ExtenderProvide_RefusalAndFailureAreDistinct) {
  const ProvideRow refused = Row("error", "activation_refused", "probe failed");
  const ProvideRow failed = Row("error", "activation_failed", "probe failed");
  UR_EXPECT_TRUE(refused != failed);
  UR_EXPECT_TEXT("Activation refused: probe failed", TextOf(refused));
  UR_EXPECT_TEXT("Activation failed: probe failed", TextOf(failed));
}

// An error case this build does not know renders the Reason bare, in the error
// color; so does an error that names no case.
UR_TEST(ExtenderProvide_UnknownErrorCaseRendersTheReasonBare) {
  const ProvideRow unknown = Row("error", "quota", "the operator is over its quota");
  UR_EXPECT_TRUE(unknown.visible);
  UR_EXPECT_TRUE(unknown.dot == ProvideDot::Red);
  UR_EXPECT_TRUE(unknown.errorText);
  UR_EXPECT_TEXT("", unknown.textKey);
  UR_EXPECT_TEXT("the operator is over its quota", TextOf(unknown));

  const ProvideRow noCase = Row("error", "", "something the device said");
  UR_EXPECT_TRUE(noCase.dot == ProvideDot::Red);
  UR_EXPECT_TRUE(noCase.errorText);
  UR_EXPECT_TEXT("something the device said", TextOf(noCase));
}

// A state this build does not know claims no color and names no case: grey,
// the Reason bare, muted.
UR_TEST(ExtenderProvide_UnknownStateIsGreyWithTheReasonBare) {
  const ProvideRow row = Row("paused", "", "held by the operator");
  UR_EXPECT_TRUE(row.visible);
  UR_EXPECT_TRUE(row.dot == ProvideDot::Grey);
  UR_EXPECT_FALSE(row.errorText);
  UR_EXPECT_TEXT("", row.textKey);
  UR_EXPECT_TEXT("held by the operator", TextOf(row));
}

// The toggle shows the setting, not the state: a write racing the status reads
// on over an "off" status until the status catches up.
UR_TEST(ExtenderProvide_ToggleIsTheSettingNotTheState) {
  UR_EXPECT_TRUE(Row("off", "", "", false, false, false, true).on);
  UR_EXPECT_FALSE(Row("active", "", "", true, true, false, false).on);
  UR_EXPECT_FALSE(Row("setting_up", "", "", false, false, false, false).on);
}

// The local repaint after a toggle, before the listener answers.
UR_TEST(ExtenderProvide_ToggleGuess) {
  const ProvideRow onProviding = ProvideRowGuess(true, true);
  UR_EXPECT_TRUE(onProviding.visible);
  UR_EXPECT_TRUE(onProviding.dot == ProvideDot::Yellow);
  UR_EXPECT_TEXT("Setting up", TextOf(onProviding));
  UR_EXPECT_TRUE(onProviding.on);

  const ProvideRow onIdle = ProvideRowGuess(true, false);
  UR_EXPECT_TRUE(onIdle.dot == ProvideDot::Grey);
  UR_EXPECT_TEXT("Not providing", TextOf(onIdle));
  UR_EXPECT_TRUE(onIdle.on);

  for (bool providing : {false, true}) {
    const ProvideRow off = ProvideRowGuess(false, providing);
    UR_EXPECT_TRUE(off.dot == ProvideDot::Grey);
    UR_EXPECT_TEXT("Off", TextOf(off));
    UR_EXPECT_FALSE(off.on);
    // the guess is the reading the SDK's own "off" produces, so a status that
    // confirms it draws nothing new
    UR_EXPECT_TRUE(off == Row("off", "", "", false, false, false, false));
  }
}

// By value: an unchanged push is dropped, a changed Reason or setting is not.
UR_TEST(ExtenderProvide_EqualReadingsCompareEqual) {
  UR_EXPECT_TRUE(Row("error", "listen", "a") == Row("error", "listen", "a"));
  UR_EXPECT_TRUE(Row("error", "listen", "a") != Row("error", "listen", "b"));
  UR_EXPECT_TRUE(Row("active", "", "", true, true, false, true) !=
                 Row("active", "", "", true, true, false, false));
  UR_EXPECT_TRUE(Row("active", "", "x", true, false, true) !=
                 Row("active", "", "x", true, false, false));
}

// Every key and English source the row can emit is the catalog's, byte for
// byte (I18n.hpp: the English text is the msgid the catalog is keyed on).
UR_TEST(ExtenderProvide_KeysAreTheCatalogs) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../po/en.po", std::ios::binary);
  UR_EXPECT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string catalog = buffer.str();

  const std::vector<ProvideRow> rows = {
      Row("off"),
      Row("not_providing"),
      Row("setting_up"),
      Row("active", "", "", true, true),
      Row("active", "", "", true, false),
      Row("active", "", "", false, true),
      Row("active", "", "x", true, false, true),
      Row("active", "", "x", true, false, false),
      Row("error", "revoked"),
      Row("error", "start", "x"),
      Row("error", "listen", "x"),
      Row("error", "activation_refused", "x"),
      Row("error", "activation_failed", "x"),
  };
  std::set<std::string> keys;
  auto expectInCatalog = [&](const char* key, const char* english) {
    if (key[0] == '\0') return;
    keys.insert(key);
    const std::string entry =
        std::string("msgctxt \"") + key + "\"\nmsgid \"" + english + "\"\n";
    UR_EXPECT_TRUE_MSG(entry, catalog.find(entry) != std::string::npos);
  };
  for (const auto& row : rows) {
    expectInCatalog(row.textKey, row.textEnglish);
    expectInCatalog(row.familiesKey, row.familiesEnglish);
    expectInCatalog(row.detailKey, row.detailEnglish);
  }
  // off, the three plain states, active and its three families, and the five
  // error cases, two of which also serve as the active line's detail
  UR_EXPECT_EQ(12, static_cast<int>(keys.size()));
}

// The O8 rule over every combination of its three inputs.
UR_TEST(ExtenderStats_SectionsForEveryCombination) {
  for (bool providing : {false, true}) {
    for (bool hasStats : {false, true}) {
      for (bool running : {false, true}) {
        const StatsSections sections = StatsSectionsFor(providing, hasStats, running);
        const std::string label = std::string("providing=") + (providing ? "1" : "0") +
                                  " stats=" + (hasStats ? "1" : "0") +
                                  " running=" + (running ? "1" : "0");
        UR_EXPECT_TRUE_MSG(label, sections.providerVisible == (providing && hasStats));
        UR_EXPECT_TRUE_MSG(label, sections.extenderVisible == (providing && hasStats && running));
        UR_EXPECT_TRUE_MSG(label, sections.disabledMeta == !(providing && hasStats));
      }
    }
  }
  // the extender group never shows without the provider one, running or not
  UR_EXPECT_FALSE(StatsSectionsFor(false, true, true).extenderVisible);
  UR_EXPECT_FALSE(StatsSectionsFor(true, false, true).extenderVisible);
  UR_EXPECT_TRUE(StatsSectionsFor(true, true, true) == StatsSectionsFor(true, true, true));
  UR_EXPECT_TRUE(StatsSectionsFor(true, true, true) != StatsSectionsFor(true, true, false));
}

// The extender chart's count label.
UR_TEST(ExtenderStats_CountRateLabel) {
  UR_EXPECT_TEXT("340 reads/s", CountRateLabel("340", "reads/s"));
  UR_EXPECT_TEXT("1.2k reads/s", CountRateLabel("1.2k", "reads/s"));
  UR_EXPECT_TEXT("0 reads/s", CountRateLabel("0", "reads/s"));
}
