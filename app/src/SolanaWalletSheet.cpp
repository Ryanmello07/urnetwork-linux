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

Glib::ustring SolanaFailureText(const std::string& detail) {
  if (std::string(solana::FailureKey(detail)) == "error_connecting_wallet_with_reason") {
    return Format(T_("error_connecting_wallet_with_reason",
                     "There was an error connecting your wallet: {}"),
                  detail);
  }
  return T_("something_went_wrong", "Something went wrong.");
}

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
  // "superseded by ..." when the next wallet flow starts, and the page reloads
  // the wallets at once when a link was out (on_abandoned_link), else on its
  // next Load.
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
  checkDebounce_.disconnect();
  watchdog_.disconnect();
  // a create call still out may link the wallet after all: the page looks again
  if (machine_.state == solana::ConnectState::Linking && on_abandoned_link) {
    const auto reload = std::move(on_abandoned_link);
    on_abandoned_link = nullptr;
    reload();
  }
}

// ---- the bridge ------------------------------------------------------------------

void SolanaWalletSheet::OnProvider(WalletConnect::Provider provider) {
  // the buttons are already insensitive without actions (the preview);
  // checked again at press time, out loud. A Bittensor connect still out on
  // the page does not hold them: this request supersedes it in the host.
  if (!allowActions_) {
    g_message("earnings: solana wallet provider press refused (actions off)");
    return;
  }
  // one round trip at a time; a check of the typed address keeps going
  const uint64_t round = machine_.ChooseProvider();
  if (round == 0) return;
  // 180 s, not 20 s: the bridge answers only when a deep link comes BACK, and
  // a closed browser tab produces nothing, ever
  ArmWatchdog(kBridgeTimeoutMs, round);
  Render();
  auto epoch = epoch_;
  const uint64_t seen = *epoch_;
  host_.ConnectSolanaWallet(
      provider, [this, epoch, seen, round](SdkHost::SolanaConnectResult result) {
        PostToMain([this, epoch, seen, round, result = std::move(result)] {
          if (*epoch != seen) return;  // dismissed or destroyed
          OnBridgeAnswer(round, result);
        });
      });
}

void SolanaWalletSheet::OnBridgeAnswer(uint64_t round,
                                       const SdkHost::SolanaConnectResult& result) {
  const std::string key = solana::Trim(result.address);
  if (!result.ok || key.empty()) {
    // an answer for a round trip that was given up or replaced (the host's
    // "superseded by ..." for the first of two presses) is refused here
    if (!machine_.BridgeError(round, result.error)) {
      g_message("earnings: dropping a solana wallet bridge answer for an abandoned round trip");
      return;
    }
    g_warning("earnings: solana wallet connect failed: %s",
              result.error.empty() ? "(no public key)" : result.error.c_str());
    watchdog_.disconnect();
    Render();
    return;
  }
  if (!machine_.PublicKey(round)) {
    g_message("earnings: dropping a solana wallet public key for an abandoned round trip");
    return;
  }
  // Not checked client side (android and apple did not either): the server
  // validates the key when the wallet is created. The key is linked by itself;
  // an address typed meanwhile keeps its own verdict and is not what is sent.
  CreateWallet(round, key);
}

// ---- the manual address --------------------------------------------------------------

void SolanaWalletSheet::OnToggleManual() {
  manualOpen_ = !manualOpen_;
  Render();
  if (manualOpen_ && entry_->get_sensitive()) entry_->grab_focus();
}

void SolanaWalletSheet::OnAddressChanged() {
  // every keystroke: forget the verdict (the machine refuses a verdict still out
  // for the old text) and start the debounce again
  checkDebounce_.disconnect();
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

// The shape first, locally, before ANY network call; then the server. A check
// may run while the browser round trip is out without ending it.
void SolanaWalletSheet::ValidateAddress() {
  const std::string address = solana::Trim(entry_->get_text().raw());
  if (address.empty()) return;  // nothing typed: silent
  if (!solana::LooksLikeSolanaAddress(address)) {
    if (machine_.Malformed(address)) Render();
    return;
  }
  // The one affordance allowed to decline SILENTLY: the user did not ask for
  // anything, so without actions (the preview) the field says nothing more.
  if (!allowActions_) return;
  if (!machine_.Check(address)) return;
  Render();
  urnet::WalletValidateAddressArgs args;
  args.address = address;
  args.chain = std::string(urnet::SOL);
  auto epoch = epoch_;
  const uint64_t seen = *epoch_;
  host_.api().walletValidateAddress(
      args, [this, epoch, seen, address](std::optional<urnet::WalletValidateAddressResult> result,
                                         std::optional<std::string> err) {
        solana::AddressVerdict verdict = solana::AddressVerdict::Unavailable;
        if (!err && result) {
          verdict = result->valid.value_or(false) ? solana::AddressVerdict::Valid
                                                  : solana::AddressVerdict::Invalid;
        } else {
          g_warning("earnings: walletValidateAddress(SOL) failed: %s",
                    err ? err->c_str() : "(no result)");
        }
        PostToMain([this, epoch, seen, address, verdict] {
          if (*epoch != seen) return;
          OnVerdict(address, verdict);
        });
      });
}

void SolanaWalletSheet::OnVerdict(const std::string& address, solana::AddressVerdict verdict) {
  if (!machine_.Verdict(address, verdict)) return;  // the text moved on
  Render();
}

void SolanaWalletSheet::OnConnectPressed() {
  if (!allowActions_) return;
  const std::string address = solana::Trim(entry_->get_text().raw());
  if (address.empty()) return;
  if (address != machine_.address || machine_.check != solana::AddressCheck::Valid) {
    ValidateAddress();  // the verdict is stale: ask again, never send unchecked
    return;
  }
  const uint64_t round = machine_.Submit();  // 0 while a round trip is out
  if (round == 0) return;
  CreateWallet(round, address);
}

// ---- linking ---------------------------------------------------------------------

void SolanaWalletSheet::CreateWallet(uint64_t round, const std::string& address) {
  ArmWatchdog(kApiTimeoutMs, round);
  Render();
  urnet::CreateAccountWalletArgs args;
  args.blockchain = urnet::SOL;
  args.wallet_address = address;
  args.default_token_type = "USDC";
  auto epoch = epoch_;
  const uint64_t seen = *epoch_;
  host_.api().createAccountWallet(
      args, [this, epoch, seen, round](std::optional<urnet::CreateAccountWalletResult> result,
                                       std::optional<std::string> err) {
        // success = a result carrying a NON-EMPTY wallet id
        const std::string walletId =
            result && result->wallet_id ? *result->wallet_id : std::string();
        const bool ok = !err.has_value() && result.has_value() && !walletId.empty();
        const std::string detail = err.value_or(std::string());
        PostToMain([this, epoch, seen, round, ok, walletId, detail] {
          if (*epoch != seen) return;
          OnCreated(round, ok, walletId, detail);
        });
      });
}

void SolanaWalletSheet::OnCreated(uint64_t round, bool ok, const std::string& walletId,
                                  const std::string& detail) {
  if (!machine_.CreateResult(round, ok, walletId, detail)) {
    g_message("earnings: dropping a solana wallet create answer for an abandoned request");
    return;
  }
  watchdog_.disconnect();
  if (machine_.state != solana::ConnectState::Linked) {
    g_warning("earnings: createAccountWallet(SOL) failed: %s",
              detail.empty() ? "(no wallet id)" : detail.c_str());
    Render();
    return;
  }
  // linked: the sheet hides and the page takes it from here
  const auto connected = on_connected;
  const std::string linkedId = machine_.walletId;
  set_visible(false);
  if (connected) connected(linkedId);
}

void SolanaWalletSheet::ArmWatchdog(int timeoutMs, uint64_t round) {
  watchdog_.disconnect();
  watchdog_ = Glib::signal_timeout().connect(
      [this, round]() -> bool {
        // taken only for the round trip still out; the machine refuses that
        // round trip's answers from here on, so the give-up is final
        if (machine_.Timeout(round)) {
          g_warning("earnings: a solana wallet request never answered - giving up on it");
          Render();
        }
        return false;
      },
      timeoutMs);
}

// ---- drawing ---------------------------------------------------------------------

void SolanaWalletSheet::Render() {
  // the providers and Connect wait while a round trip is out; the manual entry
  // stays live until a wallet is being linked
  const bool providers = allowActions_ && machine_.ProvidersEnabled();
  phantomButton_->set_sensitive(providers);
  solflareButton_->set_sensitive(providers);
  manualToggle_->set_sensitive(machine_.EntryEnabled());
  manualPanel_->set_visible(manualOpen_);
  entry_->set_sensitive(machine_.EntryEnabled());
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
      break;
  }
  kit::SetTextOrCollapse(*statusLine_, status);

  switch (machine_.check) {
    case solana::AddressCheck::None:
    case solana::AddressCheck::Valid:
      // an accepted address needs no line: Connect says it
      kit::SetTextOrCollapse(*supportingLine_, {});
      break;
    case solana::AddressCheck::Checking:
      kit::ApplySupportingText(*supportingLine_,
                               T_("checking_wallet_address", "Checking address…"),
                               kit::ValidationState::Validating);
      supportingLine_->set_visible(true);
      break;
    case solana::AddressCheck::Invalid:
      kit::ApplySupportingText(*supportingLine_,
                               T_("invalid_solana_address", "That is not a valid Solana address."),
                               kit::ValidationState::Invalid);
      supportingLine_->set_visible(true);
      break;
    case solana::AddressCheck::Unavailable:
      // the check itself failed: nothing is sent until it can be checked;
      // retyping retries
      kit::ApplySupportingText(*supportingLine_,
                               T_("something_went_wrong", "Something went wrong."),
                               kit::ValidationState::Invalid);
      supportingLine_->set_visible(true);
      break;
  }

  kit::SetTextOrCollapse(*errorLine_, machine_.state == solana::ConnectState::Failed
                                          ? SolanaFailureText(machine_.detail)
                                          : Glib::ustring());
}

}  // namespace urnw
