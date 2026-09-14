// SPDX-License-Identifier: MPL-2.0
#include "SolanaWalletSheet.hpp"

#include <glib.h>

#include <optional>
#include <utility>

#include <urnetwork_sdk.hpp>

#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

// The Earnings page's numbers (EarningsPage.cpp), copied: the sheet is its own unit.
constexpr int kApiTimeoutMs = 20000;      // POST /account/wallet
constexpr int kBridgeTimeoutMs = 180000;  // the browser round trip: minutes are legitimate
constexpr int kValidateDebounceMs = 300;
constexpr int kSheetMinWidth = 480;

// A label at an explicit class that WRAPS (EarningsPage.cpp's note blocks).
Gtk::Label* MakeWrappedNote(const Glib::ustring& text, const char* cssClass) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->add_css_class(cssClass);
  label->set_xalign(0);
  label->set_wrap(true);
  label->set_ellipsize(Pango::EllipsizeMode::NONE);
  return label;
}

}  // namespace

SolanaWalletSheet::SolanaWalletSheet(Gtk::Window& parent, SdkHost& host, bool allowActions)
    : host_(host), allowActions_(allowActions) {
  set_transient_for(parent);
  set_modal(true);
  // the title is the dialog's accessible name
  set_title(T_("connect_solana_wallet", "Connect Solana wallet"));
  set_default_size(kSheetMinWidth, -1);
  add_css_class("ur-sheet");

  auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
  column->set_margin(24);
  column->set_size_request(kSheetMinWidth, -1);

  // what this links, and that the USDC goes there until the migration
  column->append(*MakeWrappedNote(
      T_("connect_solana_wallet_note",
         "Connect a Solana wallet app, or enter a Solana USDC address manually."),
      "ur-key"));
  column->append(*MakeWrappedNote(
      T_("usdc_payouts_until_migration",
         "USDC payouts continue to this wallet until the migration to Bittensor is complete."),
      "ur-row-note"));

  // the bridge needs the provider in the URL it opens, so both are offered up
  // front (wallet names are product names)
  auto* providers = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  phantomButton_ = Gtk::make_managed<Gtk::Button>(T_("phantom", "Phantom"));
  phantomButton_->add_css_class("suggested-action");
  phantomButton_->signal_clicked().connect(
      [this] { OnProvider(WalletConnect::Provider::Phantom); });
  providers->append(*phantomButton_);
  solflareButton_ = Gtk::make_managed<Gtk::Button>(T_("solflare", "Solflare"));
  solflareButton_->signal_clicked().connect(
      [this] { OnProvider(WalletConnect::Provider::Solflare); });
  providers->append(*solflareButton_);
  column->append(*providers);

  // "waiting" must be VISIBLE: a silently greyed button reads as a broken one
  statusLine_ = MakeWrappedNote({}, "ur-row-note");
  statusLine_->set_visible(false);
  column->append(*statusLine_);

  // the manual entry opens under the providers, which stay in view, so the
  // sheet needs no back button
  manualToggle_ =
      Gtk::make_managed<Gtk::Button>(T_("enter_address_manually", "Enter address manually"));
  manualToggle_->add_css_class("flat");
  manualToggle_->set_halign(Gtk::Align::START);
  manualToggle_->signal_clicked().connect([this] { OnToggleManual(); });
  column->append(*manualToggle_);

  manualPanel_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  entry_ = Gtk::make_managed<Gtk::Entry>();
  entry_->add_css_class("ur-input");
  entry_->set_placeholder_text(
      T_("enter_a_solana_usdc_wallet_address", "Enter a Solana USDC wallet address"));
  // a placeholder is not a name
  kit::SetAccessibleLabel(*entry_, T_("usdc_wallet_address", "USDC wallet address"));
  entry_->signal_changed().connect([this] { OnAddressChanged(); });
  manualPanel_->append(*entry_);
  // the caption class, not the note class: kit::ApplySupportingText's
  // .ur-danger-text comes earlier in the stylesheet than .ur-row-note and would
  // lose to its muted colour, while it follows .ur-caption and wins
  supportingLine_ = MakeWrappedNote({}, "ur-caption");
  supportingLine_->set_visible(false);
  manualPanel_->append(*supportingLine_);
  connectButton_ = Gtk::make_managed<Gtk::Button>(T_("connect", "Connect"));
  connectButton_->signal_clicked().connect([this] { OnConnectPressed(); });
  manualPanel_->append(*connectButton_);
  manualPanel_->set_visible(false);
  column->append(*manualPanel_);

  errorLine_ = MakeWrappedNote({}, "ur-caption");
  errorLine_->add_css_class("ur-danger-text");  // follows .ur-caption: the danger colour wins
  errorLine_->set_visible(false);
  column->append(*errorLine_);

  auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  root->append(*column);
  auto* cancel = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  cancel->set_halign(Gtk::Align::END);
  cancel->set_margin(16);
  cancel->signal_clicked().connect([this] { set_visible(false); });
  root->append(*cancel);
  set_child(*root);

  // Dismissal is allowed in every state, a round trip out included: whatever
  // is still out is dropped here, the host answers a waiting connect
  // "superseded" when the next wallet flow starts, and the page reloads the
  // wallets on its next Load.
  signal_hide().connect([this] { Abandon(); });

  Render();
}

SolanaWalletSheet::~SolanaWalletSheet() {
  ++*epoch_;  // a callback outliving the sheet finds a moved epoch and stops
  checkDebounce_.disconnect();
  watchdog_.disconnect();
}

void SolanaWalletSheet::Abandon() {
  ++*epoch_;
  ++flowGeneration_;
  ++checkGeneration_;
  checkDebounce_.disconnect();
  watchdog_.disconnect();
}

void SolanaWalletSheet::SetBridgeBusy(bool busy) {
  if (bridgeBusy_ == busy) return;
  bridgeBusy_ = busy;
  Render();
}

// ---- the bridge ------------------------------------------------------------------

void SolanaWalletSheet::OnProvider(WalletConnect::Provider provider) {
  // the buttons are already insensitive in each of these; checked again at
  // press time, out loud
  if (!allowActions_ || bridgeBusy_) {
    g_message("earnings: solana wallet provider press refused (actions %s, bridge %s)",
              allowActions_ ? "allowed" : "off", bridgeBusy_ ? "busy" : "free");
    return;
  }
  if (!machine_.ChooseProvider()) return;  // one round trip at a time
  ++checkGeneration_;                      // a check still out no longer matters
  checkDebounce_.disconnect();
  const uint64_t generation = ++flowGeneration_;
  // 180 s, not 20 s: the bridge answers only when a deep link comes BACK, and
  // a closed browser tab produces nothing, ever
  ArmWatchdog(kBridgeTimeoutMs);
  Render();
  auto epoch = epoch_;
  const uint64_t seen = *epoch_;
  host_.ConnectSolanaWallet(
      provider, [this, epoch, seen, generation](SdkHost::SolanaConnectResult result) {
        PostToMain([this, epoch, seen, generation, result = std::move(result)] {
          if (*epoch != seen) return;  // dismissed or destroyed
          OnBridgeAnswer(generation, result);
        });
      });
}

void SolanaWalletSheet::OnBridgeAnswer(uint64_t generation,
                                       const SdkHost::SolanaConnectResult& result) {
  if (generation != flowGeneration_) {
    g_message("earnings: dropping a solana wallet bridge answer for an abandoned round trip");
    return;
  }
  const std::string address = solana::Trim(result.address);
  if (!result.ok || address.empty()) {
    g_warning("earnings: solana wallet connect failed: %s",
              result.error.empty() ? "(no public key)" : result.error.c_str());
    if (machine_.BridgeError(result.error)) {
      watchdog_.disconnect();
      Render();
    }
    return;
  }
  if (!machine_.PublicKey()) return;
  // not checked client side (android and apple did not either): the server
  // validates the key when the wallet is created
  CreateWallet(address);
}

// ---- the manual address --------------------------------------------------------------

void SolanaWalletSheet::OnToggleManual() {
  manualOpen_ = !manualOpen_;
  Render();
  if (manualOpen_ && entry_->get_sensitive()) entry_->grab_focus();
}

void SolanaWalletSheet::OnAddressChanged() {
  // every keystroke: forget the verdict and drop the answer still out
  ++checkGeneration_;
  checkDebounce_.disconnect();
  checkedAddress_.clear();
  if (!machine_.Typed()) {
    Render();
    return;
  }
  Render();
  checkDebounce_ = Glib::signal_timeout().connect(
      [this]() -> bool {
        ValidateAddress();
        return false;  // non-repeating
      },
      kValidateDebounceMs);
}

// The shape first, locally, before ANY network call; then the server.
void SolanaWalletSheet::ValidateAddress() {
  const std::string address = solana::Trim(entry_->get_text().raw());
  if (address.empty()) return;  // nothing typed: silent
  if (!solana::LooksLikeSolanaAddress(address)) {
    if (machine_.Malformed()) Render();
    return;
  }
  // The one affordance allowed to decline SILENTLY: the user did not ask for
  // anything, so without actions (the preview) the field says nothing more.
  if (!allowActions_) return;
  if (!machine_.Check()) return;
  const uint64_t generation = ++checkGeneration_;
  Render();
  urnet::WalletValidateAddressArgs args;
  args.address = address;
  args.chain = std::string(urnet::SOL);
  auto epoch = epoch_;
  const uint64_t seen = *epoch_;
  host_.api().walletValidateAddress(
      args, [this, epoch, seen, generation, address](
                std::optional<urnet::WalletValidateAddressResult> result,
                std::optional<std::string> err) {
        solana::AddressVerdict verdict = solana::AddressVerdict::Unavailable;
        if (!err && result) {
          verdict = result->valid.value_or(false) ? solana::AddressVerdict::Valid
                                                  : solana::AddressVerdict::Invalid;
        } else {
          g_warning("earnings: walletValidateAddress(SOL) failed: %s",
                    err ? err->c_str() : "(no result)");
        }
        PostToMain([this, epoch, seen, generation, address, verdict] {
          if (*epoch != seen) return;
          OnVerdict(generation, address, verdict);
        });
      });
}

void SolanaWalletSheet::OnVerdict(uint64_t generation, const std::string& address,
                                  solana::AddressVerdict verdict) {
  if (generation != checkGeneration_) return;  // the text moved on
  if (!machine_.Verdict(verdict)) return;
  if (verdict == solana::AddressVerdict::Valid) checkedAddress_ = address;
  Render();
}

void SolanaWalletSheet::OnConnectPressed() {
  if (!allowActions_) return;
  const std::string address = solana::Trim(entry_->get_text().raw());
  if (address.empty()) return;
  if (address != checkedAddress_) {
    ValidateAddress();  // the verdict is stale: ask again, never send unchecked
    return;
  }
  if (!machine_.Submit()) return;
  CreateWallet(address);
}

// ---- linking ---------------------------------------------------------------------

void SolanaWalletSheet::CreateWallet(const std::string& address) {
  const uint64_t generation = ++flowGeneration_;
  ArmWatchdog(kApiTimeoutMs);
  Render();
  urnet::CreateAccountWalletArgs args;
  args.blockchain = urnet::SOL;
  args.wallet_address = address;
  args.default_token_type = "USDC";
  auto epoch = epoch_;
  const uint64_t seen = *epoch_;
  host_.api().createAccountWallet(
      args, [this, epoch, seen, generation](std::optional<urnet::CreateAccountWalletResult> result,
                                            std::optional<std::string> err) {
        // success = a result carrying a NON-EMPTY wallet id
        const std::string walletId =
            result && result->wallet_id ? *result->wallet_id : std::string();
        const bool ok = !err.has_value() && result.has_value() && !walletId.empty();
        const std::string detail = err.value_or(std::string());
        PostToMain([this, epoch, seen, generation, ok, walletId, detail] {
          if (*epoch != seen) return;
          OnCreated(generation, ok, walletId, detail);
        });
      });
}

void SolanaWalletSheet::OnCreated(uint64_t generation, bool ok, const std::string& walletId,
                                  const std::string& detail) {
  if (generation != flowGeneration_) {
    g_message("earnings: dropping a solana wallet create answer for an abandoned request");
    return;
  }
  watchdog_.disconnect();
  if (!ok) {
    g_warning("earnings: createAccountWallet(SOL) failed: %s",
              detail.empty() ? "(no wallet id)" : detail.c_str());
  }
  if (!machine_.CreateResult(ok, walletId, detail)) return;
  if (machine_.state != solana::ConnectState::Linked) {
    Render();
    return;
  }
  // linked: the sheet hides and the page takes it from here
  const auto connected = on_connected;
  const std::string linkedId = machine_.walletId;
  set_visible(false);
  if (connected) connected(linkedId);
}

void SolanaWalletSheet::ArmWatchdog(int timeoutMs) {
  watchdog_.disconnect();
  const uint64_t generation = flowGeneration_;
  watchdog_ = Glib::signal_timeout().connect(
      [this, generation]() -> bool {
        if (generation != flowGeneration_) return false;  // already answered
        // bump AGAIN so the give-up is final: a late answer must not undo what
        // the user was already told
        ++flowGeneration_;
        g_warning("earnings: a solana wallet request never answered - giving up on it");
        if (machine_.Timeout()) Render();
        return false;
      },
      timeoutMs);
}

// ---- drawing ---------------------------------------------------------------------

void SolanaWalletSheet::Render() {
  const bool input = machine_.AcceptsInput();
  const bool providers = allowActions_ && input && !bridgeBusy_;
  phantomButton_->set_sensitive(providers);
  solflareButton_->set_sensitive(providers);
  manualToggle_->set_sensitive(input);
  manualPanel_->set_visible(manualOpen_);
  entry_->set_sensitive(input);
  connectButton_->set_sensitive(allowActions_ && machine_.ConnectAllowed());

  Glib::ustring status;
  switch (machine_.state) {
    case solana::ConnectState::OpeningBrowser:
      status = T_("opening_wallet_in_browser", "Opening your wallet in the browser…");
      break;
    case solana::ConnectState::Linking:
      status = T_("connecting_to_wallet", "Connecting to wallet...");
      break;
    case solana::ConnectState::Idle:
    case solana::ConnectState::Checking:
    case solana::ConnectState::Ready:
    case solana::ConnectState::Failed:
    case solana::ConnectState::Linked:
      // the providers are waiting on the page's Bittensor round trip
      if (bridgeBusy_ && allowActions_ && input) {
        status = T_("opening_bittensor_wallet_in_browser",
                    "Opening your Bittensor wallet in the browser…");
      }
      break;
  }
  kit::SetTextOrCollapse(*statusLine_, status);

  switch (machine_.supporting) {
    case solana::Supporting::None:
      kit::SetTextOrCollapse(*supportingLine_, {});
      break;
    case solana::Supporting::Checking:
      kit::ApplySupportingText(*supportingLine_,
                               T_("checking_wallet_address", "Checking address…"),
                               kit::ValidationState::Validating);
      supportingLine_->set_visible(true);
      break;
    case solana::Supporting::Invalid:
      kit::ApplySupportingText(*supportingLine_,
                               T_("invalid_solana_address", "That is not a valid Solana address."),
                               kit::ValidationState::Invalid);
      supportingLine_->set_visible(true);
      break;
    case solana::Supporting::Unavailable:
      // the check itself failed: nothing is sent until it can be checked;
      // retyping retries
      kit::ApplySupportingText(*supportingLine_,
                               T_("something_went_wrong", "Something went wrong."),
                               kit::ValidationState::Invalid);
      supportingLine_->set_visible(true);
      break;
  }

  Glib::ustring error;
  if (machine_.state == solana::ConnectState::Failed) {
    const std::string key = solana::FailureKey(machine_.detail);
    if (key == "wallet_connect_failed") {
      error = T_("wallet_connect_failed", "Failed to connect the wallet.");
    } else if (key == "error_connecting_wallet_with_reason") {
      // the words that came back VERBATIM: often the only diagnostic
      error = Format(T_("error_connecting_wallet_with_reason",
                        "There was an error connecting your wallet: {}"),
                     machine_.detail);
    } else {
      error = T_("something_went_wrong", "Something went wrong.");
    }
  }
  kit::SetTextOrCollapse(*errorLine_, error);
}

}  // namespace urnw
