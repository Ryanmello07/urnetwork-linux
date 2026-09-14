// The wallet bridge's return routing: every public key and signature goes to
// the flow waiting for it, and one nobody waits for is dropped rather than
// turned into a wallet sign-in in a signed-in app.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "WalletBridgeRoute.hpp"

using urnw::bridge::IsSuperseded;
using urnw::bridge::PublicKeyRoute;
using urnw::bridge::RoutePublicKey;
using urnw::bridge::RouteSignature;
using urnw::bridge::SignatureRoute;

// The bridge page keeps "Return to URnetwork" on screen after its automatic
// redirect and the key pair lives until the next connect, so the same return
// can arrive twice; with nothing waiting it is dropped.
UR_TEST(WalletBridge_AConnectReturnNobodyWaitsForIsDropped) {
  UR_EXPECT_TRUE(RoutePublicKey(false, false, false) == PublicKeyRoute::Drop);
}

// A bare connect (the Solana payout wallet) takes the key before a sign-in could.
UR_TEST(WalletBridge_TheBareConnectIsAnsweredFirst) {
  UR_EXPECT_TRUE(RoutePublicKey(false, true, true) == PublicKeyRoute::AnswerConnect);
  UR_EXPECT_TRUE(RoutePublicKey(false, true, false) == PublicKeyRoute::AnswerConnect);
}

// Signing in with Phantom or Solflare still chains its challenge.
UR_TEST(WalletBridge_AWalletSignInStillSignsIn) {
  UR_EXPECT_TRUE(RoutePublicKey(false, false, true) == PublicKeyRoute::SignIn);
}

// Bittensor signs in one hop: a public key never chains anything for it.
UR_TEST(WalletBridge_ABittensorConnectReturnChainsNothing) {
  UR_EXPECT_TRUE(RoutePublicKey(true, true, true) == PublicKeyRoute::Drop);
  UR_EXPECT_TRUE(RoutePublicKey(true, false, true) == PublicKeyRoute::Drop);
}

// A signature from a superseded or abandoned tab never reaches
// AuthLoginWithWallet; a waiting request comes first, then a network being
// created with a wallet, then a wallet sign-in.
UR_TEST(WalletBridge_ASignatureNobodyWaitsForNeverSignsIn) {
  UR_EXPECT_TRUE(RouteSignature(false, false, false) == SignatureRoute::Drop);
  UR_EXPECT_TRUE(RouteSignature(true, true, true) == SignatureRoute::AnswerRequest);
  UR_EXPECT_TRUE(RouteSignature(true, false, false) == SignatureRoute::AnswerRequest);
  UR_EXPECT_TRUE(RouteSignature(false, true, true) == SignatureRoute::FinishCreate);
  UR_EXPECT_TRUE(RouteSignature(false, true, false) == SignatureRoute::FinishCreate);
  UR_EXPECT_TRUE(RouteSignature(false, false, true) == SignatureRoute::SignIn);
}

UR_TEST(WalletBridge_SupersededIsTheHostsOwnWord) {
  UR_EXPECT_TRUE(IsSuperseded("superseded by a wallet connect request"));
  UR_EXPECT_FALSE(IsSuperseded("User rejected the request."));
  UR_EXPECT_FALSE(IsSuperseded(""));
  UR_EXPECT_FALSE(IsSuperseded("superseded"));
}
