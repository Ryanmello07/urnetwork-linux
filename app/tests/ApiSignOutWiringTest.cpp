// The GUI's Api reports its own sign-out to the app. The sdk clears the
// account credential the server rejects, or the one the account signs out
// from its Sessions list (ClientSessionViewController's self-revoke), and
// says so on that Api (Api.addAuthLogoutListener). The GUI listened only on
// the daemon's device, so with no tunnel up, or for the account credential
// the device does not carry, the app went on looking signed in on a
// credential that was gone. REVOKE-UI-FINAL.md §4: after a successful
// self-sign-out the app follows its normal logout flow, whether or not the
// Sessions page is still on screen. SdkHost needs the SDK, so this reads its
// sources.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

// Every Api the GUI adopts (ClientInfoWiringTest pins that each `api_ =` is
// adopted) forwards its sign-out to the handler the device's listener uses,
// and drops the previous Api's subscription first.
UR_TEST(ApiSignOut_EveryAdoptedApiReportsItsSignOut) {
  const std::string host = ReadCode("SdkHost.cpp");
  const std::string adopt = FunctionBody(host, "void SdkHost::AdoptSpaceApiLocked()");
  UR_EXPECT_TRUE_MSG("SdkHost::AdoptSpaceApiLocked is defined", !adopt.empty());
  UR_EXPECT_TRUE(Before(adopt, "apiLogoutSub_.reset();",
                        "apiLogoutSub_.emplace(api_->addAuthLogoutListener("));
  const size_t listen = adopt.find("api_->addAuthLogoutListener(");
  UR_EXPECT_TRUE(listen != std::string::npos &&
                 adopt.find("if (onAuthInvalid_) onAuthInvalid_();", listen) != std::string::npos);
  // the device's listener goes to the same handler
  const std::string bind = FunctionBody(host, "TunnelStartResult SdkHost::BindRemoteDeviceLocked(");
  UR_EXPECT_TRUE(Contains(bind, "device_->addAuthLogoutListener("));
  UR_EXPECT_TRUE(Contains(bind, "if (onAuthInvalid_) onAuthInvalid_();"));
}

// The subscription closes before the handler it calls and the Api it listens
// to are destroyed: members die in reverse order of declaration.
UR_TEST(ApiSignOut_TheSubscriptionClosesFirst) {
  const std::string header = ReadCode("SdkHost.hpp");
  UR_EXPECT_TRUE(Before(header, "std::optional<urnet::Api> api_;",
                        "std::optional<urnet::Sub> apiLogoutSub_;"));
  UR_EXPECT_TRUE(Before(header, "AuthInvalidHandler onAuthInvalid_;",
                        "std::optional<urnet::Sub> apiLogoutSub_;"));
}

// The handler marshals to the main loop and runs the app's sign-out.
UR_TEST(ApiSignOut_TheHandlerRunsTheNormalLogout) {
  const std::string window = ReadCode("MainWindow.cpp");
  const size_t handler = window.find("host_.SetAuthInvalidHandler([this] {");
  UR_EXPECT_TRUE(handler != std::string::npos);
  const size_t logout = window.find("PostToMain([this] { host_.Logout(); });", handler);
  UR_EXPECT_TRUE(logout != std::string::npos && logout - handler < 120);
}
