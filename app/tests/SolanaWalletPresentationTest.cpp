// The Solana payout wallet's rules: the address shape and its short form, which
// wallet the card shows, the held USDC figure, what the page shows from the
// three legacy reads, and every transition of the connect sheet.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <optional>
#include <string>
#include <vector>

#include "SolanaWalletPresentation.hpp"

using urnw::solana::AddressCheck;
using urnw::solana::AddressVerdict;
using urnw::solana::CardFor;
using urnw::solana::CardView;
using urnw::solana::ConnectMachine;
using urnw::solana::ConnectState;
using urnw::solana::FailureKey;
using urnw::solana::FormatUsd;
using urnw::solana::HeldPayment;
using urnw::solana::LegacyWallet;
using urnw::solana::LooksLikeSolanaAddress;
using urnw::solana::NeedsPayoutSwitch;
using urnw::solana::PayoutWalletFor;
using urnw::solana::PendingUsdcNanoCents;
using urnw::solana::ShortAddress;

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

// A machine whose field holds an address the server accepted.
ConnectMachine CheckedAddress() {
  ConnectMachine m;
  m.Typed();
  m.Check(kSample);
  m.Verdict(kSample, AddressVerdict::Valid);
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
  UR_EXPECT_TRUE(m.ProvidersEnabled());
  const uint64_t round = m.ChooseProvider();
  UR_EXPECT_TRUE(round != 0);
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  UR_EXPECT_FALSE(m.ProvidersEnabled());
  UR_EXPECT_TRUE(m.EntryEnabled());  // the manual entry stays live while the browser is out
  UR_EXPECT_FALSE(m.ConnectAllowed());
  // the key goes straight to linking: it is not checked client side
  UR_EXPECT_TRUE(m.PublicKey(round));
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);
  UR_EXPECT_FALSE(m.EntryEnabled());
  UR_EXPECT_TRUE(m.CreateResult(round, true, "w-new", ""));
  UR_EXPECT_TRUE(m.state == ConnectState::Linked);
  UR_EXPECT_TRUE(m.walletId == "w-new");
  UR_EXPECT_FALSE(m.ProvidersEnabled());
  UR_EXPECT_FALSE(m.EntryEnabled());
}

// One round trip at a time: a second press while one is out, or while a wallet
// is being linked, starts nothing.
UR_TEST(SolanaWallet_NoSecondRoundTripWhileOneIsOut) {
  ConnectMachine m;
  const uint64_t round = m.ChooseProvider();
  UR_EXPECT_EQ(0, m.ChooseProvider());
  UR_EXPECT_EQ(0, m.Submit());
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  m.PublicKey(round);
  UR_EXPECT_EQ(0, m.ChooseProvider());
  UR_EXPECT_FALSE(m.Typed());  // the entry is off while linking
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);
}

UR_TEST(SolanaWallet_BridgeErrorFailsWithItsWords) {
  ConnectMachine m;
  const uint64_t round = m.ChooseProvider();
  UR_EXPECT_TRUE(m.BridgeError(round, "User rejected the request."));
  UR_EXPECT_TRUE(m.state == ConnectState::Failed);
  UR_EXPECT_TRUE(m.detail == "User rejected the request.");
  UR_EXPECT_TRUE(std::string(FailureKey(m.detail)) == "error_connecting_wallet_with_reason");
  UR_EXPECT_TRUE(m.ProvidersEnabled());  // the controls come back
  UR_EXPECT_TRUE(m.EntryEnabled());
}

// A give-up has no words: it reads something_went_wrong, like any failure
// without a detail (the cross-app wording rule).
UR_TEST(SolanaWallet_BridgeTimeoutIsSomethingWentWrong) {
  ConnectMachine m;
  const uint64_t round = m.ChooseProvider();
  UR_EXPECT_TRUE(m.Timeout(round));
  UR_EXPECT_TRUE(m.state == ConnectState::Failed);
  UR_EXPECT_TRUE(m.detail.empty());
  UR_EXPECT_TRUE(std::string(FailureKey(m.detail)) == "something_went_wrong");
}

UR_TEST(SolanaWallet_LinkingTimeoutIsSomethingWentWrong) {
  ConnectMachine m;
  const uint64_t round = m.ChooseProvider();
  m.PublicKey(round);
  UR_EXPECT_TRUE(m.Timeout(round));
  UR_EXPECT_TRUE(m.state == ConnectState::Failed);
  UR_EXPECT_TRUE(m.detail.empty());
  UR_EXPECT_TRUE(std::string(FailureKey(m.detail)) == "something_went_wrong");
}

// The user moved on (or never asked): a late answer changes nothing.
UR_TEST(SolanaWallet_LateBridgeAnswersAreIgnored) {
  ConnectMachine idle;
  UR_EXPECT_FALSE(idle.PublicKey(1));
  UR_EXPECT_FALSE(idle.BridgeError(1, "late"));
  UR_EXPECT_FALSE(idle.Timeout(1));
  UR_EXPECT_FALSE(idle.CreateResult(1, true, "w-late", ""));
  UR_EXPECT_TRUE(idle.state == ConnectState::Idle);
  UR_EXPECT_TRUE(idle.detail.empty());

  ConnectMachine failed;
  const uint64_t round = failed.ChooseProvider();
  failed.Timeout(round);
  UR_EXPECT_FALSE(failed.PublicKey(round));  // the give-up is final
  UR_EXPECT_FALSE(failed.BridgeError(round, "late"));
  UR_EXPECT_TRUE(failed.state == ConnectState::Failed);
}

// The first of two presses was given up on, and the host answers it
// "superseded by a wallet connect request" when the second starts: that answer
// must not fail the second round trip, nor may a stale create answer link.
UR_TEST(SolanaWallet_SupersededAnswerNeverLands) {
  ConnectMachine m;
  const uint64_t first = m.ChooseProvider();
  UR_EXPECT_TRUE(m.Timeout(first));
  const uint64_t second = m.ChooseProvider();
  UR_EXPECT_TRUE(second != 0 && second != first);
  UR_EXPECT_FALSE(m.BridgeError(first, "superseded by a wallet connect request"));
  UR_EXPECT_FALSE(m.PublicKey(first));
  UR_EXPECT_FALSE(m.Timeout(first));
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  UR_EXPECT_TRUE(m.detail.empty());
  UR_EXPECT_TRUE(m.PublicKey(second));
  UR_EXPECT_FALSE(m.CreateResult(first, true, "w-stale", ""));
  UR_EXPECT_TRUE(m.CreateResult(second, true, "w-new", ""));
  UR_EXPECT_TRUE(m.walletId == "w-new");
}

// ---- the connect sheet: the manual address -----------------------------------------

UR_TEST(SolanaWallet_ManualCheckedAddressLinks) {
  ConnectMachine m;
  UR_EXPECT_TRUE(m.Typed());
  UR_EXPECT_FALSE(m.ConnectAllowed());  // nothing checked yet
  UR_EXPECT_TRUE(m.Check(kSample));
  UR_EXPECT_TRUE(m.state == ConnectState::Checking);
  UR_EXPECT_TRUE(m.check == AddressCheck::Checking);
  UR_EXPECT_FALSE(m.ConnectAllowed());
  UR_EXPECT_TRUE(m.Verdict(kSample, AddressVerdict::Valid));
  UR_EXPECT_TRUE(m.state == ConnectState::Ready);
  UR_EXPECT_TRUE(m.check == AddressCheck::Valid);
  UR_EXPECT_TRUE(m.ConnectAllowed());
  const uint64_t round = m.Submit();
  UR_EXPECT_TRUE(round != 0);
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);
  UR_EXPECT_FALSE(m.EntryEnabled());  // field, providers and Connect are off
  UR_EXPECT_FALSE(m.ProvidersEnabled());
  UR_EXPECT_FALSE(m.ConnectAllowed());
  UR_EXPECT_TRUE(m.address == kSample);  // the accepted address is the one being linked
  UR_EXPECT_TRUE(m.CreateResult(round, true, "w-typed", ""));
  UR_EXPECT_TRUE(m.state == ConnectState::Linked);
}

UR_TEST(SolanaWallet_ManualVerdicts) {
  ConnectMachine invalid;
  invalid.Typed();
  invalid.Check(kSample);
  UR_EXPECT_TRUE(invalid.Verdict(kSample, AddressVerdict::Invalid));
  UR_EXPECT_TRUE(invalid.state == ConnectState::Idle);
  UR_EXPECT_TRUE(invalid.check == AddressCheck::Invalid);
  UR_EXPECT_FALSE(invalid.ConnectAllowed());

  ConnectMachine unavailable;
  unavailable.Typed();
  unavailable.Check(kSample);
  UR_EXPECT_TRUE(unavailable.Verdict(kSample, AddressVerdict::Unavailable));
  UR_EXPECT_TRUE(unavailable.state == ConnectState::Idle);
  UR_EXPECT_TRUE(unavailable.check == AddressCheck::Unavailable);
  UR_EXPECT_FALSE(unavailable.ConnectAllowed());
}

// A malformed address is said at once and no check goes out.
UR_TEST(SolanaWallet_MalformedAddressSendsNothing) {
  ConnectMachine m;
  m.Typed();
  UR_EXPECT_TRUE(m.Malformed("7Xk9"));
  UR_EXPECT_TRUE(m.state == ConnectState::Idle);
  UR_EXPECT_TRUE(m.check == AddressCheck::Invalid);
  UR_EXPECT_FALSE(m.ConnectAllowed());
  UR_EXPECT_FALSE(m.Verdict("7Xk9", AddressVerdict::Valid));  // nothing went out to answer
}

// Typing forgets the verdict, and an answer for the old text is dropped.
UR_TEST(SolanaWallet_TypingResetsTheVerdict) {
  ConnectMachine ready = CheckedAddress();
  UR_EXPECT_TRUE(ready.Typed());
  UR_EXPECT_TRUE(ready.state == ConnectState::Idle);
  UR_EXPECT_TRUE(ready.check == AddressCheck::None);
  UR_EXPECT_FALSE(ready.ConnectAllowed());

  ConnectMachine checking;
  checking.Typed();
  checking.Check(kSample);
  checking.Typed();
  UR_EXPECT_FALSE(checking.Verdict(kSample, AddressVerdict::Valid));
  UR_EXPECT_TRUE(checking.state == ConnectState::Idle);
  UR_EXPECT_TRUE(checking.check == AddressCheck::None);
}

// The text changed and was checked again while the first check was out: only
// the verdict for the text in the field lands.
UR_TEST(SolanaWallet_VerdictForOtherTextIsDropped) {
  const std::string other(32, '1');
  ConnectMachine m;
  m.Typed();
  m.Check(kSample);
  m.Typed();
  m.Check(other);
  UR_EXPECT_FALSE(m.Verdict(kSample, AddressVerdict::Valid));
  UR_EXPECT_TRUE(m.check == AddressCheck::Checking);
  UR_EXPECT_FALSE(m.ConnectAllowed());
  UR_EXPECT_TRUE(m.Verdict(other, AddressVerdict::Invalid));
  UR_EXPECT_TRUE(m.check == AddressCheck::Invalid);
}

// ---- the connect sheet: a round trip and a typed address side by side --------------

// The manual entry stays live while the browser is out: the address is checked
// without ending that round trip, Connect waits for it to end, and a failed
// round trip leaves the accepted address ready to send.
UR_TEST(SolanaWallet_TypingWhileTheBrowserIsOutKeepsThatRoundTrip) {
  ConnectMachine m;
  const uint64_t round = m.ChooseProvider();
  UR_EXPECT_TRUE(m.Typed());
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  UR_EXPECT_TRUE(m.Check(kSample));
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  UR_EXPECT_TRUE(m.Verdict(kSample, AddressVerdict::Valid));
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
  UR_EXPECT_TRUE(m.check == AddressCheck::Valid);
  UR_EXPECT_FALSE(m.ConnectAllowed());  // Connect waits for the round trip
  UR_EXPECT_EQ(0, m.Submit());
  UR_EXPECT_TRUE(m.BridgeError(round, "User rejected the request."));
  UR_EXPECT_TRUE(m.state == ConnectState::Failed);
  UR_EXPECT_TRUE(m.ConnectAllowed());
}

// A provider pressed while a check is out keeps that check and its verdict.
UR_TEST(SolanaWallet_AProviderKeepsTheCheckInFlight) {
  ConnectMachine m;
  m.Typed();
  m.Check(kSample);
  const uint64_t round = m.ChooseProvider();
  UR_EXPECT_TRUE(round != 0);
  UR_EXPECT_TRUE(m.check == AddressCheck::Checking);
  UR_EXPECT_TRUE(m.Verdict(kSample, AddressVerdict::Valid));
  UR_EXPECT_TRUE(m.check == AddressCheck::Valid);
  UR_EXPECT_TRUE(m.state == ConnectState::OpeningBrowser);
}

// The wallet app's key and the typed address never write over each other: the
// key links without touching the field's address or verdict, a verdict arriving
// while the key is linked leaves the link alone, and a typed address being
// linked takes no key.
UR_TEST(SolanaWallet_ABridgeKeyAndATypedAddressStaySeparate) {
  ConnectMachine m;
  m.Typed();
  m.Check(kSample);
  const uint64_t round = m.ChooseProvider();
  UR_EXPECT_TRUE(m.PublicKey(round));
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);
  UR_EXPECT_TRUE(m.address == kSample);
  UR_EXPECT_TRUE(m.Verdict(kSample, AddressVerdict::Invalid));
  UR_EXPECT_TRUE(m.state == ConnectState::Linking);  // the link goes on
  UR_EXPECT_FALSE(m.Typed());                       // and the field is off meanwhile
  UR_EXPECT_TRUE(m.CreateResult(round, true, "w-app", ""));
  UR_EXPECT_TRUE(m.state == ConnectState::Linked);

  ConnectMachine typed = CheckedAddress();
  const uint64_t link = typed.Submit();
  UR_EXPECT_TRUE(link != 0);
  UR_EXPECT_FALSE(typed.PublicKey(link));  // no browser round trip is out
  UR_EXPECT_EQ(0, typed.ChooseProvider());
  UR_EXPECT_TRUE(typed.state == ConnectState::Linking);
  UR_EXPECT_TRUE(typed.CreateResult(link, true, "w-typed", ""));
}

// ---- the connect sheet: failure and retry -------------------------------------------

UR_TEST(SolanaWallet_CreateFailures) {
  ConnectMachine refused = CheckedAddress();
  const uint64_t round = refused.Submit();
  UR_EXPECT_TRUE(refused.CreateResult(round, false, "", "Invalid wallet address."));
  UR_EXPECT_TRUE(refused.state == ConnectState::Failed);
  UR_EXPECT_TRUE(std::string(FailureKey(refused.detail)) == "error_connecting_wallet_with_reason");

  // a result without a wallet id is not a success
  ConnectMachine noId = CheckedAddress();
  const uint64_t link = noId.Submit();
  UR_EXPECT_TRUE(noId.CreateResult(link, true, "", ""));
  UR_EXPECT_TRUE(noId.state == ConnectState::Failed);
  UR_EXPECT_TRUE(noId.walletId.empty());
  UR_EXPECT_TRUE(std::string(FailureKey(noId.detail)) == "something_went_wrong");
}

// Any new attempt clears the failure; the accepted address may be sent again.
UR_TEST(SolanaWallet_AnyNewAttemptClearsTheFailure) {
  ConnectMachine retry = CheckedAddress();
  const uint64_t first = retry.Submit();
  retry.CreateResult(first, false, "", "network down");
  UR_EXPECT_TRUE(retry.ConnectAllowed());
  const uint64_t second = retry.Submit();
  UR_EXPECT_TRUE(second != 0 && second != first);
  UR_EXPECT_TRUE(retry.state == ConnectState::Linking);
  UR_EXPECT_TRUE(retry.detail.empty());

  ConnectMachine provider;
  const uint64_t trip = provider.ChooseProvider();
  provider.BridgeError(trip, "closed");
  UR_EXPECT_FALSE(provider.ConnectAllowed());  // nothing typed was accepted
  UR_EXPECT_TRUE(provider.ChooseProvider() != 0);
  UR_EXPECT_TRUE(provider.detail.empty());

  // a new address is a new attempt once it is debounced
  ConnectMachine typed;
  const uint64_t again = typed.ChooseProvider();
  typed.BridgeError(again, "closed");
  UR_EXPECT_TRUE(typed.Typed());
  UR_EXPECT_TRUE(typed.state == ConnectState::Failed);
  UR_EXPECT_TRUE(typed.Check(kSample));
  UR_EXPECT_TRUE(typed.state == ConnectState::Checking);
  UR_EXPECT_TRUE(typed.detail.empty());
}

UR_TEST(SolanaWallet_LinkedIsFinal) {
  ConnectMachine m;
  const uint64_t round = m.ChooseProvider();
  m.PublicKey(round);
  m.CreateResult(round, true, "w-new", "");
  UR_EXPECT_EQ(0, m.ChooseProvider());
  UR_EXPECT_FALSE(m.Typed());
  UR_EXPECT_EQ(0, m.Submit());
  UR_EXPECT_FALSE(m.Timeout(round));
  UR_EXPECT_FALSE(m.CreateResult(round, false, "", "late"));
  UR_EXPECT_TRUE(m.state == ConnectState::Linked);
  UR_EXPECT_TRUE(m.walletId == "w-new");
}

UR_TEST(SolanaWallet_AFailureWithWordsShowsThem) {
  UR_EXPECT_TRUE(std::string(FailureKey("Invalid wallet address.")) ==
                 "error_connecting_wallet_with_reason");
  UR_EXPECT_TRUE(std::string(FailureKey("superseded by a wallet connect request")) ==
                 "error_connecting_wallet_with_reason");
}

UR_TEST(SolanaWallet_AFailureWithoutWordsIsSomethingWentWrong) {
  UR_EXPECT_TRUE(std::string(FailureKey("")) == "something_went_wrong");
}
