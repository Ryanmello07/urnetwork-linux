// The server refuses a payment sheet or checkout session for a legacy guest
// network with error.code guest_sign_in_required (server refuseGuestPurchase)
// when the app had not read the guest from the balance yet. The upgrade sheet
// does not fall through to the next checkout path or show the server's
// sentence: it hides, and its owner opens the conversion, then the upgrade
// again once a sign-in is added.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadAppFile(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of the function whose definition starts with `signature`, braces
// balanced from the first '{' after it, or "".
std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t at = source.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t open = source.find('{', at);
  if (open == std::string::npos) return std::string();
  int depth = 0;
  for (size_t i = open; i < source.size(); ++i) {
    if (source[i] == '{') ++depth;
    if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
  }
  return std::string();
}

bool Has(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

UR_TEST(GuestRefusal_TheSheetStopsBeforeAnyFallback) {
  const std::string sheet = ReadAppFile("src/UpgradeSheet.cpp");
  const std::string mapping =
      "PurchaseRefusalFor(result->error->code.value_or(std::string())) ==\n"
      "                  PurchaseRefusal::AddSignIn";
  for (const char* signature :
       {"void UpgradeSheet::RequestPaymentSheet(", "void UpgradeSheet::RequestSession("}) {
    const std::string body = FunctionBody(sheet, signature);
    const size_t mapped = body.find(mapping);
    const size_t refused = body.find("RefuseForGuest();");
    UR_EXPECT_TRUE_MSG(signature, mapped != std::string::npos && refused != std::string::npos);
    // before the fallback to the next path and before any error line
    for (const char* after : {"RequestSession(/*embedded=*/", "OpenPaySheet(", "OpenEmbedded(", "fail("}) {
      const size_t at = body.find(after, body.find("PostToMain("));
      UR_EXPECT_TRUE_MSG(after, at == std::string::npos || refused < at);
    }
  }
  const std::string refuse = FunctionBody(sheet, "void UpgradeSheet::RefuseForGuest(");
  UR_EXPECT_TRUE(Has(refuse, "hide();"));
  UR_EXPECT_TRUE(Has(refuse, "if (on_guest_sign_in_required) on_guest_sign_in_required();"));
  UR_EXPECT_FALSE(Has(refuse, "errorLabel_"));
}

UR_TEST(GuestRefusal_TheOwnersOpenTheConversion) {
  // the drawer's sheet goes through the drawer's guest gate
  const std::string drawer = ReadAppFile("src/ConnectDrawer.cpp");
  const size_t wired = drawer.find("upgradeSheet_->on_guest_sign_in_required = [this] {");
  UR_EXPECT_TRUE(wired != std::string::npos);
  UR_EXPECT_TRUE(drawer.find("if (on_guest_upgrade) on_guest_upgrade([this] { OpenUpgrade(); });", wired) !=
                 std::string::npos);

  // the onboarding flow steps aside, and every owner of it opens the conversion
  const std::string onboarding = ReadAppFile("src/Onboarding.cpp");
  UR_EXPECT_TRUE(Has(onboarding, "checkout_->on_guest_sign_in_required = [this] {"));
  UR_EXPECT_TRUE(Has(onboarding, "if (on_guest_sign_in_required) on_guest_sign_in_required();"));
  const std::string window = ReadAppFile("src/MainWindow.cpp");
  size_t created = 0;
  size_t wiredOnboarding = 0;
  for (size_t at = window.find("std::make_unique<OnboardingWindow>("); at != std::string::npos;
       at = window.find("std::make_unique<OnboardingWindow>(", at + 1)) {
    ++created;
  }
  for (size_t at = window.find("onboarding_->on_guest_sign_in_required = [this] { OnOnboardingGuestSignInRequired(); };");
       at != std::string::npos;
       at = window.find("onboarding_->on_guest_sign_in_required = [this] { OnOnboardingGuestSignInRequired(); };", at + 1)) {
    ++wiredOnboarding;
  }
  UR_EXPECT_TRUE(0 < created && created == wiredOnboarding);
  UR_EXPECT_TRUE(Has(FunctionBody(window, "void MainWindow::OnOnboardingGuestSignInRequired("),
                     "DivertGuestToConversion("));
}
