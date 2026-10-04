// The add sheet and the host it drives need GTK and the SDK, so this reads
// their sources: the sheet offers every method (AddSignInFlow.hpp), each one
// runs the host's add flow, and the host's add flows end in add-auth, never in
// authLogin or a new jwt.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadAddSignInSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of `signature` (up to the next top-level definition), "" if absent.
std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string AddSheetSource() {
  const std::string source = ReadAddSignInSource("AccountPage.cpp");
  const size_t start = source.find("class AccountAddAuthSheet : public Gtk::Window");
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("class AccountAuthCodeSheet", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

UR_TEST(AddSignInWiring_TheSheetOffersEveryMethod) {
  const std::string sheet = AddSheetSource();
  UR_EXPECT_TRUE(!sheet.empty());
  UR_EXPECT_TRUE(sheet.find("addsignin::Methods()") != std::string::npos);
  UR_EXPECT_TRUE(sheet.find("addsignin::WalletChains()") != std::string::npos);
  UR_EXPECT_TRUE(sheet.find("host_.AddSignInWithSso(") != std::string::npos);
  UR_EXPECT_TRUE(sheet.find("host_.AddSignInWithSolana(") != std::string::npos);
  UR_EXPECT_TRUE(sheet.find("host_.AddSignInWithBittensor(") != std::string::npos);
  UR_EXPECT_TRUE(sheet.find("AppendBittensorWalletChoices(") != std::string::npos);
  // the sheet never signs in
  UR_EXPECT_TRUE(sheet.find("host_.SignInWith") == std::string::npos);
  UR_EXPECT_TRUE(sheet.find("authLogin") == std::string::npos);
}

UR_TEST(AddSignInWiring_HostAddFlowsNeverSignIn) {
  const std::string host = ReadAddSignInSource("SdkHost.cpp");
  UR_EXPECT_TRUE(!host.empty());
  for (const char* signature :
       {"void SdkHost::AddSignInWithSso(", "void SdkHost::AddSignInWithSolana(",
        "void SdkHost::AddSignInWithBittensor(", "void SdkHost::AddAuthMethod("}) {
    const std::string body = FunctionBody(host, signature);
    UR_EXPECT_TRUE_MSG(signature, !body.empty());
    UR_EXPECT_TRUE_MSG(signature, body.find("authLogin") == std::string::npos);
    UR_EXPECT_TRUE_MSG(signature, body.find("setByJwt") == std::string::npos);
    UR_EXPECT_TRUE_MSG(signature, body.find("RegisterNetworkClient") == std::string::npos);
    UR_EXPECT_TRUE_MSG(signature, body.find("walletAuthDone_") == std::string::npos);
  }
  UR_EXPECT_TRUE(FunctionBody(host, "void SdkHost::AddAuthMethod(").find("api_->addAuth(") !=
                 std::string::npos);
  // the Bittensor add asks for its own purpose
  UR_EXPECT_TRUE(FunctionBody(host, "void SdkHost::AddSignInWithBittensor(")
                     .find("bittensor::kPurposeAdd") != std::string::npos);
}

// The api's sso return is routed by owner: the sign-in call sits only under
// the sign-in route, and an add-owned attempt is marked so.
UR_TEST(AddSignInWiring_SsoReturnsAreRoutedByOwner) {
  const std::string host = ReadAddSignInSource("SdkHost.cpp");
  const size_t sso = host.find("wallet_.on_sso = ");
  UR_EXPECT_TRUE(sso != std::string::npos);
  if (sso == std::string::npos) return;
  const size_t end = host.find("wallet_.on_error = ", sso);
  const std::string handler = host.substr(sso, end - sso);
  const size_t route = handler.find("addsignin::RouteSso(");
  const size_t signIn = handler.find("case addsignin::SsoRoute::SignIn:");
  const size_t login = handler.find("AuthLoginWithSso(");
  UR_EXPECT_TRUE(route != std::string::npos);
  UR_EXPECT_TRUE(signIn != std::string::npos && login != std::string::npos && login > signIn);
  UR_EXPECT_TRUE(handler.find("AuthLoginWithSso(", login + 1) == std::string::npos);
  UR_EXPECT_TRUE(FunctionBody(host, "void SdkHost::AddSignInWithSso(")
                     .find("ssoOwner_ = addsignin::Owner::AddSignIn") != std::string::npos);
  UR_EXPECT_TRUE(FunctionBody(host, "void SdkHost::SignInWithSso(")
                     .find("ssoOwner_ = addsignin::Owner::SignIn") != std::string::npos);
}

}  // namespace
