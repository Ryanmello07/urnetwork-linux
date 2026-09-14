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

// The connect sheet, on the windows app's model: the manual field's verdict
// lives BESIDE the flow's state, so a wallet-app round trip and a typed address
// never write over each other.
//
//   state           shows                                 leaves on
//   Idle            providers, the manual entry           a provider -> OpeningBrowser;
//                                                         a plausible address -> Checking
//   OpeningBrowser  opening_wallet_in_browser; providers  the public key -> Linking;
//                   and Connect off, the manual entry     a bridge error -> Failed(detail);
//                   still live                            180 s -> Failed
//   Checking        checking_wallet_address               valid -> Ready; invalid or
//                                                         unanswered -> Idle, with its line
//   Ready           Connect on                            Connect -> Linking
//   Linking         connecting_to_wallet; everything off  a wallet id -> Linked;
//                                                         an error -> Failed(detail); 20 s -> Failed
//   Failed          the failure line; controls on         a provider, Connect, a new address
//   Linked          everything off                        (the sheet closes)
//
// Every round trip is numbered: ChooseProvider and Submit return the attempt's
// number and its answers (PublicKey, BridgeError, CreateResult, Timeout) carry
// it, so an answer for an attempt that was given up or replaced -- the host's
// "superseded by a wallet connect request" for the first of two presses -- is
// refused. A server verdict carries the address it is about, so a verdict for
// text that has since changed is refused too.
enum class ConnectState { Idle, OpeningBrowser, Checking, Ready, Linking, Failed, Linked };

// The answer of the server's validate call for the typed address.
enum class AddressVerdict { Valid, Invalid, Unavailable };

// The manual field's verdict, beside the state: nothing yet, checking with the
// server, accepted, refused, or the check itself failed.
enum class AddressCheck { None, Checking, Valid, Invalid, Unavailable };

// Every event answers whether it was taken; an event the machine does not
// expect changes nothing, and the sheet must act on nothing.
struct ConnectMachine {
  ConnectState state = ConnectState::Idle;
  std::string detail;  // Failed: the bridge's or server's words, "" when none came back
  AddressCheck check = AddressCheck::None;
  std::string address;   // the trimmed typed address `check` is about
  uint64_t attempt = 0;  // the newest round trip's number (0: none yet)
  std::string walletId;  // Linked

  // A round trip is out, or the wallet is linked: the providers and Connect wait.
  bool InFlight() const {
    return state == ConnectState::OpeningBrowser || state == ConnectState::Linking ||
           state == ConnectState::Linked;
  }
  bool ProvidersEnabled() const { return !InFlight(); }
  // The manual toggle and the field: live while the browser is out, off once a
  // wallet is being linked.
  bool EntryEnabled() const {
    return state != ConnectState::Linking && state != ConnectState::Linked;
  }
  // Connect: an accepted address, and no round trip out.
  bool ConnectAllowed() const { return check == AddressCheck::Valid && !InFlight(); }

  // Phantom or Solflare pressed: the browser opens. Returns the attempt's
  // number, 0 when a round trip is already out. The field's verdict, and a
  // check still out for it, are left alone.
  uint64_t ChooseProvider() {
    if (InFlight()) return 0;
    state = ConnectState::OpeningBrowser;
    detail.clear();
    return ++attempt;
  }
  // The bridge came back with the wallet's public key for `round`: link it.
  bool PublicKey(uint64_t round) {
    if (state != ConnectState::OpeningBrowser || round != attempt) return false;
    state = ConnectState::Linking;
    return true;
  }
  // The bridge came back with an error for `round`, or the browser never opened.
  bool BridgeError(uint64_t round, const std::string& why) {
    if (state != ConnectState::OpeningBrowser || round != attempt) return false;
    Fail(why);
    return true;
  }
  // A watchdog gave up on `round`: 180 s on the browser, 20 s on the create
  // call. No words came back, so it reads something_went_wrong.
  bool Timeout(uint64_t round) {
    if (state != ConnectState::OpeningBrowser && state != ConnectState::Linking) return false;
    if (round != attempt) return false;
    Fail(std::string());
    return true;
  }
  // A keystroke: the verdict belonged to the old text. A failure stays on
  // screen until the new text is debounced.
  bool Typed() {
    if (!EntryEnabled()) return false;
    address.clear();
    SettleCheck(AddressCheck::None);
    return true;
  }
  // The debounced text failed the local check: said at once, nothing is sent.
  bool Malformed(const std::string& text) {
    if (!EntryEnabled()) return false;
    NewAddress(text);
    SettleCheck(AddressCheck::Invalid);
    return true;
  }
  // The debounced text passed the local check: the server check goes out. While
  // the browser is out, the check runs without ending that round trip.
  bool Check(const std::string& text) {
    if (!EntryEnabled()) return false;
    NewAddress(text);
    check = AddressCheck::Checking;
    if (state != ConnectState::OpeningBrowser) state = ConnectState::Checking;
    return true;
  }
  // The server's answer for `text`: refused unless that text is being checked.
  bool Verdict(const std::string& text, AddressVerdict verdict) {
    if (check != AddressCheck::Checking || text != address) return false;
    switch (verdict) {
      case AddressVerdict::Valid:
        check = AddressCheck::Valid;
        break;
      case AddressVerdict::Invalid:
        check = AddressCheck::Invalid;
        break;
      case AddressVerdict::Unavailable:
        check = AddressCheck::Unavailable;
        break;
    }
    if (state == ConnectState::Checking) {
      state = check == AddressCheck::Valid ? ConnectState::Ready : ConnectState::Idle;
    }
    return true;
  }
  // Connect pressed for the accepted address. Returns the attempt's number, 0
  // when there is no accepted address or a round trip is out.
  uint64_t Submit() {
    if (!ConnectAllowed()) return 0;
    state = ConnectState::Linking;
    detail.clear();
    return ++attempt;
  }
  // POST /account/wallet answered for `round`. Success is a result carrying a
  // non-empty wallet id; anything else fails with whatever words came back.
  bool CreateResult(uint64_t round, bool ok, const std::string& newWalletId,
                    const std::string& why) {
    if (state != ConnectState::Linking || round != attempt) return false;
    if (ok && !newWalletId.empty()) {
      state = ConnectState::Linked;
      walletId = newWalletId;
      detail.clear();
      return true;
    }
    Fail(why);
    return true;
  }

 private:
  void Fail(const std::string& why) {
    state = ConnectState::Failed;
    detail = why;
  }
  // Checking and Ready are the field's own states: once its verdict is gone or
  // refused they fall back to Idle. A round trip's state is left alone.
  void SettleCheck(AddressCheck verdict) {
    check = verdict;
    if (state == ConnectState::Checking || state == ConnectState::Ready) {
      state = ConnectState::Idle;
    }
  }
  // A newly debounced address is a new attempt: the last failure no longer applies.
  void NewAddress(const std::string& text) {
    address = text;
    if (state == ConnectState::Failed) {
      state = ConnectState::Idle;
      detail.clear();
    }
  }
};
}  // namespace urnw::solana
