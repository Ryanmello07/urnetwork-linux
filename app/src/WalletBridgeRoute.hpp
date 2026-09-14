// Which waiting flow a wallet-bridge return belongs to. The bridge is a pair of
// process-wide callbacks with no request id, so a return nobody waits for (the
// bridge page's "Return to URnetwork" after its automatic redirect, a tab from
// an abandoned or superseded attempt) is dropped, never turned into a sign-in.
//
// The twin of the windows app's WalletBridgeRoute.h: the same routes, plus the
// create-network signature (a wallet sign-in that found no network finishes by
// signing a second challenge), and no Seeker signature request. Header-only and
// free of GTK and the SDK (tests/WalletBridgeRouteTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string_view>

namespace urnw::bridge {

// every reason a superseded wallet flow is answered with starts with this
inline constexpr std::string_view kSupersededPrefix = "superseded by ";

constexpr bool IsSuperseded(std::string_view error) noexcept {
  return error.substr(0, kSupersededPrefix.size()) == kSupersededPrefix;
}

// A connect return (urnetwork://<phantom|solflare>-connect) carries the
// wallet's public key.
enum class PublicKeyRoute { Drop, AnswerConnect, SignIn };

constexpr PublicKeyRoute RoutePublicKey(bool bittensor, bool connectWaiting,
                                        bool walletSignInWaiting) noexcept {
  if (bittensor) return PublicKeyRoute::Drop;  // no connect hop to chain
  if (connectWaiting) return PublicKeyRoute::AnswerConnect;
  return walletSignInWaiting ? PublicKeyRoute::SignIn : PublicKeyRoute::Drop;
}

// A signature return carries the wallet's signature over the last message the
// app sent it.
enum class SignatureRoute { Drop, AnswerRequest, FinishCreate, SignIn };

constexpr SignatureRoute RouteSignature(bool signWaiting, bool createWaiting,
                                        bool walletSignInWaiting) noexcept {
  if (signWaiting) return SignatureRoute::AnswerRequest;
  if (createWaiting) return SignatureRoute::FinishCreate;
  return walletSignInWaiting ? SignatureRoute::SignIn : SignatureRoute::Drop;
}

}  // namespace urnw::bridge
