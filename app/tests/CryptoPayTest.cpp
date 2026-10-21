// The payment-method toggle and the crypto invoice's rules (CryptoPay.hpp,
// PaymentQr.hpp; mmm/ZCASH-CONTRACT.md §6): the method row's segments,
// selection and plan rules, what the invoice shows in each state, the
// renewal notice's window and the QR's encoding and sizing. The screens that
// show them need GTK and the SDK; CryptoPayWiringTest.cpp reads those.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "CryptoInvoiceStorage.hpp"
#include "CryptoPay.hpp"
#include "PaymentQr.hpp"

namespace {

namespace cp = urnw::crypto_pay;

using Methods = std::vector<std::string>;

// An open, payable Monero invoice as the host hands it over.
cp::InvoiceView OpenXmr() {
  cp::InvoiceView v;
  v.phase = "open";
  v.intentId = "0b6f0000-0000-4000-8000-000000000001";
  v.asset = "xmr";
  v.plan = "yearly";
  v.address = "8" + std::string(94, 'A');
  v.amount = "0.072387";
  v.ticker = "XMR";
  v.uri = "monero:" + v.address + "?tx_amount=0.072387&tx_description=URnetwork%20Pro%20yearly";
  v.expiresAt = "2026-10-20T18:30:00Z";
  v.secondsLeft = 1799;
  v.status = "pending";
  v.remaining = "0.072387";
  v.confirmationsRequired = 1;
  v.uriChecked = true;
  v.wallets = {"Cake", "Feather", "Monerujo", "Stack", "Unstoppable"};
  return v;
}

std::string Kinds(const std::vector<cp::Step>& steps) {
  std::string out;
  for (const cp::Step& step : steps) {
    const char* kind = step.kind == cp::StepKind::Waiting  ? "waiting"
                       : step.kind == cp::StepKind::Seen   ? "seen"
                       : step.kind == cp::StepKind::Confirming ? "confirming"
                                                           : "paid";
    const char* state = step.state == cp::StepState::Done      ? "done"
                        : step.state == cp::StepState::Current ? "current"
                                                               : "upcoming";
    if (!out.empty()) out += " ";
    out += std::string(kind) + "=" + state;
  }
  return out;
}

}  // namespace

// Card first, then the coins in the fixed order, whatever order the answer
// came in; ids this app has no label for are left out; Card is always there.
UR_TEST(CryptoPay_RowMethodsAreCardThenTheCoinsInOrder) {
  UR_EXPECT_TRUE(cp::RowMethods({"card", "xmr", "usdc_solana", "zec"}) ==
                 Methods({"card", "usdc_solana", "zec", "xmr"}));
  UR_EXPECT_TRUE(cp::RowMethods({"xmr", "doge", "xmr"}) == Methods({"card", "xmr"}));
  UR_EXPECT_TRUE(cp::RowMethods({}) == Methods({"card"}));
  UR_EXPECT_TRUE(cp::RowMethods({"card"}) == Methods({"card"}));
}

// No coin listed: no row, and the surface looks as it did before the toggle.
UR_TEST(CryptoPay_TheRowShowsOnlyWithACoin) {
  UR_EXPECT_FALSE(cp::MethodRowShown({"card"}));
  UR_EXPECT_FALSE(cp::MethodRowShown({}));
  UR_EXPECT_TRUE(cp::MethodRowShown({"card", "usdc_solana"}));
  UR_EXPECT_TRUE(cp::MethodRowShown({"card", "xmr"}));
}

// The remembered method when listed, else Card.
UR_TEST(CryptoPay_ARememberedCoinTheServerDroppedFallsBackToCard) {
  const Methods methods{"card", "usdc_solana", "xmr"};
  UR_EXPECT_TRUE(cp::MethodSelection(methods, "xmr") == "xmr");
  UR_EXPECT_TRUE(cp::MethodSelection(methods, "card") == "card");
  UR_EXPECT_TRUE(cp::MethodSelection(methods, "zec") == "card");
  UR_EXPECT_TRUE(cp::MethodSelection(methods, "") == "card");
  UR_EXPECT_TRUE(cp::MethodSelection({"card"}, "usdc_solana") == "card");
}

// Monthly is card only; a coin buys the yearly plan, or the welcome offer.
UR_TEST(CryptoPay_ACoinBuysYearlyOrTheOffer) {
  UR_EXPECT_TRUE(cp::MonthlyAllowed("card"));
  for (const char* coin : {"usdc_solana", "zec", "xmr"}) {
    UR_EXPECT_FALSE(cp::MonthlyAllowed(coin));
    UR_EXPECT_TRUE(cp::IsCoin(coin));
  }
  UR_EXPECT_FALSE(cp::IsCoin("card"));
  UR_EXPECT_FALSE(cp::IsCoin(""));
  UR_EXPECT_TRUE(std::string(cp::CoinPlan(false)) == "yearly");
  UR_EXPECT_TRUE(std::string(cp::CoinPlan(true)) == "yearly_onboarding");
}

// No renewal or trial wording while a coin is selected (§6.1): under the
// yearly price, the offer's "then $40/year" goes for a coin; "Save N%" and the
// monthly equivalent stay; Card keeps everything.
UR_TEST(CryptoPay_ACoinShowsNoRenewalLine) {
  using Lines = std::vector<std::string>;
  const std::string then = "then $40/year";
  const std::string save = "Save 33%";
  const std::string equivalent = "\xE2\x89\x88 $3.34/month \xC2\xB7 billed once a year";
  // the welcome offer: the renewal price under the first-year price
  UR_EXPECT_TRUE(cp::YearlyCardLines(then, true, equivalent, false) == Lines({then, equivalent}));
  UR_EXPECT_TRUE(cp::YearlyCardLines(then, true, equivalent, true) == Lines({equivalent}));
  UR_EXPECT_TRUE(cp::YearlyCardLines(then, true, "", true).empty());
  // no offer: the saving is not renewal wording
  UR_EXPECT_TRUE(cp::YearlyCardLines(save, false, equivalent, true) == Lines({save, equivalent}));
  UR_EXPECT_TRUE(cp::YearlyCardLines(save, false, "", false) == Lines({save}));
  UR_EXPECT_TRUE(cp::YearlyCardLines("", false, "", true).empty());
}

// The client events name the method's store (§6.1).
UR_TEST(CryptoPay_EventsUseTheMethodsStore) {
  UR_EXPECT_TRUE(std::string(cp::EventStore("card")) == "stripe");
  UR_EXPECT_TRUE(std::string(cp::EventStore("usdc_solana")) == "solana");
  UR_EXPECT_TRUE(std::string(cp::EventStore("zec")) == "zcash");
  UR_EXPECT_TRUE(std::string(cp::EventStore("xmr")) == "monero");
  UR_EXPECT_TRUE(std::string(cp::EventStore("doge")).empty());
}

// The segment labels and the invoice's network line are the store's keys.
UR_TEST(CryptoPay_LabelsAndNetworkLinesAreTheStoresKeys) {
  const auto is = [](cp::Text text, const char* key, const char* english) {
    return text.key && text.english && std::string(text.key) == key &&
           std::string(text.english) == english;
  };
  UR_EXPECT_TRUE(is(cp::MethodLabel("card"), "pay_method_card", "Card"));
  UR_EXPECT_TRUE(is(cp::MethodLabel("usdc_solana"), "pay_method_usdc", "USDC"));
  UR_EXPECT_TRUE(is(cp::MethodLabel("zec"), "pay_method_zcash", "Zcash"));
  UR_EXPECT_TRUE(is(cp::MethodLabel("xmr"), "pay_method_monero", "Monero"));
  UR_EXPECT_TRUE(cp::MethodLabel("doge").key == nullptr);
  UR_EXPECT_TRUE(is(cp::NetworkLine("usdc_solana"), "crypto_network_usdc", "USDC on Solana"));
  UR_EXPECT_TRUE(is(cp::NetworkLine("zec"), "crypto_network_zcash", "Zcash, shielded"));
  UR_EXPECT_TRUE(is(cp::NetworkLine("xmr"), "crypto_network_monero", "Monero"));
  UR_EXPECT_TRUE(cp::NetworkLine("card").key == nullptr);
}

// A refused start: unavailable stays on the coin, a guest converts, the rest
// is the surface's own refusal line.
UR_TEST(CryptoPay_StartRefusals) {
  UR_EXPECT_TRUE(cp::StartRefusalFor("asset_unavailable", true) == cp::StartRefusal::Unavailable);
  UR_EXPECT_TRUE(cp::StartRefusalFor("guest_sign_in_required", true) ==
                 cp::StartRefusal::GuestSignIn);
  for (const char* code : {"rate_limited", "offer_unavailable", "start_failed", "plan_unavailable",
                           "invalid_request"}) {
    UR_EXPECT_TRUE_MSG(code, cp::StartRefusalFor(code, true) == cp::StartRefusal::Other);
  }
  // a transport failure carries no code
  UR_EXPECT_TRUE(cp::StartRefusalFor("", true) == cp::StartRefusal::Other);
  UR_EXPECT_TRUE(cp::StartRefusalFor("", false) == cp::StartRefusal::None);
}

UR_TEST(CryptoPay_Stages) {
  cp::InvoiceView v;
  UR_EXPECT_TRUE(cp::StageOf(v) == cp::InvoiceStage::None);
  v.phase = "none";
  UR_EXPECT_TRUE(cp::StageOf(v) == cp::InvoiceStage::None);
  v.phase = "starting";
  UR_EXPECT_TRUE(cp::StageOf(v) == cp::InvoiceStage::Starting);
  v.phase = "failed";
  UR_EXPECT_TRUE(cp::StageOf(v) == cp::InvoiceStage::Failed);
  v.phase = "open";
  UR_EXPECT_TRUE(cp::StageOf(v) == cp::InvoiceStage::Open);
  v.status = "paid";  // credited: paid whatever the phase reached
  UR_EXPECT_TRUE(cp::StageOf(v) == cp::InvoiceStage::Paid);
  v.status = "";
  v.phase = "paid";
  UR_EXPECT_TRUE(cp::StageOf(v) == cp::InvoiceStage::Paid);
}

// A payment in progress: an open invoice the server still knows.
UR_TEST(CryptoPay_InProgress) {
  cp::InvoiceView v = OpenXmr();
  UR_EXPECT_TRUE(cp::InvoiceInProgress(v));
  for (const char* status : {"seen", "confirming", "review", "expired"}) {
    v.status = status;
    UR_EXPECT_TRUE_MSG(status, cp::InvoiceInProgress(v));
  }
  v.status = "unknown";
  UR_EXPECT_FALSE(cp::InvoiceInProgress(v));
  v.status = "paid";
  UR_EXPECT_FALSE(cp::InvoiceInProgress(v));
  v = OpenXmr();
  v.phase = "starting";
  UR_EXPECT_FALSE(cp::InvoiceInProgress(v));
  v.phase = "none";
  UR_EXPECT_FALSE(cp::InvoiceInProgress(v));
}

UR_TEST(CryptoPay_Formats) {
  UR_EXPECT_TRUE(cp::FormatCountdown(1799) == "29:59");
  UR_EXPECT_TRUE(cp::FormatCountdown(5) == "0:05");
  UR_EXPECT_TRUE(cp::FormatCountdown(3600) == "60:00");
  UR_EXPECT_TRUE(cp::FormatCountdown(0) == "0:00");
  UR_EXPECT_TRUE(cp::FormatCountdown(-3) == "0:00");
  UR_EXPECT_TRUE(cp::AmountWithTicker("0.072387", "XMR") == "0.072387 XMR");
  UR_EXPECT_TRUE(cp::AmountWithTicker("40.012345", "") == "40.012345");
  UR_EXPECT_TRUE(cp::AmountAboveZero("0.000100"));
  UR_EXPECT_FALSE(cp::AmountAboveZero("0"));
  UR_EXPECT_FALSE(cp::AmountAboveZero("0.000000"));
  UR_EXPECT_FALSE(cp::AmountAboveZero(""));
  UR_EXPECT_TRUE(cp::JoinWallets({"Cake", "", "Feather"}) == "Cake, Feather");
  UR_EXPECT_TRUE(cp::JoinWallets({}).empty());
}

// waiting -> payment seen -> confirming n of N -> paid; USDC has two steps.
UR_TEST(CryptoPay_StatusSteps) {
  UR_EXPECT_TRUE(Kinds(cp::StepsFor("xmr", "pending")) ==
                 "waiting=current seen=upcoming confirming=upcoming paid=upcoming");
  UR_EXPECT_TRUE(Kinds(cp::StepsFor("xmr", "seen")) ==
                 "waiting=done seen=current confirming=upcoming paid=upcoming");
  UR_EXPECT_TRUE(Kinds(cp::StepsFor("zec", "confirming")) ==
                 "waiting=done seen=done confirming=current paid=upcoming");
  UR_EXPECT_TRUE(Kinds(cp::StepsFor("zec", "paid")) ==
                 "waiting=done seen=done confirming=done paid=done");
  UR_EXPECT_TRUE(Kinds(cp::StepsFor("usdc_solana", "pending")) == "waiting=current paid=upcoming");
  UR_EXPECT_TRUE(Kinds(cp::StepsFor("usdc_solana", "paid")) == "waiting=done paid=done");
  UR_EXPECT_TRUE(Kinds(cp::StepsFor("xmr", "")) ==
                 "waiting=current seen=upcoming confirming=upcoming paid=upcoming");
}

// Pending with time left: everything to pay with, and Pay another way.
UR_TEST(CryptoPay_APendingInvoiceShowsEverythingToPayWith) {
  const cp::InvoicePresentation p = cp::PresentInvoice(OpenXmr());
  UR_EXPECT_TRUE(p.showQr);
  UR_EXPECT_TRUE(p.showAmount);
  UR_EXPECT_TRUE(p.amountText == "0.072387 XMR");
  UR_EXPECT_TRUE(p.showAddress);
  UR_EXPECT_FALSE(p.showMemo);  // USDC only
  UR_EXPECT_FALSE(p.usdcWarning);
  UR_EXPECT_TRUE(p.showCountdown);
  UR_EXPECT_TRUE(p.countdownText == "29:59");
  UR_EXPECT_FALSE(p.expired);
  UR_EXPECT_TRUE(p.showRefresh && p.refreshEnabled);
  UR_EXPECT_TRUE(Kinds(p.steps) == "waiting=current seen=upcoming confirming=upcoming paid=upcoming");
  UR_EXPECT_FALSE(p.showRemaining);
  UR_EXPECT_FALSE(p.review);
  UR_EXPECT_TRUE(p.showWallets);
  UR_EXPECT_TRUE(p.walletsText == "Cake, Feather, Monerujo, Stack, Unstoppable");
  UR_EXPECT_FALSE(p.zcashNote);
  UR_EXPECT_TRUE(p.notice == cp::InvoiceNotice::None);
  UR_EXPECT_TRUE(p.showPayAnotherWay);
}

// A URI the SDK's check refused is never shown or opened; the amount and the
// address still are, so the invoice can be paid by hand.
UR_TEST(CryptoPay_AnUncheckedUriShowsNoQr) {
  cp::InvoiceView v = OpenXmr();
  v.uriChecked = false;
  cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_FALSE(p.showQr);
  UR_EXPECT_TRUE(p.showAmount && p.showAddress);
  // Requoted on another device (mmm/ZCASH-CONTRACT.md §4.4.1): the controller blanks the
  // URI, which asked for the old amount. No QR and no Open in wallet; the new
  // exact amount and the address to send it to by hand.
  v = OpenXmr();
  v.uri.clear();
  v.uriChecked = false;
  v.amount = "0.071000";
  p = cp::PresentInvoice(v);
  UR_EXPECT_FALSE(p.showQr);
  UR_EXPECT_TRUE(p.showAmount && p.amountText == "0.071000 XMR");
  UR_EXPECT_TRUE(p.showAddress && p.showCountdown && p.showRefresh);
}

// Expired, as the server says or as the countdown ran out: no stale amount
// to pay, the expired line, and Refresh quote (disabled while one runs).
UR_TEST(CryptoPay_AnExpiredQuoteOffersOnlyARefresh) {
  for (int variant = 0; variant < 2; ++variant) {
    cp::InvoiceView v = OpenXmr();
    if (variant == 0) {
      v.status = "expired";
      v.secondsLeft = 0;
    } else {
      v.secondsLeft = 0;  // still pending on the server
    }
    cp::InvoicePresentation p = cp::PresentInvoice(v);
    UR_EXPECT_TRUE(p.expired);
    UR_EXPECT_FALSE(p.showQr);
    UR_EXPECT_FALSE(p.showAmount);
    UR_EXPECT_FALSE(p.showAddress);
    UR_EXPECT_FALSE(p.showCountdown);
    UR_EXPECT_TRUE(p.showRefresh && p.refreshEnabled);
    UR_EXPECT_TRUE(p.steps.empty());
    UR_EXPECT_TRUE(p.showPayAnotherWay);
    UR_EXPECT_TRUE(p.notice == cp::InvoiceNotice::None);
    v.refreshing = true;
    p = cp::PresentInvoice(v);
    UR_EXPECT_TRUE(p.showRefresh);
    UR_EXPECT_FALSE(p.refreshEnabled);
  }
  // the status result's quote_expired code is the expired line, not a failure
  cp::InvoiceView v = OpenXmr();
  v.status = "expired";
  v.secondsLeft = 0;
  v.errorCode = "quote_expired";
  UR_EXPECT_TRUE(cp::PresentInvoice(v).notice == cp::InvoiceNotice::None);
}

// Something arrived but not enough: what is left, where to send it, no QR
// for the full amount, and no way to walk away from the money.
UR_TEST(CryptoPay_APartialPaymentAsksForTheRest) {
  cp::InvoiceView v = OpenXmr();
  v.status = "seen";
  v.received = "0.050000";
  v.remaining = "0.022387";
  const cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(p.showRemaining);
  UR_EXPECT_TRUE(p.remainingText == "0.022387 XMR");
  UR_EXPECT_TRUE(p.showAddress);
  UR_EXPECT_FALSE(p.showQr);
  UR_EXPECT_FALSE(p.showAmount);
  UR_EXPECT_FALSE(p.showCountdown);
  UR_EXPECT_FALSE(p.showRefresh);
  UR_EXPECT_FALSE(p.expired);
  UR_EXPECT_TRUE(Kinds(p.steps) == "waiting=done seen=current confirming=upcoming paid=upcoming");
  UR_EXPECT_FALSE(p.showPayAnotherWay);
  // nothing left: no remaining line
  v.remaining = "0.000000";
  UR_EXPECT_FALSE(cp::PresentInvoice(v).showRemaining);
}

// Confirming: the count, and nothing left to do.
UR_TEST(CryptoPay_ConfirmingShowsTheCount) {
  cp::InvoiceView v = OpenXmr();
  v.status = "confirming";
  v.confirmations = 0;
  v.confirmationsRequired = 10;
  const cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(Kinds(p.steps) == "waiting=done seen=done confirming=current paid=upcoming");
  UR_EXPECT_EQ(int64_t{0}, p.confirmations);
  UR_EXPECT_EQ(int64_t{10}, p.confirmationsRequired);
  UR_EXPECT_FALSE(p.showQr || p.showAmount || p.showAddress || p.showRefresh || p.showWallets);
  UR_EXPECT_FALSE(p.showPayAnotherWay);
}

// Review: the review copy and nothing else to do.
UR_TEST(CryptoPay_ReviewShowsOnlyTheReviewCopy) {
  cp::InvoiceView v = OpenXmr();
  v.status = "review";
  v.reviewReason = "underpaid";
  v.errorCode = "underpaid";
  const cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(p.review);
  UR_EXPECT_TRUE(p.steps.empty());
  UR_EXPECT_FALSE(p.showQr || p.showAmount || p.showAddress || p.showCountdown || p.showRefresh ||
                  p.showRemaining || p.showWallets || p.zcashNote || p.expired);
  UR_EXPECT_TRUE(p.notice == cp::InvoiceNotice::None);
  UR_EXPECT_FALSE(p.showPayAnotherWay);
}

// Paid: every step done.
UR_TEST(CryptoPay_PaidIsEveryStepDone) {
  cp::InvoiceView v = OpenXmr();
  v.status = "paid";
  v.phase = "paid";
  const cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(Kinds(p.steps) == "waiting=done seen=done confirming=done paid=done");
  UR_EXPECT_FALSE(p.showQr || p.showAmount || p.showRefresh || p.showPayAnotherWay);
}

// A coin that cannot be quoted now, an invoice the server no longer knows, a
// refused re-quote: the line under the invoice, and a way out.
UR_TEST(CryptoPay_FailuresUnderTheInvoice) {
  cp::InvoiceView v = OpenXmr();
  v.status = "expired";
  v.secondsLeft = 0;
  v.errorCode = "asset_unavailable";
  cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(p.notice == cp::InvoiceNotice::Unavailable);
  UR_EXPECT_TRUE(p.showPayAnotherWay);

  v = OpenXmr();
  v.status = "unknown";
  p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(p.notice == cp::InvoiceNotice::Failed);
  UR_EXPECT_TRUE(p.showPayAnotherWay);
  UR_EXPECT_FALSE(p.showQr || p.showAmount || p.showRefresh);
  UR_EXPECT_TRUE(p.steps.empty());

  v = OpenXmr();
  v.errorCode = "rate_limited";
  p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(p.notice == cp::InvoiceNotice::Failed);
  // the quote it had is still good
  UR_EXPECT_TRUE(p.showQr && p.showAmount);
}

// USDC: the memo with its own Copy, and the network-only warning.
UR_TEST(CryptoPay_UsdcShowsTheMemoAndTheNetworkWarning) {
  cp::InvoiceView v = OpenXmr();
  v.asset = "usdc_solana";
  v.ticker = "USDC";
  v.amount = "40.012345";
  v.memo = "9xQeWvG816bUx9EPjHmaT23yvVM2ZWbrrpZb9PusVFin";
  v.uri = "solana:" + v.address + "?amount=40.012345";
  cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(p.showMemo);
  UR_EXPECT_TRUE(p.usdcWarning);
  UR_EXPECT_TRUE(p.amountText == "40.012345 USDC");
  UR_EXPECT_TRUE(Kinds(p.steps) == "waiting=current paid=upcoming");
  UR_EXPECT_FALSE(p.zcashNote);
  v.memo.clear();
  UR_EXPECT_FALSE(cp::PresentInvoice(v).showMemo);
}

// Zcash: the exchange note while there is something to send.
UR_TEST(CryptoPay_ZcashShowsTheExchangeNote) {
  cp::InvoiceView v = OpenXmr();
  v.asset = "zec";
  v.ticker = "ZEC";
  UR_EXPECT_TRUE(cp::PresentInvoice(v).zcashNote);
  v.status = "confirming";
  UR_EXPECT_FALSE(cp::PresentInvoice(v).zcashNote);
}

// A resumed invoice before its first status poll: money may already be on its
// way, so nothing offers to pay, re-quote or walk away from it until the
// server answers (CONTRACT §6.2.1). Its waiting steps stand in meanwhile.
UR_TEST(CryptoPay_AResumedInvoiceWaitsForItsStatus) {
  cp::InvoiceView v = OpenXmr();
  v.status = "";
  const cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_FALSE(p.showQr || p.showAmount || p.showAddress || p.showMemo || p.showCountdown ||
                  p.showWallets || p.expired);
  UR_EXPECT_FALSE(p.showRefresh);
  UR_EXPECT_FALSE(p.showPayAnotherWay);
  UR_EXPECT_TRUE(Kinds(p.steps) == "waiting=current seen=upcoming confirming=upcoming paid=upcoming");
  // its quote ran out while the app was closed: still nothing until the
  // server says whether anything arrived
  v.secondsLeft = 0;
  const cp::InvoicePresentation stale = cp::PresentInvoice(v);
  UR_EXPECT_FALSE(stale.expired || stale.showQr || stale.showAmount || stale.showRefresh ||
                  stale.showPayAnotherWay);
  // the first answer: nothing arrived and the quote is gone
  v.status = "expired";
  const cp::InvoicePresentation answered = cp::PresentInvoice(v);
  UR_EXPECT_TRUE(answered.expired && answered.showRefresh && answered.showPayAnotherWay);
  UR_EXPECT_FALSE(answered.showQr || answered.showAmount);
}

// No invoice, or one still starting: nothing to show yet.
UR_TEST(CryptoPay_NothingToShowBeforeTheQuote) {
  cp::InvoiceView v;
  v.phase = "starting";
  v.asset = "xmr";
  const cp::InvoicePresentation p = cp::PresentInvoice(v);
  UR_EXPECT_FALSE(p.showQr || p.showAmount || p.showAddress || p.showCountdown || p.showRefresh ||
                  p.expired || p.review || p.showWallets || p.showPayAnotherWay);
  UR_EXPECT_TRUE(p.steps.empty());
  UR_EXPECT_TRUE(p.notice == cp::InvoiceNotice::None);
}

// The controller's directory: one per network server, under the SDK's root.
UR_TEST(CryptoPay_StorageDirIsPerServer) {
  UR_EXPECT_TRUE(cp::StorageDirName("api.bringyour.com") == "api.bringyour.com");
  UR_EXPECT_TRUE(cp::StorageDirName("my.host:8443/x") == "my.host_8443_x");
  UR_EXPECT_TRUE(cp::StorageDirName("") == "default");
  UR_EXPECT_TRUE(cp::StorageDirName("..") == "default");
  UR_EXPECT_TRUE(cp::StorageDirName("../etc") == ".._etc");
  UR_EXPECT_TRUE(cp::StorageDir("/data/urnetwork", "ur.io") == "/data/urnetwork/crypto_invoice/ur.io");
  UR_EXPECT_TRUE(cp::StorageDir("", "ur.io").empty());
}

// The sign-out clears every file the controller wrote, whatever their names,
// and leaves the directory itself (owner-only) for the next sign-in.
UR_TEST(CryptoPay_SignOutWipesTheStorage) {
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / ("urnw-crypto-pay-test-" + std::to_string(::getpid()));
  std::error_code ignored;
  fs::remove_all(root, ignored);
  const std::string dir = cp::StorageDir(root.string(), "ur.io");
  UR_EXPECT_TRUE(cp::EnsureStorageDir(dir));
  const fs::perms perms = fs::status(dir).permissions();
  UR_EXPECT_TRUE((perms & fs::perms::all) == fs::perms::owner_all);
  for (const char* name : {".crypto_invoice", ".payment_method", ".crypto_invoice.tmp-123"}) {
    std::ofstream(fs::path(dir) / name) << "{}";
  }
  fs::create_directories(fs::path(dir) / "kept");
  UR_EXPECT_EQ(size_t{3}, cp::WipeStorage(dir));
  UR_EXPECT_FALSE(fs::exists(fs::path(dir) / ".crypto_invoice"));
  UR_EXPECT_FALSE(fs::exists(fs::path(dir) / ".payment_method"));
  UR_EXPECT_TRUE(fs::is_directory(dir));
  UR_EXPECT_TRUE(fs::is_directory(fs::path(dir) / "kept"));
  UR_EXPECT_EQ(size_t{0}, cp::WipeStorage(dir));
  UR_EXPECT_EQ(size_t{0}, cp::WipeStorage((root / "missing").string()));
  UR_EXPECT_EQ(size_t{0}, cp::WipeStorage(""));
  UR_EXPECT_FALSE(cp::EnsureStorageDir(""));
  fs::remove_all(root, ignored);
}

// A one-time crypto year does not renew, and the Stripe portal never manages it.
UR_TEST(CryptoPay_ACryptoStoreDoesNotRenew) {
  UR_EXPECT_TRUE(cp::StoreDoesNotRenew("crypto"));
  for (const char* family : {"stripe", "apple", "google", "other", ""}) {
    UR_EXPECT_FALSE(cp::StoreDoesNotRenew(family));
  }
}

// The server's URIs encode at level M into a code the canvas draws at whole
// pixels per module, at least 3 of them.
UR_TEST(CryptoPay_PaymentUrisEncodeAtAScannableSize) {
  const std::string xmr = "monero:8" + std::string(94, 'B') +
                          "?tx_amount=0.072387&tx_description=URnetwork%20Pro%20yearly%20(welcome%20offer)";
  const std::string zec = "zcash:u1" + std::string(212, 'q') +
                          "?amount=0.123456&message=URnetwork%20Pro%20yearly%20(welcome%20offer)";
  const std::string usdc =
      "solana:" + std::string(44, 'C') +
      "?amount=40.012345&spl-token=EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v&reference=" +
      std::string(44, 'D') + "&label=URnetwork&message=URnetwork%20Pro%20yearly";
  for (const std::string& uri : {xmr, zec, usdc}) {
    const cp::QrModules code = cp::EncodePaymentQr(uri);
    UR_EXPECT_TRUE_MSG(uri, 21 <= code.count && code.count <= 97);  // versions 1..20
    UR_EXPECT_TRUE_MSG(uri, code.dark.size() ==
                                static_cast<size_t>(code.count) * static_cast<size_t>(code.count));
    const int side = cp::QrCanvasSide(code.count);
    UR_EXPECT_TRUE_MSG(uri, cp::kQrMinSide <= side && side <= cp::kQrMaxSide);
    UR_EXPECT_TRUE_MSG(uri, 3 <= cp::QrModulePixels(side, code.count));
    // the finder pattern's corner is dark: a real code, not an empty grid
    UR_EXPECT_TRUE_MSG(uri, code.Dark(0, 0) && code.Dark(code.count - 1, 0));
  }
  UR_EXPECT_EQ(0, cp::EncodePaymentQr("").count);
}

UR_TEST(CryptoPay_QrSizing) {
  UR_EXPECT_EQ(cp::kQrMinSide, cp::QrCanvasSide(0));
  UR_EXPECT_EQ(cp::kQrMinSide, cp::QrCanvasSide(21));  // 29 modules * 4 = 116 -> floor 200
  UR_EXPECT_EQ(260, cp::QrCanvasSide(57));             // 65 * 4
  UR_EXPECT_EQ(cp::kQrMaxSide, cp::QrCanvasSide(97));  // 105 * 4 = 420 -> cap 320
  UR_EXPECT_EQ(4, cp::QrModulePixels(260, 57));
  UR_EXPECT_EQ(3, cp::QrModulePixels(320, 97));
  UR_EXPECT_EQ(0, cp::QrModulePixels(20, 57));
  UR_EXPECT_EQ(0, cp::QrModulePixels(260, 0));
}
