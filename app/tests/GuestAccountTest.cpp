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
                           "MainWindow.cpp", "ConnectDrawer.cpp"}) {
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
  UR_EXPECT_TRUE(Has(window, "drawer_->on_create_account = [this] { OpenGuestConversion(); };"));
  // the sheet adds and verifies on this network, and never signs in or out
  const std::string sheet = ReadSource("GuestConversionSheet.cpp");
  UR_EXPECT_TRUE(Has(sheet, "host_.api().addAuth("));
  UR_EXPECT_TRUE(Has(sheet, "host_.api().authVerify("));
  UR_EXPECT_TRUE(Has(sheet, "host_.RefreshJwt()"));
  UR_EXPECT_TRUE(!Has(sheet, "Logout"));
  UR_EXPECT_TRUE(!Has(sheet, "host_.VerifyCode("));  // the variant that signs in
  // the guest's upgrade door never reaches checkout
  const std::string drawer = ReadSource("ConnectDrawer.cpp");
  const std::string openUpgrade = FunctionBody(drawer, "void ConnectDrawer::OpenUpgrade()");
  UR_EXPECT_TRUE(Has(openUpgrade, "if (balance_.IsGuest())"));
  UR_EXPECT_TRUE(Has(openUpgrade, "on_create_account()"));
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

// The conversion sheet's Resend follows the rate limit the way the verify page
// does (GuestConversion's ResendCooldown, tested in GuestConversionTest): off
// until the retry time, with the notice re-rendered every second.
UR_TEST(guestConversionResendFollowsTheRateLimit) {
  const std::string sheet = ReadSource("GuestConversionSheet.cpp");
  const std::string render = FunctionBody(sheet, "void GuestConversionSheet::Render()");
  UR_EXPECT_TRUE(!render.empty());
  UR_EXPECT_TRUE_MSG("Resend follows CanResend", Has(render, "resend_->set_sensitive(conversion_->CanResend());"));
  UR_EXPECT_TRUE_MSG("Resend is not only gated on busy", !Has(render, "resend_->set_sensitive(!busy);"));
  UR_EXPECT_TRUE_MSG("the countdown ticks", Has(render, "if (conversion_->CoolingDown())"));
  UR_EXPECT_TRUE(Has(render, "Glib::signal_timeout().connect_seconds("));
  UR_EXPECT_TRUE(Has(sheet, "int64_t NowSeconds() override { return MonotonicSeconds(); }"));
}

}  // namespace
