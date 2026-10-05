// The cloud proxies page on ur.io that Settings opens, decided pure so
// tests/CloudProxyLinkTest.cpp can pin it without GTK or the SDK.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw::cloudproxy {

// The app has no protocol switch: WireGuard, SOCKS and HTTPS proxies are
// created on ur.io (SOCKS and WireGuard on Pro), and Settings opens this page.
// It carries no credential; ur.io asks the user to sign in when it needs to.
constexpr const char* kProxiesUrl = "https://ur.io/app/proxies";

}  // namespace urnw::cloudproxy
