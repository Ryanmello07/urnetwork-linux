// What the upgrade sheet says when its checkout page fails
// (CheckoutBridgeError.hpp): the page's codes this app knows read in its own
// words, any other failure in the page's text, and the sheet uses it for the
// SDK's hand-back (UpgradeSheet.cpp, read as text: it needs GTK, WebKit and
// the SDK).
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CheckoutBridgeError.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadFile(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string Key(const urnw::CheckoutFailureText& text) { return text.key ? text.key : ""; }

}  // namespace

// The page's codes (urnet::CheckoutBridgeError*) this app says in its own
// words; any other failure reads in the page's text, and one without a text
// says something went wrong.
UR_TEST(CheckoutBridgeError_KnownCodesReadInTheAppsWords) {
  const std::string english = "The page's English text.";
  struct Case {
    const char* code;
    const char* key;
  };
  for (const Case& c : {
           Case{"checkout_unavailable", "checkout_error_unavailable"},
           Case{"stripe_unavailable", "checkout_error_payment_form_unavailable"},
       }) {
    const urnw::CheckoutFailureText text = urnw::CheckoutFailureTextFor(c.code, english);
    UR_EXPECT_TRUE_MSG(c.code, Key(text) == c.key);
    UR_EXPECT_TRUE_MSG(c.code, text.pageText.empty());
  }
  // the page's other failures, a code this app does not know, a page before
  // the codes (-1), and the pay page's failure, which has no code
  for (const char* code : {"invalid_request", "checkout_error", "checkout_paused", "-1", ""}) {
    const urnw::CheckoutFailureText text = urnw::CheckoutFailureTextFor(code, english);
    UR_EXPECT_TRUE_MSG(code, text.key == nullptr);
    UR_EXPECT_TRUE_MSG(code, text.pageText == english);
  }
  UR_EXPECT_TRUE(Key(urnw::CheckoutFailureTextFor("checkout_error", "")) ==
                 "something_went_wrong_please_try_again_later");
  UR_EXPECT_TRUE(Key(urnw::CheckoutFailureTextFor("", "")) ==
                 "something_went_wrong_please_try_again_later");
}

// Every key the sheet can show is in the linux catalog with its English as the
// msgid; the two new ones are translated in every language the app ships.
UR_TEST(CheckoutBridgeError_StringsAreTranslated) {
  const std::string pot = ReadFile("../po/urnetwork.pot");
  const std::string linguas = ReadFile("../po/LINGUAS");
  if (pot.empty() || linguas.empty()) {
    UR_FAIL("could not read po/urnetwork.pot or po/LINGUAS");
    return;
  }
  std::vector<std::string> languages;
  std::istringstream lines(linguas);
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line[0] != '#') languages.push_back(line);
  }
  UR_EXPECT_TRUE(languages.size() >= 28);
  struct Case {
    const char* code;
    const char* message;
    bool translatedEverywhere;
  };
  for (const Case& c : {
           Case{"checkout_unavailable", "x", true},
           Case{"stripe_unavailable", "x", true},
           Case{"checkout_error", "", false},
       }) {
    const urnw::CheckoutFailureText text = urnw::CheckoutFailureTextFor(c.code, c.message);
    if (text.key == nullptr) {
      UR_FAIL(std::string(c.code) + " has no string");
      continue;
    }
    const std::string entry = "msgctxt \"" + std::string(text.key) + "\"\nmsgid \"" +
                              std::string(text.english) + "\"\nmsgstr \"";
    UR_EXPECT_TRUE_MSG(std::string(text.key) + " in the pot", pot.find(entry) != std::string::npos);
    if (!c.translatedEverywhere) continue;
    for (const auto& language : languages) {
      const std::string po = ReadFile("../po/" + language + ".po");
      const size_t at = po.find(entry);
      UR_EXPECT_TRUE_MSG(std::string(text.key) + " in " + language, at != std::string::npos);
      if (at == std::string::npos) continue;
      const size_t start = at + entry.size();
      const std::string translated = po.substr(start, po.find('"', start) - start);
      UR_EXPECT_TRUE_MSG(std::string(text.key) + " translated in " + language, !translated.empty());
      if (language != "en") {
        UR_EXPECT_TRUE_MSG(std::string(text.key) + " is English in " + language,
                           translated != text.english);
      }
    }
  }
}

// The sheet takes the page's code from the SDK's hand-back and shows its text.
UR_TEST(CheckoutBridgeError_TheCodeReachesTheSheet) {
  const std::string sheet = ReadFile("UpgradeSheet.cpp");
  if (sheet.empty()) {
    UR_FAIL("could not read UpgradeSheet.cpp");
    return;
  }
  const std::string callback = FunctionBody(sheet, "void UpgradeSheet::HandleCheckoutCallback(");
  for (const char* want : {"errorCode = redirect->ErrorCode;",
                           "CheckoutFailureTextFor(errorCode, errorMessage)",
                           "g_dpgettext2(GETTEXT_PACKAGE, failure.key,",
                           "Glib::ustring(failure.pageText)"}) {
    UR_EXPECT_TRUE_MSG(want, callback.find(want) != std::string::npos);
  }
}
