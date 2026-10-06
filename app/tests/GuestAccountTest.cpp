// A legacy guest network (UPGRADE.md D8, A4, S6): the server removed the guest
// upgrade routes and the SDK's UpgradeGuest now always fails, so nothing may
// call it (or create guest networks). Every create-account and upgrade action
// for a guest converts the network in place (GuestConversionSheet: add a
// sign-in, verify it), never signing out, and no checkout opens for a guest.
// Who is a guest includes the server's `guest`, since a refreshed jwt has lost
// its GuestMode claim. The sources need GTK and the SDK, so they are read as
// text; the conversion itself is tested in GuestConversionTest.
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

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

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

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

UR_TEST(nothingCallsTheRemovedGuestUpgrade) {
  for (const char* file : {"SdkHost.cpp", "SdkHost.hpp", "AuthViews.cpp", "AuthViews.hpp",
                           "MainWindow.cpp"}) {
    const std::string source = ReadSource(file);
    UR_EXPECT_TRUE_MSG(file, !source.empty());
    UR_EXPECT_TRUE_MSG(file, !Has(source, "upgradeGuest("));
    UR_EXPECT_TRUE_MSG(file, !Has(source, "UpgradeGuest"));
    UR_EXPECT_TRUE_MSG(file, !Has(source, "LoginAsGuest"));
    UR_EXPECT_TRUE_MSG(file, !Has(source, "guest_mode = true"));
  }
}

UR_TEST(guestConvertsInPlaceAndNeverSignsOut) {
  const std::string window = ReadSource("MainWindow.cpp");
  UR_EXPECT_TRUE(!Has(window, "OfferGuestSignOut"));
  const std::string open = FunctionBody(window, "void MainWindow::OpenGuestConversion()");
  UR_EXPECT_TRUE(Has(open, "guestConversionSheet_->Open()"));
  UR_EXPECT_TRUE(!Has(open, "Logout"));
  // Account's button reads "Create an account" for a guest and asks only for
  // the conversion
  const size_t account = window.find("accountPage_->on_open_upgrade = [this] {");
  UR_EXPECT_TRUE(account != std::string::npos);
  if (account != std::string::npos) {
    const std::string hook = window.substr(account, 200);
    UR_EXPECT_TRUE(Has(hook, "if (balance_.IsGuest()) {\n      OpenGuestConversion();"));
  }
  // the sheet adds and verifies on this network, and never signs in or out
  const std::string sheet = ReadSource("GuestConversionSheet.cpp");
  UR_EXPECT_TRUE(Has(sheet, "host_.api().addAuth("));
  UR_EXPECT_TRUE(Has(sheet, "host_.api().authVerify("));
  UR_EXPECT_TRUE(Has(sheet, "host_.RefreshJwt()"));
  UR_EXPECT_TRUE(!Has(sheet, "Logout"));
  UR_EXPECT_TRUE(!Has(sheet, "host_.VerifyCode("));  // the variant that signs in
  // the guest's upgrade door never reaches checkout: every upgrade entry
  // point lands on the window's OpenUpgrade, which converts a guest first
  const std::string openUpgrade = FunctionBody(window, "void MainWindow::OpenUpgrade()");
  const size_t gate = openUpgrade.find("if (balance_.IsGuest()) {");
  const size_t sheetOpen = openUpgrade.find("upgradeSheet_->Open(");
  UR_EXPECT_TRUE(gate != std::string::npos && sheetOpen != std::string::npos && gate < sheetOpen);
  UR_EXPECT_TRUE(Has(openUpgrade, "DivertGuestToConversion("));
  // the sign-out copy is gone; the conversion copy is in the catalog
  const std::string pot = ReadSource("../po/urnetwork.pot");
  UR_EXPECT_TRUE(!Has(pot, "msgctxt \"guest_sign_out_balance_warning\""));
  UR_EXPECT_TRUE(Has(pot, "msgctxt \"guest_convert_explanation\""));
  UR_EXPECT_TRUE(Has(pot, "msgctxt \"sign_in_method_added_successfully\""));
}

UR_TEST(refreshedGuestIsStillAGuest) {
  // the store reads the server's guest, not only the jwt claim a refresh clears
  const std::string store = ReadSource("SubscriptionBalance.cpp");
  UR_EXPECT_TRUE(Has(store, "serverGuest_ = result->guest.value_or(false);"));
  UR_EXPECT_TRUE(Has(store, "isGuest_ = IsGuestNetwork(jwtGuest_, serverGuest_);"));
  UR_EXPECT_TRUE(!Has(store, "isGuest_ = byJwt->GuestMode;"));
}

// Account > Login methods adds an email or phone through the same add -> code
// -> verify flow and reports it added (reloads the methods) only once the code
// is accepted; before, it reported the sign-in added as soon as addAuth
// answered. The flow itself is tested in GuestConversionTest.
UR_TEST(accountAddedSignInIsVerifiedBeforeItCountsAsAdded) {
  const std::string page = ReadSource("AccountPage.cpp");
  const std::string sheet = FunctionBody(page, "class AccountAddAuthSheet : public Gtk::Window");
  UR_EXPECT_TRUE(!sheet.empty());
  UR_EXPECT_TRUE(Has(sheet, "GuestConversion conversion_;"));
  UR_EXPECT_TRUE(Has(sheet, "sheet.host_.api().addAuth("));
  UR_EXPECT_TRUE(Has(sheet, "host_.ResendVerifyCode("));
  UR_EXPECT_TRUE(Has(sheet, "host_.api().authVerify("));
  UR_EXPECT_TRUE(!Has(sheet, "host_.VerifyCode("));  // the variant that signs in
  // on_changed (the page's reload) runs for an email or phone only on the Done
  // step; the one other call is an Apple, Google or wallet add that add-auth
  // accepted (no code step: AddSignInFlow.hpp NeedsVerification)
  size_t calls = 0;
  for (size_t at = sheet.find("if (on_changed) on_changed();"); at != std::string::npos;
       at = sheet.find("if (on_changed) on_changed();", at + 1)) {
    ++calls;
    const size_t done = sheet.rfind("case GuestConversionStep::", at);
    const size_t added = sheet.rfind("addedMethod_ = method;", at);
    const bool onDone = done != std::string::npos &&
                        sheet.compare(done, 31, "case GuestConversionStep::Done:") == 0 &&
                        (added == std::string::npos || added < done);
    const bool onProviderAdded = added != std::string::npos && at - added < 200 &&
                                 sheet.rfind("if (result.ok)", at) != std::string::npos &&
                                 sheet.rfind("if (result.ok)", at) < added;
    UR_EXPECT_TRUE(onDone || onProviderAdded);
  }
  UR_EXPECT_EQ(size_t{2}, calls);
  // the code page waits out a rate limit
  UR_EXPECT_TRUE(Has(sheet, "resend_->set_sensitive(conversion_.CanResend());"));
  UR_EXPECT_TRUE(Has(sheet, "TickCooldown("));
  const std::string guestSheet = ReadSource("GuestConversionSheet.cpp");
  UR_EXPECT_TRUE(Has(guestSheet, "resend_->set_sensitive(conversion_->CanResend());"));
  UR_EXPECT_TRUE(Has(guestSheet, "ShowVerifySendNotice(*notice_, conversion_->ShownNotice());"));
}

// A purchase entry that sent a guest to the conversion continues to the
// upgrade it was opening once the conversion is done and the guest clears
// (GuestUpgradeContinuation, tested in GuestConversionTest); it used to close
// back to where the user started. Account's "Create an account" asks only
// for the conversion.
UR_TEST(guestPurchaseContinuesAfterTheConversion) {
  const std::string window = ReadSource("MainWindow.cpp");
  const std::string openUpgrade = FunctionBody(window, "void MainWindow::OpenUpgrade()");
  UR_EXPECT_TRUE_MSG("the upgrade continues after the conversion",
                     Has(openUpgrade, "DivertGuestToConversion([this] { OpenUpgrade(); });"));
  const std::string divert = FunctionBody(window, "void MainWindow::DivertGuestToConversion(");
  UR_EXPECT_TRUE(Has(divert, "guestUpgrade_.Divert(std::move(checkout));"));
  UR_EXPECT_TRUE(Has(divert, "OpenGuestConversion();"));
  const std::string open = FunctionBody(window, "void MainWindow::OpenGuestConversion()");
  UR_EXPECT_TRUE(Has(open, "guestUpgrade_.ConversionDone();"));
  UR_EXPECT_TRUE(Has(open, "guestUpgrade_.ConversionClosed();"));
  UR_EXPECT_TRUE(Has(open, "guestUpgrade_.Poll(balance_.IsGuest());"));
  UR_EXPECT_TRUE_MSG("the balance change continues a waiting purchase",
                     Has(window, "    guestUpgrade_.Poll(balance_.IsGuest());\n"));
  // on_done runs before the hide the window's handler reads
  const std::string sheet = ReadSource("GuestConversionSheet.cpp");
  const size_t done = sheet.find("if (on_done) on_done();");
  const size_t hide = sheet.find("set_visible(false);", sheet.find("case GuestConversionStep::Done:"));
  UR_EXPECT_TRUE(done != std::string::npos && hide != std::string::npos && done < hide);
}

}  // namespace
