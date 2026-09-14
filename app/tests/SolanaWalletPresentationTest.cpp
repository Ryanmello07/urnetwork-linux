// The Solana payout wallet's rules: the address shape and its short form, which
// wallet the card shows, the held USDC figure, what the page shows from the
// three legacy reads, and every transition of the connect sheet.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <optional>
#include <string>
#include <vector>

#include "SolanaWalletPresentation.hpp"

using urnw::solana::AddressVerdict;
using urnw::solana::CardFor;
using urnw::solana::CardView;
using urnw::solana::ConnectMachine;
using urnw::solana::ConnectState;
using urnw::solana::FailureKey;
using urnw::solana::FormatUsd;
using urnw::solana::HeldPayment;
using urnw::solana::kTimeoutDetail;
using urnw::solana::LegacyWallet;
using urnw::solana::LooksLikeSolanaAddress;
using urnw::solana::NeedsPayoutSwitch;
using urnw::solana::PayoutWalletFor;
using urnw::solana::PendingUsdcNanoCents;
using urnw::solana::ShortAddress;
using urnw::solana::Supporting;

namespace {

// 44 characters, base58 only (no 0/O/I/l), short form "7Xk9…3fQa".
constexpr const char* kSample = "7Xk9SAMPLEsampeSAMPLEsampeSAMPLEsampeSAM3fQa";

LegacyWallet Wallet(const std::string& id, const std::string& chain, bool active = true,
                    const std::string& circle = std::string()) {
  LegacyWallet wallet;
  wallet.id = id;
  wallet.blockchain = chain;
  wallet.address = std::string(kSample);
  wallet.circleWalletId = circle;
  wallet.active = active;
  return wallet;
}

HeldPayment Payment(int64_t nanoCents, bool completed, bool canceled) {
  HeldPayment payment;
  payment.payoutNanoCents = nanoCents;
  payment.completed = completed;
  payment.canceled = canceled;
  return payment;
}

// A machine that has a checked address in the field.
ConnectMachine CheckedAddress() {
  ConnectMachine m;
  m.Typed();
  m.Check();
  m.Verdict(AddressVerdict::Valid);
  return m;
}

}  // namespace

// ---- the address -----------------------------------------------------------------

UR_TEST(SolanaWallet_AddressShapeIsBase58Of32To44) {
  UR_EXPECT_TRUE(LooksLikeSolanaAddress(kSample));
  UR_EXPECT_EQ(44, static_cast<int>(std::string(kSample).size()));
  // the shortest real key: the system program, 32 ones
  UR_EXPECT_TRUE(LooksLikeSolanaAddress(std::string(32, '1')));
  UR_EXPECT_FALSE(LooksLikeSolanaAddress(std::string(31, '1')));  // too short
  UR_EXPECT_FALSE(LooksLikeSolanaAddress(std::string(45, '1')));  // too long
  UR_EXPECT_FALSE(LooksLikeSolanaAddress(""));
  UR_EXPECT_FALSE(LooksLikeSolanaAddress("   "));
}

UR_TEST(SolanaWallet_AddressRejectsCharactersBase58Lacks) {
  const std::string base(kSample);
  for (const char c : {'0', 'O', 'I', 'l', '-', '_', '+', '/'}) {
    std::string address = base;
    address[10] = c;
    UR_EXPECT_TRUE_MSG(std::string("rejects '") + c + "'", !LooksLikeSolanaAddress(address));
  }
  // an EVM address is not a Solana address
  UR_EXPECT_FALSE(LooksLikeSolanaAddress("0x71C7656EC7ab88b098defB751B7401B5f6d8976F"));
}

UR_TEST(SolanaWallet_AddressIsTrimmedBeforeTheCheck) {
  UR_EXPECT_TRUE(LooksLikeSolanaAddress(std::string("  ") + kSample + "\n"));
  UR_EXPECT_TRUE(LooksLikeSolanaAddress(std::string("\t") + kSample));
  UR_EXPECT_TRUE(urnw::solana::Trim(std::string(" \t") + kSample + " \r\n") == kSample);
  UR_EXPECT_TRUE(urnw::solana::Trim(" \t ").empty());
  // inner whitespace is not trimmed away: it is not an address
  std::string split(kSample);
  split[20] = ' ';
  UR_EXPECT_FALSE(LooksLikeSolanaAddress(split));
}

UR_TEST(SolanaWallet_ShortAddressIsFirstAndLastFour) {
  UR_EXPECT_TRUE(ShortAddress(kSample) == "7Xk9\xE2\x80\xA6" "3fQa");
  UR_EXPECT_TRUE(ShortAddress("123456789") == "123456789");  // no shorter: whole
  UR_EXPECT_TRUE(ShortAddress("1234567890") == "1234\xE2\x80\xA6" "7890");
  UR_EXPECT_TRUE(ShortAddress("").empty());
  // never cuts inside a multi-byte character
  const std::string wide = "\xE2\x80\xA6\xE2\x80\xA6\xE2\x80\xA6\xE2\x80\xA6";
  UR_EXPECT_TRUE(ShortAddress(wide) == wide);
}

// ---- the payout wallet -------------------------------------------------------------

UR_TEST(SolanaWallet_PayoutWalletIsTheConnectedPayoutWallet) {
  const std::vector<LegacyWallet> wallets = {Wallet("w-tao", "TAO"), Wallet("w-sol", "SOL"),
                                             Wallet("w-other", "SOL")};
  const auto payout = PayoutWalletFor(wallets, "w-sol");
  UR_EXPECT_TRUE(payout.has_value());
  UR_EXPECT_TRUE(payout && payout->id == "w-sol");
  UR_EXPECT_TRUE(payout && payout->address == kSample);
  // no payout id: no card, whatever wallets exist
  UR_EXPECT_FALSE(PayoutWalletFor(wallets, "").has_value());
  // an id no listed wallet carries
  UR_EXPECT_FALSE(PayoutWalletFor(wallets, "w-missing").has_value());
  UR_EXPECT_FALSE(PayoutWalletFor({}, "w-sol").has_value());
}

UR_TEST(SolanaWallet_PayoutWalletDropsCircleTaoAndInactive) {
  const std::vector<LegacyWallet> wallets = {
      Wallet("w-tao", "TAO"),
      Wallet("w-circle", "SOL", true, "circle-1"),
      Wallet("w-removed", "SOL", false),
  };
  UR_EXPECT_FALSE(PayoutWalletFor(wallets, "w-tao").has_value());      // never a payout wallet
  UR_EXPECT_FALSE(PayoutWalletFor(wallets, "w-circle").has_value());   // custodial
  UR_EXPECT_FALSE(PayoutWalletFor(wallets, "w-removed").has_value());  // removed
}

// A legacy Polygon payout wallet still receives USDC: it gets the card.
UR_TEST(SolanaWallet_PolygonPayoutWalletIsShown) {
  const auto payout = PayoutWalletFor({Wallet("w-matic", "MATIC")}, "w-matic");
  UR_EXPECT_TRUE(payout.has_value());
}

UR_TEST(SolanaWallet_PayoutSwitchOnlyForAnotherWallet) {
  UR_EXPECT_TRUE(NeedsPayoutSwitch("w-new", "w-old"));
  UR_EXPECT_TRUE(NeedsPayoutSwitch("w-new", ""));  // the network had none
  UR_EXPECT_FALSE(NeedsPayoutSwitch("w-same", "w-same"));
  UR_EXPECT_FALSE(NeedsPayoutSwitch("", "w-old"));  // nothing was created
}

// ---- the held USDC ---------------------------------------------------------------

UR_TEST(SolanaWallet_PendingIsEveryHeldPayment) {
  const std::vector<HeldPayment> payments = {
      Payment(2'000'000'000, false, false),   // held
      Payment(1'870'000'000, false, false),   // held
      Payment(9'000'000'000, true, false),    // paid
      Payment(5'000'000'000, false, true),    // canceled
      Payment(7'000'000'000, true, true),     // both
  };
  UR_EXPECT_EQ(3'870'000'000LL, PendingUsdcNanoCents(payments));
  UR_EXPECT_EQ(0, PendingUsdcNanoCents({}));
}

// The scale is the SDK's: 1 USD = 1e9 nano cents.
UR_TEST(SolanaWallet_FormatUsdIsTwoDecimals) {
  UR_EXPECT_TRUE(FormatUsd(3'870'000'000LL) == "3.87");
  UR_EXPECT_TRUE(FormatUsd(0) == "0.00");
  UR_EXPECT_TRUE(FormatUsd(1'000'000'000LL) == "1.00");
  UR_EXPECT_TRUE(FormatUsd(50'000'000LL) == "0.05");
  UR_EXPECT_TRUE(FormatUsd(1'234'567'890'123LL) == "1234.57");
  // half a cent rounds up, just under it rounds down
  UR_EXPECT_TRUE(FormatUsd(5'000'000LL) == "0.01");
  UR_EXPECT_TRUE(FormatUsd(4'999'999LL) == "0.00");
  UR_EXPECT_TRUE(FormatUsd(-3'870'000'000LL) == "-3.87");
}

// ---- the page's reading ------------------------------------------------------------

UR_TEST(SolanaWallet_NothingShowsUntilTheReadsAreIn) {
  const auto payout = PayoutWalletFor({Wallet("w-sol", "SOL")}, "w-sol");
  const CardView loading = CardFor(false, payout, 3'870'000'000LL);
  UR_EXPECT_FALSE(loading.showCard);
  UR_EXPECT_FALSE(loading.showPending);
  UR_EXPECT_FALSE(loading.showWaitingLine);
  const CardView noWallet = CardFor(false, std::nullopt, 3'870'000'000LL);
  UR_EXPECT_FALSE(noWallet.showWaitingLine);
}

UR_TEST(SolanaWallet_CardShowsTheWalletAndWhatIsWaiting) {
  const auto payout = PayoutWalletFor({Wallet("w-sol", "SOL")}, "w-sol");
  const CardView view = CardFor(true, payout, 3'870'000'000LL);
  UR_EXPECT_TRUE(view.showCard);
  UR_EXPECT_TRUE(view.showPending);
  UR_EXPECT_FALSE(view.showWaitingLine);  // the card carries the figure itself
  UR_EXPECT_TRUE(view.address == kSample);
  UR_EXPECT_TRUE(view.shortAddress == "7Xk9\xE2\x80\xA6" "3fQa");
  UR_EXPECT_TRUE(view.pendingUsd == "3.87");
}

UR_TEST(SolanaWallet_CardHidesThePendingLineAtZero) {
  const auto payout = PayoutWalletFor({Wallet("w-sol", "SOL")}, "w-sol");
  const CardView zero = CardFor(true, payout, 0);
  UR_EXPECT_TRUE(zero.showCard);
  UR_EXPECT_FALSE(zero.showPending);
  UR_EXPECT_TRUE(zero.pendingUsd.empty());
  // a figure that rounds to 0.00 is no figure
  const CardView dust = CardFor(true, payout, 4'999'999LL);
  UR_EXPECT_FALSE(dust.showPending);
}

// The emailed user: no payout wallet, payouts held.
UR_TEST(SolanaWallet_WaitingLineWithoutAWallet) {
  const CardView view = CardFor(true, std::nullopt, 3'870'000'000LL);
  UR_EXPECT_FALSE(view.showCard);
  UR_EXPECT_TRUE(view.showWaitingLine);
  UR_EXPECT_TRUE(view.pendingUsd == "3.87");
  const CardView none = CardFor(true, std::nullopt, 0);
  UR_EXPECT_FALSE(none.showCard);
  UR_EXPECT_FALSE(none.showWaitingLine);
}

// ---- the connect sheet: the bridge -------------------------------------------------

UR_TEST(SolanaWallet_ProviderOpensTheBrowserThenLinksTheKey) {
  ConnectMachine m;
  UR_EXPECT_TRUE(m.state == ConnectState::Idle);
  UR_EXPECT_TRUE(m.AcceptsInput());
  UR_EXPECT_TRUE(m.ChooseProvider());
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  UR_EXPECT_TRUE(m.Busy());
  UR_EXPECT_FALSE(m.AcceptsInput());
  UR_EXPECT_FALSE(m.ConnectAllowed());
  // the key goes straight to linking: it is not checked client side
  UR_EXPECT_TRUE(m.PublicKey());
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);
  UR_EXPECT_TRUE(m.Busy());
  UR_EXPECT_TRUE(m.CreateResult(true, "w-new", ""));
  UR_EXPECT_TRUE(m.state == ConnectState::Linked);
  UR_EXPECT_TRUE(m.walletId == "w-new");
  UR_EXPECT_FALSE(m.Busy());
}

// One bridge round trip at a time: a second press while one is out, or while
// the key is being linked, starts nothing.
UR_TEST(SolanaWallet_NoSecondRoundTripWhileOneIsOut) {
  ConnectMachine m;
  m.ChooseProvider();
  UR_EXPECT_FALSE(m.ChooseProvider());
  UR_EXPECT_FALSE(m.Typed());
  UR_EXPECT_FALSE(m.Submit());
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  m.PublicKey();
  UR_EXPECT_FALSE(m.ChooseProvider());
  UR_EXPECT_FALSE(m.Typed());
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);
}

UR_TEST(SolanaWallet_BridgeErrorFailsWithItsWords) {
  ConnectMachine m;
  m.ChooseProvider();
  UR_EXPECT_TRUE(m.BridgeError("User rejected the request."));
  UR_EXPECT_TRUE(m.state == ConnectState::Failed);
  UR_EXPECT_TRUE(m.detail == "User rejected the request.");
  UR_EXPECT_TRUE(std::string(FailureKey(m.detail)) == "error_connecting_wallet_with_reason");
  UR_EXPECT_TRUE(m.AcceptsInput());  // the controls come back
}

UR_TEST(SolanaWallet_BridgeTimeoutIsWalletConnectFailed) {
  ConnectMachine m;
  m.ChooseProvider();
  UR_EXPECT_TRUE(m.Timeout());
  UR_EXPECT_TRUE(m.state == ConnectState::Failed);
  UR_EXPECT_TRUE(std::string(FailureKey(m.detail)) == "wallet_connect_failed");
}

UR_TEST(SolanaWallet_LinkingTimeoutIsWalletConnectFailed) {
  ConnectMachine m;
  m.ChooseProvider();
  m.PublicKey();
  UR_EXPECT_TRUE(m.Timeout());
  UR_EXPECT_TRUE(m.state == ConnectState::Failed);
  UR_EXPECT_TRUE(std::string(FailureKey(m.detail)) == "wallet_connect_failed");
}

// The user moved on (or never asked): a late answer changes nothing.
UR_TEST(SolanaWallet_LateBridgeAnswersAreIgnored) {
  ConnectMachine idle;
  UR_EXPECT_FALSE(idle.PublicKey());
  UR_EXPECT_FALSE(idle.BridgeError("late"));
  UR_EXPECT_FALSE(idle.Timeout());
  UR_EXPECT_FALSE(idle.CreateResult(true, "w-late", ""));
  UR_EXPECT_TRUE(idle.state == ConnectState::Idle);
  UR_EXPECT_TRUE(idle.detail.empty());

  ConnectMachine failed;
  failed.ChooseProvider();
  failed.Timeout();
  UR_EXPECT_FALSE(failed.PublicKey());  // the give-up is final
  UR_EXPECT_TRUE(failed.state == ConnectState::Failed);
}

// ---- the connect sheet: the manual address -----------------------------------------

UR_TEST(SolanaWallet_ManualCheckedAddressLinks) {
  ConnectMachine m;
  UR_EXPECT_TRUE(m.Typed());
  UR_EXPECT_FALSE(m.ConnectAllowed());  // nothing checked yet
  UR_EXPECT_TRUE(m.Check());
  UR_EXPECT_TRUE(m.state == ConnectState::Checking);
  UR_EXPECT_TRUE(m.supporting == Supporting::Checking);
  UR_EXPECT_FALSE(m.ConnectAllowed());
  UR_EXPECT_TRUE(m.Verdict(AddressVerdict::Valid));
  UR_EXPECT_TRUE(m.state == ConnectState::Ready);
  UR_EXPECT_TRUE(m.supporting == Supporting::None);  // the line clears
  UR_EXPECT_TRUE(m.ConnectAllowed());
  UR_EXPECT_TRUE(m.Submit());
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);
  UR_EXPECT_FALSE(m.AcceptsInput());  // field, providers and Connect are off
  UR_EXPECT_TRUE(m.CreateResult(true, "w-typed", ""));
  UR_EXPECT_TRUE(m.state == ConnectState::Linked);
}

UR_TEST(SolanaWallet_ManualVerdicts) {
  ConnectMachine invalid;
  invalid.Typed();
  invalid.Check();
  UR_EXPECT_TRUE(invalid.Verdict(AddressVerdict::Invalid));
  UR_EXPECT_TRUE(invalid.state == ConnectState::Idle);
  UR_EXPECT_TRUE(invalid.supporting == Supporting::Invalid);
  UR_EXPECT_FALSE(invalid.ConnectAllowed());

  ConnectMachine unavailable;
  unavailable.Typed();
  unavailable.Check();
  UR_EXPECT_TRUE(unavailable.Verdict(AddressVerdict::Unavailable));
  UR_EXPECT_TRUE(unavailable.state == ConnectState::Idle);
  UR_EXPECT_TRUE(unavailable.supporting == Supporting::Unavailable);
  UR_EXPECT_FALSE(unavailable.ConnectAllowed());
}

// A malformed address is said at once and no check goes out.
UR_TEST(SolanaWallet_MalformedAddressSendsNothing) {
  ConnectMachine m;
  m.Typed();
  UR_EXPECT_TRUE(m.Malformed());
  UR_EXPECT_TRUE(m.state == ConnectState::Idle);
  UR_EXPECT_TRUE(m.supporting == Supporting::Invalid);
  UR_EXPECT_FALSE(m.ConnectAllowed());
}

// Typing forgets the verdict, and an answer for the old text is dropped.
UR_TEST(SolanaWallet_TypingResetsTheVerdict) {
  ConnectMachine ready = CheckedAddress();
  UR_EXPECT_TRUE(ready.Typed());
  UR_EXPECT_TRUE(ready.state == ConnectState::Idle);
  UR_EXPECT_FALSE(ready.addressChecked);
  UR_EXPECT_FALSE(ready.ConnectAllowed());

  ConnectMachine checking;
  checking.Typed();
  checking.Check();
  checking.Typed();
  UR_EXPECT_FALSE(checking.Verdict(AddressVerdict::Valid));
  UR_EXPECT_TRUE(checking.state == ConnectState::Idle);
  UR_EXPECT_TRUE(checking.supporting == Supporting::None);
}

// A provider pressed while a check is out drops the check's answer.
UR_TEST(SolanaWallet_ProviderSupersedesACheck) {
  ConnectMachine m;
  m.Typed();
  m.Check();
  UR_EXPECT_TRUE(m.ChooseProvider());
  UR_EXPECT_TRUE(m.supporting == Supporting::None);
  UR_EXPECT_FALSE(m.Verdict(AddressVerdict::Valid));
  UR_EXPECT_FALSE(m.Malformed());
  UR_EXPECT_FALSE(m.Check());
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
}

// ---- the connect sheet: failure and retry -------------------------------------------

UR_TEST(SolanaWallet_CreateFailures) {
  ConnectMachine refused = CheckedAddress();
  refused.Submit();
  UR_EXPECT_TRUE(refused.CreateResult(false, "", "Invalid wallet address."));
  UR_EXPECT_TRUE(refused.state == ConnectState::Failed);
  UR_EXPECT_TRUE(std::string(FailureKey(refused.detail)) == "error_connecting_wallet_with_reason");

  // a result without a wallet id is not a success
  ConnectMachine noId = CheckedAddress();
  noId.Submit();
  UR_EXPECT_TRUE(noId.CreateResult(true, "", ""));
  UR_EXPECT_TRUE(noId.state == ConnectState::Failed);
  UR_EXPECT_TRUE(noId.walletId.empty());
  UR_EXPECT_TRUE(std::string(FailureKey(noId.detail)) == "something_went_wrong");
}

// Any new attempt clears the failure; the checked address may be sent again.
UR_TEST(SolanaWallet_AnyNewAttemptClearsTheFailure) {
  ConnectMachine retry = CheckedAddress();
  retry.Submit();
  retry.CreateResult(false, "", "network down");
  UR_EXPECT_TRUE(retry.ConnectAllowed());
  UR_EXPECT_TRUE(retry.Submit());
  UR_EXPECT_TRUE(retry.state == ConnectState::Linking);
  UR_EXPECT_TRUE(retry.detail.empty());

  ConnectMachine provider;
  provider.ChooseProvider();
  provider.BridgeError("closed");
  UR_EXPECT_FALSE(provider.ConnectAllowed());  // nothing typed was checked
  UR_EXPECT_TRUE(provider.ChooseProvider());
  UR_EXPECT_TRUE(provider.detail.empty());

  ConnectMachine typed;
  typed.ChooseProvider();
  typed.BridgeError("closed");
  UR_EXPECT_TRUE(typed.Typed());
  UR_EXPECT_TRUE(typed.state == ConnectState::Idle);
  UR_EXPECT_TRUE(typed.detail.empty());
}

UR_TEST(SolanaWallet_LinkedIsFinal) {
  ConnectMachine m;
  m.ChooseProvider();
  m.PublicKey();
  m.CreateResult(true, "w-new", "");
  UR_EXPECT_FALSE(m.ChooseProvider());
  UR_EXPECT_FALSE(m.Typed());
  UR_EXPECT_FALSE(m.Submit());
  UR_EXPECT_FALSE(m.Timeout());
  UR_EXPECT_FALSE(m.CreateResult(false, "", "late"));
  UR_EXPECT_TRUE(m.state == ConnectState::Linked);
  UR_EXPECT_TRUE(m.walletId == "w-new");
}

UR_TEST(SolanaWallet_FailureKeys) {
  UR_EXPECT_TRUE(std::string(FailureKey(kTimeoutDetail)) == "wallet_connect_failed");
  UR_EXPECT_TRUE(std::string(FailureKey("superseded")) == "error_connecting_wallet_with_reason");
  UR_EXPECT_TRUE(std::string(FailureKey("")) == "something_went_wrong");
}
