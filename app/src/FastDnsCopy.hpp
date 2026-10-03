// The DNS setting that races a handicapped resolver over the host network while the tunnel's
// DNS starts (sdk DnsResolverSettings.EnableFallback). It is opt-in and off by default, since
// those lookups can be seen by the local network and their answers are for the device's real
// location rather than the exit; every surface (the DNS sheet toggle and the
// connect page/drawer status rows) uses this one key and English fallback.
// Kept independent of GTK/gettext so it is unit-testable.
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw {

struct FastDnsCopy {
  const char* key;
  const char* english;
};

inline constexpr FastDnsCopy kFastDnsLabel{"fast_dns_on_connect", "Fast DNS on connect"};

inline constexpr FastDnsCopy kFastDnsDescription{
    "fast_dns_on_connect_description",
    "Answers DNS over the local network while the tunnel's DNS starts. This can reveal your "
    "lookups to the local network and return answers that don't match your exit location. When "
    "off, DNS only resolves through the tunnel."};

}  // namespace urnw
