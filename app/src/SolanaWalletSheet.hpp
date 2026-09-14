// "Connect Solana wallet": the sheet the Earnings page opens from the three-dot
// overflow beside "Connect Bittensor wallet". It links the Solana wallet USDC
// payouts are sent to until the migration to Bittensor completes -- the
// connect flow the old payout pane had, restored for the networks still owed
// USDC.
//
// Two ways in, on one modal window: Phantom or Solflare through the ur.io
// wallet bridge (SdkHost::ConnectSolanaWallet hands back the wallet's public
// key; nothing is signed), or an address entered by hand (checked locally for
// its base58 shape, then by POST /wallet/validate-address for SOL). Either way
// the wallet is created with POST /account/wallet {SOL, address, USDC}; the
// page then makes it the payout wallet when the server did not.
//
// The address field stays live while the browser round trip is out; the two
// never write over each other (the machine numbers every round trip and ties
// every verdict to its text). Every failure renders ON THE SHEET: a snackbar
// behind a modal is unreadable.
// The states and their transitions are SolanaWalletPresentation.hpp's
// ConnectMachine (tested on the host); this class runs the requests, the
// debounce and the watchdogs, and draws what the machine says.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <gtkmm.h>

#include "SdkHost.hpp"
#include "SolanaWalletPresentation.hpp"

namespace urnw {

// One wording for every Solana connect, link and remove failure, on the sheet
// and on the Earnings page: the words that came back, VERBATIM (often the only
// diagnostic), in error_connecting_wallet_with_reason, and something_went_wrong
// only when none did. solana::FailureKey decides which.
Glib::ustring SolanaFailureText(const std::string& detail);

class SolanaWalletSheet : public Gtk::Window {
 public:
  // allowActions=false (the preview harness): the provider and Connect buttons
  // render insensitive and the address is checked locally only.
  SolanaWalletSheet(Gtk::Window& parent, SdkHost& host, bool allowActions);
  ~SolanaWalletSheet() override;

  // The wallet was created and the sheet has hidden itself (page:
  // OnSolanaConnected, which makes it the payout wallet and reloads).
  std::function<void(std::string walletId)> on_connected;
  // The sheet was dismissed while a wallet was being linked: the create call's
  // answer is dropped with the sheet, so the page reloads the wallets to show
  // what the server did (page: LoadLegacyWallets with reset).
  std::function<void()> on_abandoned_link;

 private:
  void OnProvider(WalletConnect::Provider provider);
  void OnBridgeAnswer(uint64_t round, const SdkHost::SolanaConnectResult& result);
  void OnToggleManual();
  void OnAddressChanged();
  void ValidateAddress();
  void OnVerdict(const std::string& address, solana::AddressVerdict verdict);
  void OnConnectPressed();
  void CreateWallet(uint64_t round, const std::string& address);
  void OnCreated(uint64_t round, bool ok, const std::string& walletId,
                 const std::string& detail);
  void ArmWatchdog(int timeoutMs, uint64_t round);
  // The sheet was dismissed (or linked): everything still out is dropped.
  void Abandon();
  void Render();

  SdkHost& host_;
  const bool allowActions_;
  bool manualOpen_ = false;
  solana::ConnectMachine machine_;

  // stale-async guards: epoch_ drops every answer once the sheet is dismissed
  // or destroyed; within its life the machine refuses an answer for a round
  // trip that was given up or replaced, and a verdict for text that changed
  std::shared_ptr<uint64_t> epoch_ = std::make_shared<uint64_t>(0);
  sigc::connection checkDebounce_;
  sigc::connection watchdog_;

  Gtk::Button* phantomButton_ = nullptr;
  Gtk::Button* solflareButton_ = nullptr;
  Gtk::Label* statusLine_ = nullptr;
  Gtk::Button* manualToggle_ = nullptr;
  Gtk::Box* manualPanel_ = nullptr;
  Gtk::Entry* entry_ = nullptr;
  Gtk::Label* supportingLine_ = nullptr;
  Gtk::Button* connectButton_ = nullptr;
  Gtk::Label* errorLine_ = nullptr;
};

}  // namespace urnw
