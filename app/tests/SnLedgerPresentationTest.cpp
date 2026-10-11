// The Settlement v2 tile and its claim sheet on the Earnings page
// (SnLedgerPresentation.hpp): which tile shows in every state of the read
// (loading, unavailable, no account, accounts), with v2 claims configured or
// not and with or without a coldkey; its figures (Claimable, Paid, Earned and
// what a pending root adds); and the claim sheet's rows through a claim (sent,
// claimed with the vault's amount, needs gas, already claimed, another
// failure, a failure about no account, Done).
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "SnLedgerPresentation.hpp"

using urnw::snledger::Account;
using urnw::snledger::AccountView;
using urnw::snledger::ClaimProgress;
using urnw::snledger::ClaimRow;
using urnw::snledger::Failure;
using urnw::snledger::FailureFor;
using urnw::snledger::Load;
using urnw::snledger::Phase;
using urnw::snledger::SectionFor;
using urnw::snledger::SectionView;
using urnw::snledger::Status;

namespace {

// names each figure by the rao it was formatted from
std::string Tag(int64_t rao) { return "a" + std::to_string(rao); }

Account LedgerAccount(int64_t noId, int64_t index, int64_t finalizedRao, int64_t paidRao,
                      bool pending = false, int64_t pendingRao = 0) {
  Account account;
  account.vault = "0x5a3e0000000000000000000000000000000000a2";
  account.noId = noId;
  account.index = index;
  account.finalizedRao = finalizedRao;
  account.paidRao = paidRao;
  account.claimableRao = finalizedRao - paidRao;
  account.pending = pending;
  account.pendingRao = pendingRao;
  return account;
}

// one pool account: 5.12 α finalized, 1.879 α of it paid
Account OnePool() { return LedgerAccount(1, 42, 5'120'000'000, 1'879'000'000); }

SectionView Tile(bool hasColdkey, Load load, const std::vector<Account>& accounts,
                 bool configured, bool claiming = false) {
  return SectionFor(hasColdkey, load, accounts, configured, claiming, Tag);
}

// CL-2: the accounts are a coldkey's; with none nothing shows, whatever the read
UR_TEST(SnLedgerNothingShowsWithoutAColdkey) {
  for (const Load load : {Load::Loading, Load::Ready, Load::Unavailable}) {
    for (const bool configured : {false, true}) {
      UR_EXPECT_TRUE(Tile(false, load, {}, configured) == SectionView{});
      UR_EXPECT_TRUE(Tile(false, load, {OnePool()}, configured) == SectionView{});
    }
  }
}

// CL-2: before the release configures v2 claims, only balances the ledger
// returned show, and never a Claim
UR_TEST(SnLedgerBeforeActivationOnlyReturnedBalancesShow) {
  UR_EXPECT_TRUE(Tile(true, Load::Loading, {}, false) == SectionView{});
  UR_EXPECT_TRUE(Tile(true, Load::Unavailable, {}, false) == SectionView{});
  UR_EXPECT_TRUE(Tile(true, Load::Ready, {}, false) == SectionView{});
  const SectionView view = Tile(true, Load::Ready, {OnePool()}, false);
  UR_EXPECT_TRUE(view.visible);
  UR_EXPECT_TRUE(view.status == Status::None);
  UR_EXPECT_EQ(1u, view.accounts.size());
  UR_EXPECT_FALSE(view.showClaim);
  UR_EXPECT_FALSE(view.claimEnabled);
}

// CL-2: configured, the tile shows while loading, with Claim off
UR_TEST(SnLedgerLoadingSaysSoWithClaimOff) {
  const SectionView view = Tile(true, Load::Loading, {OnePool()}, true);
  UR_EXPECT_TRUE(view.visible);
  UR_EXPECT_TRUE(view.status == Status::Loading);
  UR_EXPECT_TRUE(view.accounts.empty());
  UR_EXPECT_TRUE(view.showClaim);
  UR_EXPECT_FALSE(view.claimEnabled);
}

// CL-2: a ledger that cannot be read shows "unavailable" and no figure at all,
// never a zero, and keeps Claim disabled
UR_TEST(SnLedgerUnavailableIsNeverZero) {
  const std::vector<Account> none;
  for (const std::vector<Account>& accounts : {none, std::vector<Account>{OnePool()}}) {
    const SectionView view = Tile(true, Load::Unavailable, accounts, true);
    UR_EXPECT_TRUE(view.visible);
    UR_EXPECT_TRUE(view.status == Status::Unavailable);
    UR_EXPECT_TRUE(view.accounts.empty());
    UR_EXPECT_TRUE(view.showClaim);
    UR_EXPECT_FALSE(view.claimEnabled);
  }
}

// CL-2: an answer with no account is a real answer: nothing to claim yet
UR_TEST(SnLedgerNoAccountHasNothingToClaimYet) {
  const SectionView view = Tile(true, Load::Ready, {}, true);
  UR_EXPECT_TRUE(view.visible);
  UR_EXPECT_TRUE(view.status == Status::NothingYet);
  UR_EXPECT_TRUE(view.accounts.empty());
  UR_EXPECT_TRUE(view.showClaim);
  UR_EXPECT_FALSE(view.claimEnabled);
}

// CL-2: Claimable is the claimable, Paid the paid, Earned the finalized
// cumulative; Claim is on with something claimable
UR_TEST(SnLedgerFiguresAreClaimablePaidAndEarned) {
  const SectionView view = Tile(true, Load::Ready, {OnePool()}, true);
  UR_EXPECT_TRUE(view.visible);
  UR_EXPECT_TRUE(view.status == Status::None);
  UR_EXPECT_FALSE(view.showPools);
  AccountView expected;
  expected.noId = 1;
  expected.claimable = "a3241000000";
  expected.paid = "a1879000000";
  expected.earned = "a5120000000";
  UR_EXPECT_EQ(1u, view.accounts.size());
  UR_EXPECT_TRUE(view.accounts.front() == expected);
  UR_EXPECT_TRUE(view.showClaim);
  UR_EXPECT_TRUE(view.claimEnabled);
}

// CL-2: a pending root shows as pending: what it adds over the finalized
// cumulative, and only when it adds something
UR_TEST(SnLedgerPendingShowsTheIncreaseAwaitingFinalization) {
  const Account raised = LedgerAccount(1, 42, 5'120'000'000, 1'879'000'000, true, 5'870'000'000);
  UR_EXPECT_TRUE(Tile(true, Load::Ready, {raised}, true).accounts.front().pendingIncrease ==
                 "a750000000");
  const Account level = LedgerAccount(1, 42, 5'120'000'000, 0, true, 5'120'000'000);
  UR_EXPECT_TRUE(Tile(true, Load::Ready, {level}, true).accounts.front().pendingIncrease.empty());
  const Account lower = LedgerAccount(1, 42, 5'120'000'000, 0, true, 5'000'000'000);
  UR_EXPECT_TRUE(Tile(true, Load::Ready, {lower}, true).accounts.front().pendingIncrease.empty());
  const Account notPending = LedgerAccount(1, 42, 5'120'000'000, 0, false, 9'000'000'000);
  UR_EXPECT_TRUE(
      Tile(true, Load::Ready, {notPending}, true).accounts.front().pendingIncrease.empty());
  UR_EXPECT_EQ(int64_t{0}, urnw::snledger::PendingIncreaseRao(notPending));
}

// CL-2: an account only in the pending root shows zeros it really has, the
// pending amount, and Claim off
UR_TEST(SnLedgerAnAccountOnlyInThePendingRoot) {
  const SectionView view =
      Tile(true, Load::Ready, {LedgerAccount(1, 7, 0, 0, true, 2'000'000'000)}, true);
  UR_EXPECT_EQ(1u, view.accounts.size());
  UR_EXPECT_TRUE(view.accounts.front().claimable == "a0");
  UR_EXPECT_TRUE(view.accounts.front().earned == "a0");
  UR_EXPECT_TRUE(view.accounts.front().pendingIncrease == "a2000000000");
  UR_EXPECT_TRUE(view.showClaim);
  UR_EXPECT_FALSE(view.claimEnabled);
}

// CL-2: Claim waits while any claim is in flight (one gas key, one claim at a
// time) and needs something claimable
UR_TEST(SnLedgerClaimWaitsForAClaimInFlight) {
  UR_EXPECT_TRUE(Tile(true, Load::Ready, {OnePool()}, true, false).claimEnabled);
  UR_EXPECT_FALSE(Tile(true, Load::Ready, {OnePool()}, true, true).claimEnabled);
  UR_EXPECT_TRUE(Tile(true, Load::Ready, {OnePool()}, true, true).showClaim);
  const Account paidOut = LedgerAccount(1, 42, 5'120'000'000, 5'120'000'000);
  UR_EXPECT_FALSE(Tile(true, Load::Ready, {paidOut}, true).claimEnabled);
}

// CL-2: with more than one pool each account names its pool, and Claim
// counts every account
UR_TEST(SnLedgerSeveralPoolsNameTheirPool) {
  const std::vector<Account> accounts = {LedgerAccount(1, 42, 2'000'000'000, 2'000'000'000),
                                         LedgerAccount(3, 5, 1'000'000'000, 0)};
  const SectionView view = Tile(true, Load::Ready, accounts, true);
  UR_EXPECT_TRUE(view.showPools);
  UR_EXPECT_EQ(2u, view.accounts.size());
  UR_EXPECT_EQ(int64_t{1}, view.accounts[0].noId);
  UR_EXPECT_EQ(int64_t{3}, view.accounts[1].noId);
  UR_EXPECT_EQ(int64_t{1'000'000'000}, urnw::snledger::ClaimableRao(accounts));
  UR_EXPECT_TRUE(view.claimEnabled);
}

// ---- the claim sheet -------------------------------------------------------------

// the rows of a sheet over two pools, the second fully paid
ClaimProgress TwoPoolSheet() {
  return ClaimProgress({LedgerAccount(1, 42, 5'120'000'000, 1'879'000'000),
                        LedgerAccount(2, 9, 1'000'000'000, 1'000'000'000),
                        LedgerAccount(3, 5, 700'000'000, 0)});
}

const ClaimRow* RowOf(const ClaimProgress& progress, int64_t noId, int64_t index) {
  for (const ClaimRow& row : progress.Rows()) {
    if (row.noId == noId && row.index == index) return &row;
  }
  return nullptr;
}

// APPS-SPEC.md claim (CL-2): the sheet lists every account with something to claim
UR_TEST(SnLedgerClaimSheetListsWhatCanBeClaimed) {
  const ClaimProgress progress = TwoPoolSheet();
  UR_EXPECT_EQ(2u, progress.Rows().size());
  UR_EXPECT_TRUE(RowOf(progress, 2, 9) == nullptr);
  UR_EXPECT_EQ(int64_t{3'241'000'000}, RowOf(progress, 1, 42)->amountRao);
  UR_EXPECT_TRUE(RowOf(progress, 3, 5)->phase == Phase::Claimable);
  UR_EXPECT_EQ(int64_t{3'941'000'000}, progress.ClaimableRao());
  UR_EXPECT_FALSE(progress.Busy());
  UR_EXPECT_FALSE(progress.NeedsGas());
  UR_EXPECT_FALSE(progress.AnyClaimed());
}

// APPS-SPEC.md claim (CL-2): a press sends every row; Sent carries the
// transaction, Confirmed the amount the vault's Claimed event paid
UR_TEST(SnLedgerClaimSentThenConfirmedWithTheAmount) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  UR_EXPECT_TRUE(progress.Busy());
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Sending);
  UR_EXPECT_TRUE(RowOf(progress, 3, 5)->phase == Phase::Sending);
  progress.Sent(1, 42, "0xabc1");
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Sent);
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->txHash == "0xabc1");
  progress.Confirmed(1, 42, "0xabc1", 3'240'999'999);
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Claimed);
  UR_EXPECT_EQ(int64_t{3'240'999'999}, RowOf(progress, 1, 42)->amountRao);
  UR_EXPECT_TRUE(progress.AnyClaimed());
  UR_EXPECT_EQ(int64_t{0}, progress.ClaimableRao());  // the other row is still sending
  progress.Done();
  UR_EXPECT_FALSE(progress.Busy());
}

// APPS-SPEC.md claim (CL-2): a Confirmed without an amount keeps what was
// claimable, and a Confirmed without a transaction keeps the one Sent gave
UR_TEST(SnLedgerConfirmedWithoutAnAmountKeepsTheClaimable) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  progress.Sent(3, 5, "0xdef2");
  progress.Confirmed(3, 5, "", 0);
  UR_EXPECT_EQ(int64_t{700'000'000}, RowOf(progress, 3, 5)->amountRao);
  UR_EXPECT_TRUE(RowOf(progress, 3, 5)->txHash == "0xdef2");
}

// APPS-SPEC.md claim (CL-2): needs_gas fails the row, keeps it claimable and
// turns on the top-up until the next press
UR_TEST(SnLedgerNeedsGasTurnsOnTheTopUp) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  UR_EXPECT_TRUE(progress.Failed(1, 42, "needs_gas: the gas key holds 0.004 TAO"));
  UR_EXPECT_TRUE(progress.NeedsGas());
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Failed);
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->message == "needs_gas: the gas key holds 0.004 TAO");
  progress.Done();
  UR_EXPECT_EQ(int64_t{3'941'000'000}, progress.ClaimableRao());
  progress.Start();
  UR_EXPECT_FALSE(progress.NeedsGas());
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Sending);
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->message.empty());
}

// APPS-SPEC.md claim rule 4 (CL-2): another claim paid the account first,
// which is success: claimed, nothing to retry
UR_TEST(SnLedgerAlreadyClaimedIsPaid) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  UR_EXPECT_TRUE(progress.Failed(3, 5, "already_claimed: NothingToClaim: another claim paid"));
  UR_EXPECT_TRUE(RowOf(progress, 3, 5)->phase == Phase::Claimed);
  UR_EXPECT_TRUE(RowOf(progress, 3, 5)->message.empty());
  UR_EXPECT_FALSE(progress.NeedsGas());
  progress.Done();
  UR_EXPECT_EQ(int64_t{3'241'000'000}, progress.ClaimableRao());
}

// APPS-SPEC.md claim (CL-2): any other failure keeps its message for the
// app's words and the row claimable
UR_TEST(SnLedgerOtherFailuresKeepTheMessage) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  UR_EXPECT_TRUE(progress.Failed(1, 42, "claim_failed: TooManyStakingHotkeys"));
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Failed);
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->message == "claim_failed: TooManyStakingHotkeys");
  UR_EXPECT_FALSE(progress.NeedsGas());
  progress.Done();
  UR_EXPECT_EQ(int64_t{3'941'000'000}, progress.ClaimableRao());
}

// APPS-SPEC.md claim (CL-2): a failure about no account (noId 0) is the
// sheet's message and leaves every row as it was; needs_gas still turns on
// the top-up
UR_TEST(SnLedgerAFailureAboutNoAccountIsTheSheets) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  UR_EXPECT_FALSE(progress.Failed(0, 0, "ledger_unavailable: unreachable"));
  UR_EXPECT_EQ(2u, progress.Rows().size());
  UR_EXPECT_TRUE(RowOf(progress, 0, 0) == nullptr);
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Sending);
  UR_EXPECT_FALSE(progress.NeedsGas());
  UR_EXPECT_FALSE(progress.Failed(0, 0, "needs_gas: the gas key holds nothing"));
  UR_EXPECT_TRUE(progress.NeedsGas());
  progress.Done();
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Claimable);
  UR_EXPECT_TRUE(RowOf(progress, 3, 5)->phase == Phase::Claimable);
}

// APPS-SPEC.md claim (CL-2): after Done a row the SDK did not claim (nothing it
// verified was claimable) is claimable again, and a claimed one stays claimed
UR_TEST(SnLedgerDoneRestoresRowsTheSdkDidNotClaim) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  progress.Sent(1, 42, "0xabc1");
  progress.Confirmed(1, 42, "0xabc1", 3'241'000'000);
  progress.Done();
  UR_EXPECT_FALSE(progress.Busy());
  UR_EXPECT_TRUE(RowOf(progress, 1, 42)->phase == Phase::Claimed);
  UR_EXPECT_TRUE(RowOf(progress, 3, 5)->phase == Phase::Claimable);
  UR_EXPECT_EQ(int64_t{700'000'000}, progress.ClaimableRao());
}

// APPS-SPEC.md claim (CL-2): an account the SDK claims that the sheet did not
// list (the ledger moved since the read) gets its own row
UR_TEST(SnLedgerAnAccountTheSheetDidNotListGetsARow) {
  ClaimProgress progress = TwoPoolSheet();
  progress.Start();
  progress.Sent(4, 7, "0x0777");
  UR_EXPECT_EQ(3u, progress.Rows().size());
  UR_EXPECT_TRUE(RowOf(progress, 4, 7)->phase == Phase::Sent);
  progress.Confirmed(4, 7, "0x0777", 12'000'000);
  UR_EXPECT_EQ(int64_t{12'000'000}, RowOf(progress, 4, 7)->amountRao);
}

// APPS-SPEC.md claim (CL-2): the SDK's stable codes, with or without detail
UR_TEST(SnLedgerFailureCodes) {
  UR_EXPECT_TRUE(FailureFor("needs_gas: the gas key holds 0 TAO") == Failure::NeedsGas);
  UR_EXPECT_TRUE(FailureFor("needs_gas") == Failure::NeedsGas);
  UR_EXPECT_TRUE(FailureFor("already_claimed: NothingToClaim") == Failure::AlreadyClaimed);
  UR_EXPECT_TRUE(FailureFor("proof_mismatch: the proof does not verify") == Failure::Other);
  UR_EXPECT_TRUE(FailureFor("not_claimable: below the transfer floor") == Failure::Other);
  UR_EXPECT_TRUE(FailureFor("the gas ran out") == Failure::Other);
  UR_EXPECT_TRUE(FailureFor("") == Failure::Other);
}

}  // namespace
