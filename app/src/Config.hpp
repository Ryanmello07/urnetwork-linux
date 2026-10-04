// Build-time app configuration. Values here are placeholders that a build
// machine (or a developer) fills in — nothing in this file is a secret, and
// every value degrades gracefully when left empty.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw {

// WalletConnect Cloud project id, for the Bittensor WalletConnect wallet (Nova,
// Nightly and other WalletConnect v2 substrate wallets): SdkHost passes it to the
// SDK session (setWalletConnectProjectId), which hands it to the ur.io
// bittensor-connect page as wc_project_id. Set with -Dwalletconnect_project_id;
// empty is valid and makes the page use its own configured id. Talisman and
// TAO.com never send it.
#ifndef UR_WALLETCONNECT_PROJECT_ID
#define UR_WALLETCONNECT_PROJECT_ID ""
#endif
inline constexpr const char* kWalletConnectProjectId = UR_WALLETCONNECT_PROJECT_ID;

}  // namespace urnw
