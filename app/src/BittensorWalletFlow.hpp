// What the app does with the SDK Bittensor wallet session's answers
// (sdk bittensor_wallet.go, BittensorWalletSession). The SDK decides whether a
// wallet result is a proof; this header decides what each refusal means for
// the flow on screen, and which string explains it. Header-only and free of
// GTK and the SDK (tests/BittensorWalletFlowTest.cpp).
//
// Wallets and transports on linux (the SDK's BittensorWalletTransportFor):
//   - Talisman: browser_bridge. The system browser opens
//     https://ur.io/wallet-connect?...&wallet=talisman, the page drives the
//     Talisman extension, and the result comes back on the app's existing
//     urnetwork://bittensor-sign-message scheme handler (the .desktop file's
//     x-scheme-handler/urnetwork).
//   - TAO.com: manual. TAO.com documents no programmatic interface, so the
//     manual sheet shows the challenge and takes the address and a pasted
//     signature.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string_view>

namespace urnw::bittensor {

// the redirect link the bridge returns on (already registered by the .desktop)
inline constexpr std::string_view kRedirectLink = "urnetwork://bittensor-sign-message";
inline constexpr std::string_view kPlatform = "linux";

inline constexpr std::string_view kWalletTalisman = "talisman";
inline constexpr std::string_view kWalletTaoCom = "taocom";

inline constexpr std::string_view kPurposeLogin = "login";
inline constexpr std::string_view kPurposeCreate = "create";
inline constexpr std::string_view kPurposeConnect = "connect";
// adding the wallet as a sign-in method (AddSignInFlow.hpp): a login or create
// session refuses its return (purpose_mismatch), so it never signs in
inline constexpr std::string_view kPurposeAdd = "add";

inline constexpr std::string_view kTransportBrowserBridge = "browser_bridge";
inline constexpr std::string_view kTransportManual = "manual";

// The error every flow is answered with when the user closes the manual
// sheet: the flow ends by their choice, so nothing is shown as a failure.
inline constexpr std::string_view kCancelled = "bittensor wallet request cancelled";

constexpr bool IsCancelled(std::string_view error) noexcept { return error == kCancelled; }

// What a session answer means for the flow waiting on it.
enum class Outcome {
  // a proof: continue the flow (sign in, create, connect)
  Accept,
  // not this flow's answer (another purpose, a late or replayed return): drop
  // it and keep waiting
  Ignore,
  // manual entry the user can correct: keep the sheet open and say why
  Retry,
  // the flow is over: answer it with the error
  Fail,
};

constexpr Outcome Classify(std::string_view errorCode, bool manual) noexcept {
  if (errorCode.empty()) return Outcome::Accept;
  if (errorCode == "purpose_mismatch" || errorCode == "not_bittensor_return" ||
      errorCode == "not_awaiting_wallet") {
    return Outcome::Ignore;
  }
  if (manual && (errorCode == "invalid_signature" || errorCode == "address_mismatch" ||
                 errorCode == "invalid_ss58_address")) {
    return Outcome::Retry;
  }
  return Outcome::Fail;
}

// The store key (and its English, the gettext msgid) explaining a refusal.
// An empty key means: show the wallet's own words (wallet_error), or the
// generic failure when there are none.
struct ErrorText {
  std::string_view key;
  std::string_view english;
};

constexpr ErrorText ErrorTextFor(std::string_view errorCode) noexcept {
  if (errorCode == "invalid_signature") {
    return {"bittensor_error_invalid_signature",
            "That is not a valid signature. Paste the full 0x signature from your wallet."};
  }
  if (errorCode == "challenge_expired") {
    return {"bittensor_error_challenge_expired",
            "This request expired. Start again to get a new message to sign."};
  }
  if (errorCode == "message_mismatch") {
    return {"bittensor_error_message_mismatch",
            "The wallet signed a different request. Start again."};
  }
  if (errorCode == "address_mismatch") {
    return {"earnings_wallet_mismatch", "The wallet that signed is not the address you entered."};
  }
  if (errorCode == "invalid_ss58_address") {
    return {"invalid_ss58_address", "That is not a valid Bittensor address."};
  }
  return {"", ""};
}

// How long a flow waits for its answer. A bridge round trip reports nothing
// when the tab is closed, so it times out; the manual sheet is bounded by the
// challenge itself (5 minutes) plus the time to submit.
inline constexpr uint32_t kBridgeTimeoutMs = 180'000;
inline constexpr uint32_t kManualTimeoutMs = 330'000;

constexpr uint32_t TimeoutMsFor(std::string_view transport) noexcept {
  return transport == kTransportManual ? kManualTimeoutMs : kBridgeTimeoutMs;
}

}  // namespace urnw::bittensor
