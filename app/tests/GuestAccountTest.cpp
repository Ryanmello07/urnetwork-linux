// A legacy guest network (UPGRADE.md D8, S6): the server removed the guest
// upgrade routes and the SDK's UpgradeGuest now always fails, so nothing may
// call it (or create guest networks). Every create-account and upgrade action
// for a guest offers to sign out instead, warning that the guest balance stays
// on the guest network, and no checkout opens for a guest. The sources need
// GTK and the SDK, so they are read as text.
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

UR_TEST(guestGetsTheSignOutOfferWithTheBalanceWarning) {
  const std::string window = ReadSource("MainWindow.cpp");
  const std::string offer = FunctionBody(window, "void MainWindow::OfferGuestSignOut()");
  UR_EXPECT_TRUE(Has(offer, "\"guest_sign_out_balance_warning\""));
  UR_EXPECT_TRUE(Has(offer, "\"guest_sign_out_and_create_account\""));
  // signing out abandons the guest network: cancel is the default
  UR_EXPECT_TRUE(Has(offer, "adw_message_dialog_set_default_response(ADW_MESSAGE_DIALOG(dialog), \"cancel\")"));
  UR_EXPECT_TRUE(Has(offer, "host_.Logout()"));
  UR_EXPECT_TRUE(Has(window, "drawer_->on_create_account = [this] { OfferGuestSignOut(); };"));
  // the guest's upgrade door never reaches checkout
  const std::string drawer = ReadSource("ConnectDrawer.cpp");
  const std::string openUpgrade = FunctionBody(drawer, "void ConnectDrawer::OpenUpgrade()");
  UR_EXPECT_TRUE(Has(openUpgrade, "if (balance_.IsGuest())"));
  UR_EXPECT_TRUE(Has(openUpgrade, "on_create_account()"));
  // the catalog carries the copy
  const std::string pot = ReadSource("../po/urnetwork.pot");
  UR_EXPECT_TRUE(Has(pot, "msgctxt \"guest_sign_out_balance_warning\""));
  UR_EXPECT_TRUE(Has(pot, "msgctxt \"guest_sign_out_and_create_account\""));
}

}  // namespace
