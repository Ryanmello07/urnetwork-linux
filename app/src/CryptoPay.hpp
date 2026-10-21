// The payment-method toggle (Card · USDC · Zcash · Monero) and the native
// crypto invoice (mmm/ZCASH-CONTRACT.md §6, mmm/ZCASH.md §0.1 and §0.3), the
// half that needs no GTK and no SDK, so tests/CryptoPayTest.cpp pins it.
//
// The SDK owns the money: which coins the server enabled (the balance
// result's crypto_assets, filtered by urnet::paymentMethods), the invoice
// itself (urnet::CryptoInvoiceViewController: quote, countdown, re-quote,
// status, the saved invoice) and the payment URI check
// (urnet::checkCryptoPaymentUri). This header only decides what the screens
// show from those answers:
//
//   * the method row: Card first, then each coin the server lists, always in
//     the order USDC, Zcash, Monero; no row at all with no coin listed, so the
//     surface looks exactly as before; a remembered coin the server no longer
//     lists falls back to Card;
//   * a coin buys the yearly plan, or the welcome offer (yearly_onboarding)
//     while it is active. Monthly is card only: picking a coin greys the
//     monthly card and moves the selection to yearly;
//   * the invoice, top to bottom: the network line, the QR and Open in wallet
//     (only for a URI that passed the SDK's check, and only while the quote can
//     still be paid), the exact amount, the address and USDC's memo, the
//     countdown with Refresh quote (the expired line once it ran out), the
//     status steps (waiting -> payment seen -> confirming n of N -> paid; USDC
//     is never seen or confirming, so it has two), what is left to send after
//     a partial payment, the review copy alone for a payment in review, the
//     wallets that work, and Zcash's exchange note;
//   * the renewal notice of a one-time crypto year (§2.2 crypto_term), and the
//     "does not renew" reading of a crypto store.
//
// Copy lives with the screens (T_ calls in PaymentMethodRow.cpp,
// CryptoInvoiceView.cpp, UpgradeSheet.cpp, Onboarding.cpp and AccountPage.cpp)
// except the per-id tables below, whose {key, English} pairs the catalog test
// reads like any lookup.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace urnw::crypto_pay {

// ---- ids (the SDK's crypto_pay.go constants and the server's vocabulary) ----

// A method is Card or a coin; a coin is named by its asset id.
inline constexpr const char* kMethodCard = "card";            // urnet::PaymentMethodCard
inline constexpr const char* kAssetUsdcSolana = "usdc_solana";  // urnet::CryptoAssetUsdcSolana
inline constexpr const char* kAssetZec = "zec";                // urnet::CryptoAssetZec
inline constexpr const char* kAssetXmr = "xmr";                // urnet::CryptoAssetXmr

// The plans a coin sells (mmm/ZCASH-CONTRACT.md §1 "item"; decision 6: no coin sells
// monthly).
inline constexpr const char* kPlanYearly = "yearly";
inline constexpr const char* kPlanYearlyOnboarding = "yearly_onboarding";

// The invoice's phase (urnet::CryptoInvoiceState.Phase, §4.4).
inline constexpr const char* kPhaseNone = "none";
inline constexpr const char* kPhaseStarting = "starting";
inline constexpr const char* kPhaseOpen = "open";
inline constexpr const char* kPhasePaid = "paid";
inline constexpr const char* kPhaseFailed = "failed";

// The invoice's status (§2.5), first match wins on the server.
inline constexpr const char* kStatusUnknown = "unknown";
inline constexpr const char* kStatusPaid = "paid";
inline constexpr const char* kStatusReview = "review";
inline constexpr const char* kStatusConfirming = "confirming";
inline constexpr const char* kStatusSeen = "seen";
inline constexpr const char* kStatusExpired = "expired";
inline constexpr const char* kStatusPending = "pending";

// The purchase error codes the crypto endpoints add (§2.7), and the one every
// purchase endpoint shares for a legacy guest network.
inline constexpr const char* kErrorAssetUnavailable = "asset_unavailable";
inline constexpr const char* kErrorQuoteExpired = "quote_expired";
inline constexpr const char* kErrorUnderpaid = "underpaid";
inline constexpr const char* kErrorGuestSignInRequired = "guest_sign_in_required";

// The store family urnet::classifySubscriptionStore gives a one-time crypto
// renewal (markets solana, zcash, monero; §4.3): it never renews.
inline constexpr const char* kStoreFamilyCrypto = "crypto";

// A store key and its English, the gettext msgid (I18n.hpp T_).
struct Text {
  const char* key;
  const char* english;
};

inline bool IsCoin(std::string_view method) {
  return method == kAssetUsdcSolana || method == kAssetZec || method == kAssetXmr;
}

// The segment's label in the method row ("Card", "USDC", "Zcash", "Monero";
// the coin names are never translated). {nullptr, nullptr} for an id this app
// does not know.
inline Text MethodLabel(std::string_view method) {
  if (method == kMethodCard) return {"pay_method_card", "Card"};
  if (method == kAssetUsdcSolana) return {"pay_method_usdc", "USDC"};
  if (method == kAssetZec) return {"pay_method_zcash", "Zcash"};
  if (method == kAssetXmr) return {"pay_method_monero", "Monero"};
  return {nullptr, nullptr};
}

// The invoice's first line: the network to send on, named before anything
// else (the USDC panel's lesson: "which network do I select?").
inline Text NetworkLine(std::string_view asset) {
  if (asset == kAssetUsdcSolana) return {"crypto_network_usdc", "USDC on Solana"};
  if (asset == kAssetZec) return {"crypto_network_zcash", "Zcash, shielded"};
  if (asset == kAssetXmr) return {"crypto_network_monero", "Monero"};
  return {nullptr, nullptr};
}

// The client events' store for a method (§6.1: card = stripe; the coins are
// the markets solana, zcash, monero). "" for an unknown method.
inline const char* EventStore(std::string_view method) {
  if (method == kMethodCard) return "stripe";
  if (method == kAssetUsdcSolana) return "solana";
  if (method == kAssetZec) return "zcash";
  if (method == kAssetXmr) return "monero";
  return "";
}

// ---- the method row -----------------------------------------------------------

// The row's segments from the SDK's answer (urnet::paymentMethods: card, then
// each enabled coin that sells the item): Card first, then the coins this app
// knows, always USDC, Zcash, Monero, each once. Card is always there, whatever
// the answer: it is the method that needs no server switch.
inline std::vector<std::string> RowMethods(const std::vector<std::string>& sdkMethods) {
  std::vector<std::string> methods{kMethodCard};
  for (const char* coin : {kAssetUsdcSolana, kAssetZec, kAssetXmr}) {
    if (std::find(sdkMethods.begin(), sdkMethods.end(), coin) != sdkMethods.end()) {
      methods.emplace_back(coin);
    }
  }
  return methods;
}

// The row shows only with a coin beside Card: with none listed the surface
// looks exactly as before the toggle (§6.1).
inline bool MethodRowShown(const std::vector<std::string>& methods) {
  return std::any_of(methods.begin(), methods.end(),
                     [](const std::string& method) { return IsCoin(method); });
}

// The selection a surface opens with: the method remembered on this device
// when the row lists it, else Card (a remembered coin the server switched off
// falls back to Card).
inline std::string MethodSelection(const std::vector<std::string>& methods,
                                   const std::string& remembered) {
  if (!remembered.empty() &&
      std::find(methods.begin(), methods.end(), remembered) != methods.end()) {
    return remembered;
  }
  return kMethodCard;
}

// Monthly is card only (urnet::paymentMethodAllowsPlan(method, "monthly")).
inline bool MonthlyAllowed(std::string_view method) { return !IsCoin(method); }

// The yearly plan card's lines between its price and its trial line, top to
// bottom, for the selected method (§6.1: no renewal or trial wording while a
// coin is selected). Card shows them all. A coin buys one year once: the
// offer's renewal line ("then $40/year", `secondaryRenews`) goes, while "Save
// N%" and the monthly equivalent stay. The trial line itself is the card's
// alone (PlanPicker hides it for a coin). Empty texts are no line.
inline std::vector<std::string> YearlyCardLines(const std::string& secondary, bool secondaryRenews,
                                                const std::string& equivalent, bool coin) {
  std::vector<std::string> lines;
  if (!secondary.empty() && !(coin && secondaryRenews)) lines.push_back(secondary);
  if (!equivalent.empty()) lines.push_back(equivalent);
  return lines;
}

// The plan a coin buys: the welcome offer while the surface shows it, the
// yearly plan otherwise. It is also the one item the row asks
// urnet::paymentMethods about, so a coin is listed only when it sells exactly
// what the plan card prices.
inline const char* CoinPlan(bool offerActive) {
  return offerActive ? kPlanYearlyOnboarding : kPlanYearly;
}

// What a refused start leads to (§6.1). asset_unavailable keeps the coin
// selectable with "Temporarily unavailable" under the row; a legacy guest
// goes to the conversion, like a refused card checkout; anything else is the
// surface's own refusal line (ServerRefusalText.hpp).
enum class StartRefusal { None, Unavailable, GuestSignIn, Other };

inline StartRefusal StartRefusalFor(const std::string& errorCode, bool failed) {
  if (errorCode == kErrorAssetUnavailable) return StartRefusal::Unavailable;
  if (errorCode == kErrorGuestSignInRequired) return StartRefusal::GuestSignIn;
  if (!errorCode.empty() || failed) return StartRefusal::Other;
  return StartRefusal::None;
}

// ---- the invoice --------------------------------------------------------------

// urnet::CryptoInvoiceState as the screens read it, plus the two answers the
// host asks the SDK for beside it: whether the URI passed
// urnet::checkCryptoPaymentUri, and urnet::cryptoWalletSuggestions.
struct InvoiceView {
  std::string phase;  // none | starting | open | paid | failed
  std::string intentId;
  std::string asset;
  std::string plan;
  std::string itemId;
  std::string address;
  std::string amount;  // exact decimal string, never a float
  std::string ticker;  // USDC | ZEC | XMR
  std::string uri;     // built by the server; shown and opened, never built here
  std::string memo;    // USDC only
  double priceUsd = 0;
  std::string expiresAt;
  int64_t secondsLeft = 0;  // 0 once expired
  std::string status;       // §2.5
  std::string reviewReason;
  std::string received;
  std::string remaining;
  int64_t confirmations = 0;
  int64_t confirmationsRequired = 0;
  bool refreshing = false;  // a re-quote is in flight
  std::string errorCode;    // the last start or re-quote failure
  std::string errorMessage;
  // beside the state
  bool uriChecked = false;
  std::vector<std::string> wallets;
};

enum class InvoiceStage { None, Starting, Open, Paid, Failed };

inline InvoiceStage StageOf(const InvoiceView& invoice) {
  // a credited invoice is paid whatever phase the state reached
  if (invoice.phase == kPhasePaid || invoice.status == kStatusPaid) return InvoiceStage::Paid;
  if (invoice.phase == kPhaseStarting) return InvoiceStage::Starting;
  if (invoice.phase == kPhaseOpen) return InvoiceStage::Open;
  if (invoice.phase == kPhaseFailed) return InvoiceStage::Failed;
  return InvoiceStage::None;
}

// Whether the invoice belongs on screen as a payment still going on: the
// "payment in progress" entry, and the upgrade sheet opening onto it.
inline bool InvoiceInProgress(const InvoiceView& invoice) {
  return StageOf(invoice) == InvoiceStage::Open && invoice.status != kStatusUnknown;
}

// m:ss, minutes unbounded ("29:59", "0:05", "60:00"); never negative.
inline std::string FormatCountdown(int64_t seconds) {
  if (seconds < 0) seconds = 0;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld:%02lld", static_cast<long long>(seconds / 60),
                static_cast<long long>(seconds % 60));
  return buf;
}

// "0.072387 XMR": the exact amount and its ticker.
inline std::string AmountWithTicker(const std::string& amount, const std::string& ticker) {
  if (ticker.empty()) return amount;
  return amount + " " + ticker;
}

// A decimal string above zero ("0.000100" is, "0" and "0.000000" are not).
inline bool AmountAboveZero(const std::string& amount) {
  return std::any_of(amount.begin(), amount.end(), [](char c) { return '1' <= c && c <= '9'; });
}

// The wallet names as one list ("Cake, Feather, Monerujo").
inline std::string JoinWallets(const std::vector<std::string>& wallets) {
  std::string out;
  for (const std::string& wallet : wallets) {
    if (wallet.empty()) continue;
    if (!out.empty()) out += ", ";
    out += wallet;
  }
  return out;
}

// The status steps: waiting -> payment seen -> confirming n of N -> paid.
enum class StepKind { Waiting, Seen, Confirming, Paid };
enum class StepState { Done, Current, Upcoming };
struct Step {
  StepKind kind;
  StepState state;
};

// The steps an asset goes through. USDC is reported pending, paid or expired
// only (the scanner sees confirmed transfers), so it has no seen or
// confirming step to wait on.
inline std::vector<Step> StepsFor(const std::string& asset, const std::string& status) {
  std::vector<StepKind> kinds;
  if (asset == kAssetUsdcSolana) {
    kinds = {StepKind::Waiting, StepKind::Paid};
  } else {
    kinds = {StepKind::Waiting, StepKind::Seen, StepKind::Confirming, StepKind::Paid};
  }
  StepKind current = StepKind::Waiting;
  if (status == kStatusSeen) current = StepKind::Seen;
  if (status == kStatusConfirming) current = StepKind::Confirming;
  if (status == kStatusPaid) current = StepKind::Paid;
  std::vector<Step> steps;
  bool reached = false;
  for (const StepKind kind : kinds) {
    if (kind == current) {
      // paid is the last step: it reads as done, not as something to wait on
      steps.push_back({kind, kind == StepKind::Paid ? StepState::Done : StepState::Current});
      reached = true;
    } else {
      steps.push_back({kind, reached ? StepState::Upcoming : StepState::Done});
    }
  }
  return steps;
}

// The line under the invoice when something went wrong with it: a coin that
// cannot be quoted now (asset_unavailable on a re-quote), or an invoice the
// server no longer knows or a re-quote it refused. An expired quote and an
// underpayment are not failures here: the expired line and the review copy
// say those.
enum class InvoiceNotice { None, Unavailable, Failed };

// What the invoice shows, top to bottom (§6.2).
struct InvoicePresentation {
  bool usdcWarning = false;   // usdc_solana_network_only, under the address
  bool showQr = false;        // the QR and Open in wallet
  bool showAmount = false;    // Send exactly, the amount and Copy
  std::string amountText;     // "0.072387 XMR"
  bool showAddress = false;   // To this address, the address and Copy
  bool showMemo = false;      // USDC's memo and Copy
  bool showCountdown = false; // Quote expires in m:ss
  std::string countdownText;  // m:ss
  bool expired = false;       // the expired line
  bool showRefresh = false;   // Refresh quote
  bool refreshEnabled = false;
  std::vector<Step> steps;    // empty: no steps (expired, review, failed)
  int64_t confirmations = 0;
  int64_t confirmationsRequired = 0;
  bool showRemaining = false;  // "{amount} more to send"
  std::string remainingText;   // "0.010000 XMR"
  bool review = false;         // the review copy, and nothing else to do
  bool showWallets = false;
  std::string walletsText;
  bool zcashNote = false;
  InvoiceNotice notice = InvoiceNotice::None;
  bool showPayAnotherWay = false;  // only while nothing was received
};

inline InvoicePresentation PresentInvoice(const InvoiceView& invoice) {
  InvoicePresentation p;
  const InvoiceStage stage = StageOf(invoice);
  const bool open = stage == InvoiceStage::Open;
  const std::string& status = invoice.status;
  // Nothing received, as the server said: the only time a re-quote is allowed
  // (§2.6), and the only time walking away from the invoice cannot strand
  // money. A resumed invoice has no status until its first poll (empty), and
  // money may already be on its way then: it shows only its waiting steps
  // until the server answers (CONTRACT §6.2.1).
  const bool nothingReceived = status == kStatusPending || status == kStatusExpired;
  // The quote ran out: the server says so, or the countdown reached zero
  // first. Paying a stale amount is not offered: the amount, the QR and Open
  // in wallet wait for a fresh quote.
  p.expired = open && (status == kStatusExpired ||
                       (status == kStatusPending && invoice.secondsLeft <= 0));
  const bool payable = open && status == kStatusPending && !p.expired;
  // something arrived but not enough: the address stays for the top-up
  const bool partial = open && status == kStatusSeen;

  p.showAmount = payable;
  p.amountText = AmountWithTicker(invoice.amount, invoice.ticker);
  // the server's URI, only once the SDK has checked its scheme and address
  p.showQr = payable && invoice.uriChecked && !invoice.uri.empty();
  p.showAddress = (payable || partial) && !invoice.address.empty();
  p.showMemo = payable && invoice.asset == kAssetUsdcSolana && !invoice.memo.empty();
  p.usdcWarning = p.showAddress && invoice.asset == kAssetUsdcSolana;
  p.showCountdown = payable;
  p.countdownText = FormatCountdown(invoice.secondsLeft);
  p.showRefresh = open && nothingReceived;
  p.refreshEnabled = p.showRefresh && !invoice.refreshing;

  p.review = (open || stage == InvoiceStage::Paid) && status == kStatusReview;
  const bool stepped = status.empty() || status == kStatusPending || status == kStatusSeen ||
                       status == kStatusConfirming || status == kStatusPaid;
  if ((open && stepped && !p.expired) || stage == InvoiceStage::Paid) {
    p.steps = StepsFor(invoice.asset,
                       stage == InvoiceStage::Paid ? std::string(kStatusPaid) : status);
  }
  if (p.review) p.steps.clear();
  p.confirmations = invoice.confirmations;
  p.confirmationsRequired = invoice.confirmationsRequired;
  p.showRemaining = partial && AmountAboveZero(invoice.remaining);
  p.remainingText = AmountWithTicker(invoice.remaining, invoice.ticker);

  const std::string wallets = JoinWallets(invoice.wallets);
  p.showWallets = (payable || partial) && !wallets.empty();
  p.walletsText = wallets;
  p.zcashNote = (payable || partial) && invoice.asset == kAssetZec;

  if (invoice.errorCode == kErrorAssetUnavailable) {
    p.notice = InvoiceNotice::Unavailable;
  } else if (status == kStatusUnknown || stage == InvoiceStage::Failed ||
             (!invoice.errorCode.empty() && invoice.errorCode != kErrorQuoteExpired &&
              invoice.errorCode != kErrorUnderpaid)) {
    p.notice = InvoiceNotice::Failed;
  }
  // Paying another way abandons the invoice: offered while nothing was
  // received, and when the invoice failed. Money on its way (seen,
  // confirming), paid or in review leaves only Close.
  p.showPayAnotherWay = (open && nothingReceived) || status == kStatusUnknown ||
                        stage == InvoiceStage::Failed;
  return p;
}

// ---- the account: a one-time crypto year ---------------------------------------
// (When its renewal notice shows is the SDK's: urnet::cryptoTermNoticeDue,
// term_end_time <= now < grace_end_time, never before.)

// A one-time crypto year does not renew (urnet::classifySubscriptionStore
// "crypto"): the Account plan says so, and nothing offers to manage it.
inline bool StoreDoesNotRenew(const std::string& storeFamily) {
  return storeFamily == kStoreFamilyCrypto;
}

// ---- the QR -------------------------------------------------------------------

// The quiet zone the QR spec asks for, in modules on each side.
inline constexpr int kQrQuietModules = 4;
// Pixels a module aims for, and the canvas bounds. A Monero subaddress URI is
// about 160 bytes (version 10 at level M, 57 modules); a Zcash unified
// address with two receivers about 260 (version 14, 73 modules).
inline constexpr int kQrTargetPxPerModule = 4;
inline constexpr int kQrMinSide = 200;
inline constexpr int kQrMaxSide = 320;

// The canvas side a code of `moduleCount` modules wants, in px.
inline int QrCanvasSide(int moduleCount) {
  if (moduleCount <= 0) return kQrMinSide;
  const int total = moduleCount + 2 * kQrQuietModules;
  return std::clamp(total * kQrTargetPxPerModule, kQrMinSide, kQrMaxSide);
}

// Whole pixels per module for a `side` canvas: an integer module size, or the
// edges antialias and a dense code stops scanning (ExtenderShareSheet.cpp
// measured it). 0 when the code does not fit at all.
inline int QrModulePixels(int side, int moduleCount) {
  if (side <= 0 || moduleCount <= 0) return 0;
  return side / (moduleCount + 2 * kQrQuietModules);
}

}  // namespace urnw::crypto_pay
