// The Solana payout wallet on the Earnings page, decided in pure steps so every
// display rule and every transition of the connect sheet is deterministic and
// testable; the page and the sheet only draw what this decided.
//
// USDC payouts continue to a Solana payout wallet until the migration to
// Bittensor completes. A network whose payouts are held because it has no
// payout wallet is emailed "N USDC waiting"; the page shows the same figure
// (the payments neither completed nor canceled) and the wallet that receives
// it, and the connect sheet links one -- through the ur.io wallet bridge
// (Phantom / Solflare hand back the public key; nothing is signed) or by
// address, checked here for its base58 shape and then by the server.
//
// Header-only and free of GTK and the SDK so the unit tests need no vendored
// headers (tests/SolanaWalletPresentationTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace urnw::solana {

// ---- the address ---------------------------------------------------------------

inline std::string Trim(std::string text) {
  static constexpr const char* kSpace = " \t\r\n\f\v";
  const size_t first = text.find_first_not_of(kSpace);
  if (first == std::string::npos) return std::string();
  const size_t last = text.find_last_not_of(kSpace);
  return text.substr(first, last - first + 1);
}

// The base58 alphabet: no 0, O, I or l.
inline constexpr const char* kBase58Alphabet =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
// A Solana public key is 32 bytes, which base58 writes in 32 to 44 characters.
inline constexpr size_t kMinAddressLength = 32;
inline constexpr size_t kMaxAddressLength = 44;

// The local check before any network call: the SHAPE of a Solana address, not
// proof of one. A failure is shown at once and nothing is sent; a pass still
// goes to the server's validate call. A key the bridge hands back is not
// checked here at all -- the server validates it when the wallet is created.
inline bool LooksLikeSolanaAddress(const std::string& text) {
  const std::string address = Trim(text);
  if (address.size() < kMinAddressLength || address.size() > kMaxAddressLength) return false;
  const std::string alphabet = kBase58Alphabet;
  for (const char c : address) {
    if (alphabet.find(c) == std::string::npos) return false;
  }
  return true;
}

// "7Xk9…3fQa": the first and last four characters. Nine or fewer characters
// come back whole (the short form would be no shorter), and so does anything
// that is not plain ASCII -- an address never is, and a byte cut must never
// split a character.
inline std::string ShortAddress(const std::string& address) {
  if (address.size() <= 9) return address;
  for (const char c : address) {
    if (static_cast<unsigned char>(c) >= 0x80) return address;
  }
  return address.substr(0, 4) + "\xE2\x80\xA6" + address.substr(address.size() - 4);
}

// ---- the payout wallet -----------------------------------------------------------

// urnet::TAO, mirrored so this header stays SDK-free.
inline constexpr const char* kChainBittensor = "TAO";

// One account wallet (urnet::AccountWallet), reduced to what the card reads.
struct LegacyWallet {
  std::string id;
  std::string blockchain;      // SOL | MATIC | TAO
  std::string address;
  std::string circleWalletId;  // non-empty: a custodial Circle wallet
  bool active = false;
};

// The wallet the card shows: the network's payout wallet, when it is a wallet
// the user connected. Circle wallets are custodial, a TAO account wallet can
// never be a payout wallet (the server refuses it; the Bittensor block above
// is the coldkey), and a removed wallet is inactive. A legacy Polygon payout
// wallet (MATIC) still receives USDC and is shown.
inline std::optional<LegacyWallet> PayoutWalletFor(const std::vector<LegacyWallet>& wallets,
                                                   const std::string& payoutWalletId) {
  if (payoutWalletId.empty()) return std::nullopt;
  for (const auto& wallet : wallets) {
    if (!wallet.circleWalletId.empty()) continue;
    if (wallet.blockchain == kChainBittensor) continue;
    if (!wallet.active) continue;
    if (wallet.id == payoutWalletId) return wallet;
  }
  return std::nullopt;
}

// Linking a wallet makes it the payout wallet unless it already is (the server
// does that on its own only when the network had none).
inline bool NeedsPayoutSwitch(const std::string& newWalletId, const std::string& payoutWalletId) {
  return !newWalletId.empty() && newWalletId != payoutWalletId;
}

// ---- the held USDC -------------------------------------------------------------

// One account payment (urnet::AccountPayment), reduced to what the figure reads.
struct HeldPayment {
  int64_t payoutNanoCents = 0;
  bool completed = false;
  bool canceled = false;
};

// What is waiting: every payment neither completed nor canceled -- the payouts
// held for want of a wallet, which the server retries once one exists.
inline int64_t PendingUsdcNanoCents(const std::vector<HeldPayment>& payments) {
  int64_t total = 0;
  for (const auto& payment : payments) {
    if (payment.completed || payment.canceled) continue;
    total += payment.payoutNanoCents;
  }
  return total;
}

// 1 USD = 1e9 nano cents: the SDK's NanoCentsToUsd scale (urnet::nanoCentsToUsd),
// mirrored so this header stays SDK-free.
inline constexpr int64_t kNanoCentsPerUsd = 1000000000;

// The figure in whole cents, half a cent rounding up (away from zero).
inline int64_t RoundedCents(int64_t nanoCents) {
  const uint64_t perCent = static_cast<uint64_t>(kNanoCentsPerUsd / 100);
  const bool negative = nanoCents < 0;
  const uint64_t magnitude = negative ? uint64_t(0) - static_cast<uint64_t>(nanoCents)
                                      : static_cast<uint64_t>(nanoCents);
  const int64_t cents = static_cast<int64_t>((magnitude + perCent / 2) / perCent);
  return negative ? -cents : cents;
}

// "3.87": two decimals always, no grouping (the email's "3.87 USDC waiting").
inline std::string FormatUsd(int64_t nanoCents) {
  const int64_t cents = RoundedCents(nanoCents);
  const uint64_t magnitude = cents < 0 ? uint64_t(0) - static_cast<uint64_t>(cents)
                                       : static_cast<uint64_t>(cents);
  const uint64_t fraction = magnitude % 100;
  std::string text = std::to_string(magnitude / 100) + "." + (fraction < 10 ? "0" : "") +
                     std::to_string(fraction);
  return cents < 0 ? "-" + text : text;
}

// ---- the page's reading ----------------------------------------------------------

// The Solana card under the Bittensor block, and the one "N USDC waiting" line
// beside the wallet actions when there is no card. Nothing shows until the
// legacy reads are in (loading and failed hide both: the reads are secondary
// and never disturb the Bittensor block), and a figure that rounds to 0.00 is
// no figure.
struct CardView {
  bool showCard = false;
  bool showPending = false;      // the card's own "N USDC waiting" line
  bool showWaitingLine = false;  // no card: the line beside the wallet actions
  std::string address;           // full: tooltip, accessible name
  std::string shortAddress;      // the visible form
  std::string pendingUsd;        // "3.87"; empty when nothing is waiting
};

inline CardView CardFor(bool ready, const std::optional<LegacyWallet>& payoutWallet,
                        int64_t pendingNanoCents) {
  CardView view;
  if (!ready) return view;
  const bool waiting = RoundedCents(pendingNanoCents) > 0;
  if (waiting) view.pendingUsd = FormatUsd(pendingNanoCents);
  if (payoutWallet) {
    view.showCard = true;
    view.address = payoutWallet->address;
    view.shortAddress = ShortAddress(payoutWallet->address);
    view.showPending = waiting;
    return view;
  }
  view.showWaitingLine = waiting;
  return view;
}

// ---- the connect sheet -----------------------------------------------------------

// The store key a connect, link or remove failure renders with -- one rule on
// every platform: the words that came back, in
// error_connecting_wallet_with_reason, and something_went_wrong only when none
// did (a watchdog's give-up included).
inline const char* FailureKey(const std::string& detail) {
  return detail.empty() ? "something_went_wrong" : "error_connecting_wallet_with_reason";
}

enum class ConnectState { Idle, OpeningBrowser, Checking, Ready, Linking, Failed, Linked };

// The answer of the server's validate call for the typed address.
enum class AddressVerdict { Valid, Invalid, Unavailable };

// What the manual field's supporting line says.
enum class Supporting { None, Checking, Invalid, Unavailable };

// The sheet's one state, driven by events. Every event answers whether it was
// taken; an event the state does not expect -- a public key after the user
// moved on, a verdict for text that has since changed -- changes nothing and
// the sheet must act on nothing.
struct ConnectMachine {
  ConnectState state = ConnectState::Idle;
  std::string detail;  // Failed: the bridge's or server's words, "" when none came back
  Supporting supporting = Supporting::None;
  bool addressChecked = false;  // the address in the field passed the server check
  std::string walletId;         // Linked

  // A round trip is out (the browser or the create call): one at a time.
  bool Busy() const {
    return state == ConnectState::OpeningBrowser || state == ConnectState::Linking;
  }
  // The states that take a new attempt: the providers, the manual toggle and
  // the field are live in these and only these.
  bool AcceptsInput() const {
    return state == ConnectState::Idle || state == ConnectState::Checking ||
           state == ConnectState::Ready || state == ConnectState::Failed;
  }
  // Connect is live for a checked address; after a failed attempt the same
  // checked address may be sent again.
  bool ConnectAllowed() const {
    return state == ConnectState::Ready || (state == ConnectState::Failed && addressChecked);
  }

  // Phantom or Solflare pressed: the browser opens.
  bool ChooseProvider() {
    if (!AcceptsInput()) return false;
    state = ConnectState::OpeningBrowser;
    detail.clear();
    supporting = Supporting::None;
    return true;
  }
  // The bridge came back with the wallet's public key: link it.
  bool PublicKey() {
    if (state != ConnectState::OpeningBrowser) return false;
    state = ConnectState::Linking;
    return true;
  }
  // The bridge came back with an error (or the browser never opened).
  bool BridgeError(const std::string& why) {
    if (state != ConnectState::OpeningBrowser) return false;
    state = ConnectState::Failed;
    detail = why;
    return true;
  }
  // A watchdog gave up: 180 s on the browser, 20 s on the create call. No words
  // came back, so it reads something_went_wrong.
  bool Timeout() {
    if (!Busy()) return false;
    state = ConnectState::Failed;
    detail.clear();
    return true;
  }
  // The field's text changed: the verdict is forgotten.
  bool Typed() {
    if (!AcceptsInput()) return false;
    state = ConnectState::Idle;
    detail.clear();
    supporting = Supporting::None;
    addressChecked = false;
    return true;
  }
  // The debounced text failed the local check: said at once, nothing is sent.
  bool Malformed() {
    if (state != ConnectState::Idle) return false;
    supporting = Supporting::Invalid;
    return true;
  }
  // The debounced text passed the local check: the server check goes out.
  bool Check() {
    if (state != ConnectState::Idle) return false;
    state = ConnectState::Checking;
    supporting = Supporting::Checking;
    return true;
  }
  bool Verdict(AddressVerdict verdict) {
    if (state != ConnectState::Checking) return false;
    switch (verdict) {
      case AddressVerdict::Valid:
        state = ConnectState::Ready;
        supporting = Supporting::None;
        addressChecked = true;
        break;
      case AddressVerdict::Invalid:
        state = ConnectState::Idle;
        supporting = Supporting::Invalid;
        break;
      case AddressVerdict::Unavailable:
        state = ConnectState::Idle;
        supporting = Supporting::Unavailable;
        break;
    }
    return true;
  }
  // Connect pressed for the checked address.
  bool Submit() {
    if (!ConnectAllowed()) return false;
    state = ConnectState::Linking;
    detail.clear();
    return true;
  }
  // POST /account/wallet answered. Success is a result carrying a non-empty
  // wallet id; anything else is a failure with whatever words came back.
  bool CreateResult(bool ok, const std::string& newWalletId, const std::string& why) {
    if (state != ConnectState::Linking) return false;
    if (ok && !newWalletId.empty()) {
      state = ConnectState::Linked;
      walletId = newWalletId;
      return true;
    }
    state = ConnectState::Failed;
    detail = why;
    return true;
  }
};

}  // namespace urnw::solana
