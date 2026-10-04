// The manual Bittensor wallet proof (TAO.com, which documents no programmatic
// interface): the sheet shows the single-use challenge, the user signs it with
// the wallet that holds the coldkey, then pastes the address and the
// signature. The SDK session judges them (SdkHost::SubmitBittensorManual): a
// correctable refusal (bad signature, bad or different address) stays on the
// sheet; a proof, or a refusal that ends the flow, closes it. Closing the sheet
// any other way cancels the flow (bittensor::kCancelled).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

#include <gtkmm.h>

#include "SdkHost.hpp"

namespace urnw {

// The Bittensor wallet chooser (an AdwMessageDialog): one response per
// bittensor::kChooserWallets row, its id the wallet id, its label the SDK's
// product name; the body lists each wallet's hint (TAO.com: manual entry,
// WalletConnect: which wallets it works with). Map a response with
// bittensor::ChosenWallet. The caller connects "response" and presents it.
GtkWidget* NewBittensorWalletChooser(GtkWindow* parent);

class BittensorManualSheet : public Gtk::Window {
 public:
  BittensorManualSheet(Gtk::Window& parent, SdkHost& host,
                       const SdkHost::BittensorManualRequest& request);

 private:
  void OnContinue();

  SdkHost& host_;
  uint64_t flow_ = 0;
  Gtk::Entry* addressEntry_ = nullptr;
  Gtk::Entry* signatureEntry_ = nullptr;
  Gtk::Label* errorLine_ = nullptr;
  // set once the flow has an answer (a proof or a final refusal): hiding the
  // sheet then must not cancel it
  bool settled_ = false;
};

}  // namespace urnw
