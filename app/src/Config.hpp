// Build-time app configuration. Values here are placeholders that a build
// machine (or a developer) fills in — nothing in this file is a secret, and
// every value degrades gracefully when left empty.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw {

// WalletConnect Cloud project id. UNUSED since the Bittensor wallet-connect
// session (sdk bittensor_wallet.go, UPGRADE.md 4.6): the supported wallets are
// Talisman, through its browser extension on the ur.io bridge, and TAO.com,
// by manual entry -- neither documents a WalletConnect interface, so the
// bridge is never sent a project id. The define and the meson option
// (-Dwalletconnect_project_id) stay so existing build invocations still
// configure.
#ifndef UR_WALLETCONNECT_PROJECT_ID
#define UR_WALLETCONNECT_PROJECT_ID ""
#endif
inline constexpr const char* kWalletConnectProjectId = UR_WALLETCONNECT_PROJECT_ID;

}  // namespace urnw
