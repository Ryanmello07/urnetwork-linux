// Build-time app configuration. Values here are placeholders that a build
// machine (or a developer) fills in — nothing in this file is a secret, and
// every value degrades gracefully when left empty.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string_view>

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

namespace urnw::config {

// ---- the browser approval: a sign-in approved on a web page -------------------
//
// The approval behind the login's Auth code tile is off in this build. It is
// offered only when this is true and the active network is one a request can
// name (SdkHost::CurrentApprovalNetwork: the official network, or another whose
// API url is https://host[:port]), the approval page answers its descriptor
// with the contract version that network needs (below, ApprovalCheck.hpp), the
// process is not running as root and the desktop starts this app for
// urnetwork:// links (SchemeHandler.hpp). Turn it on once the page lists linux;
// until then the tile opens the auth code dialog as it always has. A test pins
// it false on main; this beta line (beta/browser-approval, never merged) turns
// it on to test the approval, and its test pins it true.
inline constexpr bool kBrowserApprovalEnabled = true;

// The origin of the approval page, a build constant: the app never takes a page
// from a server, a file, a network space or a request. For a local end-to-end
// run against a page served at another address, build with
//   -Dcpp_args='-DUR_APPROVAL_TEST_ORIGIN=\"http://localhost:5173\"'
// (or the same define in CXXFLAGS); it replaces the scheme, host and port of
// both urls below (the descriptor check still applies to it). No meson file,
// option or script of this repository defines it, and a test says so: a shipped
// build always talks to the real page. A build with the override says so in
// the app's log at start, the host only (kApprovalOriginIsATest, MainWindow).
#if defined(UR_APPROVAL_TEST_ORIGIN)
#define UR_APPROVAL_ORIGIN UR_APPROVAL_TEST_ORIGIN
inline constexpr bool kApprovalOriginIsATest = true;
#else
#define UR_APPROVAL_ORIGIN "https://manager.bringyour.com"
inline constexpr bool kApprovalOriginIsATest = false;
#endif

// The page that shows the person which network is signing in and asks them to
// approve (contract v1: the request is this url plus a fragment).
inline constexpr char kApprovalPageUrl[] = UR_APPROVAL_ORIGIN "/login-app";
// What the page says about itself, fetched with one GET before the approval is
// offered: {"contracts":[1,2],"platforms":["windows","linux"]} as
// application/json. The official network needs the page to list 1, another
// network needs 2 (ApprovalNetwork.hpp).
inline constexpr char kApprovalDescriptorUrl[] = UR_APPROVAL_ORIGIN "/approval-contract.json";

#undef UR_APPROVAL_ORIGIN

// The host (and port) of a url, for the plain text beside the button that names
// where the browser is going.
constexpr std::string_view HostOfUrl(std::string_view url) {
  const std::size_t scheme = url.find("://");
  if (scheme == std::string_view::npos) return {};
  url.remove_prefix(scheme + 3);
  return url.substr(0, url.find_first_of("/?#"));
}

inline constexpr std::string_view kApprovalHost = HostOfUrl(kApprovalPageUrl);

static_assert(HostOfUrl("https://manager.bringyour.com/login-app") == "manager.bringyour.com");
static_assert(HostOfUrl("http://localhost:5173") == "localhost:5173");
static_assert(HostOfUrl("no scheme").empty());
#if !defined(UR_APPROVAL_TEST_ORIGIN)
static_assert(!kApprovalOriginIsATest);
static_assert(std::string_view(kApprovalPageUrl) == "https://manager.bringyour.com/login-app");
static_assert(std::string_view(kApprovalDescriptorUrl) ==
              "https://manager.bringyour.com/approval-contract.json");
static_assert(kApprovalHost == "manager.bringyour.com");
#endif

}  // namespace urnw::config
