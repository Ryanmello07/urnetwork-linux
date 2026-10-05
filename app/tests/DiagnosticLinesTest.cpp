// The daemon's support log lines (DiagnosticLines.hpp): what each says, the
// filters every host-supplied value passes, and the repeat gate. The lines go
// into the logs a user uploads with feedback, so no address, path or MAC may
// reach them.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "DiagnosticLines.hpp"

namespace diag = urnw::diag;

namespace {

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// What systemd-resolved 255's resolvectl prints for its listing verbs.
const char* kDnsOverTls = R"(Global: no
Link 2 (wlp2s0): opportunistic
Link 5 (urnet0): no
Link 7 (wlx001122334455):
)";
const char* kDnssec = R"(Global: allow-downgrade
Link 2 (wlp2s0): yes
Link 5 (urnet0):
Link 7 (wlx001122334455):
)";
const char* kDns = R"(Global: 1.1.1.1#cloudflare-dns.com 9.9.9.9
Link 2 (wlp2s0): 192.168.1.1 fe80::1%2
Link 5 (urnet0): 169.254.2.1 fd00:7572:6e77::53
Link 7 (wlx001122334455): 10.0.0.1
)";
const char* kDefaultRoute = R"(Link 2 (wlp2s0): yes
Link 5 (urnet0): no
)";

}  // namespace

UR_TEST(diagnosticTokenKeepsWordsOnly) {
  UR_EXPECT_TRUE(diag::Token("yes") == "yes");
  UR_EXPECT_TRUE(diag::Token("Allow-Downgrade") == "allow-downgrade");
  UR_EXPECT_TRUE(diag::Token("dns_apply_failed") == "dns_apply_failed");
  UR_EXPECT_TRUE(diag::Token("dns override lost") == "dns-override-lost");
  for (const char* value : {"", "192.168.1.1", "fe80::1", "/etc/resolv.conf", "x\ny", "a\tb",
                            "could not open /dev/net/tun", "0123456789012345678901234567890123"}) {
    UR_EXPECT_TRUE_MSG(value, diag::Token(value) == "other");
  }
  UR_EXPECT_TRUE(diag::YesNoSetting("yes") == "yes");
  UR_EXPECT_TRUE(diag::YesNoSetting("") == "default");
  UR_EXPECT_TRUE(diag::YesNoSetting("10.0.0.1") == "other");
}

UR_TEST(diagnosticInterfaceNamesLoseTheirMac) {
  UR_EXPECT_TRUE(diag::SafeInterfaceName("wlx001122334455") == "wlx[mac]");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("enx0A1B2C3D4E5F") == "enx[mac]");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("wlp2s0") == "wlp2s0");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("enp0s20f0u2") == "enp0s20f0u2");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("qmapmux0.0") == "qmapmux0.0");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("") == "none");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("a-name-too-long-for-linux") == "other");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("eth 0") == "other");
  UR_EXPECT_TRUE(diag::SafeInterfaceName("../../x") == "other");
  // fifteen bytes with an x third, but not hex: kept
  UR_EXPECT_TRUE(diag::SafeInterfaceName("wlxnotahexvalue") == "wlxnotahexvalue");
}

UR_TEST(diagnosticResolvedModesAreKnownValues) {
  for (const char* mode : {"yes", "no", "opportunistic", "allow-downgrade"}) {
    UR_EXPECT_TRUE_MSG(mode, diag::ResolvedMode(mode) == mode);
  }
  UR_EXPECT_TRUE(diag::ResolvedMode("") == "default");
  UR_EXPECT_TRUE(diag::ResolvedMode("n/a") == "default");
  UR_EXPECT_TRUE(diag::ResolvedMode("1.1.1.1") == "other");
}

UR_TEST(diagnosticParsesResolvectlListings) {
  const diag::ResolvectlListing dot = diag::ParseResolvectlListing(kDnsOverTls);
  UR_EXPECT_TRUE(dot.has_global && dot.global == "no");
  UR_EXPECT_TRUE(dot.links.at("wlp2s0") == "opportunistic");
  UR_EXPECT_TRUE(dot.links.at("urnet0") == "no");
  UR_EXPECT_TRUE(dot.links.at("wlx001122334455").empty());
  const diag::ResolvectlListing route = diag::ParseResolvectlListing(kDefaultRoute);
  UR_EXPECT_FALSE(route.has_global);
  UR_EXPECT_EQ(2, route.links.size());
  UR_EXPECT_EQ(2, diag::CountWords(diag::ParseResolvectlListing(kDns).links.at("wlp2s0")));
  UR_EXPECT_TRUE(diag::ParseResolvectlListing("").links.empty());
}

// One link's DNS: counts and modes, never the servers, and a MAC-named link
// without its MAC.
UR_TEST(diagnosticDnsLinkLinesCountServersAndNameModes) {
  const diag::ResolvectlListing dns = diag::ParseResolvectlListing(kDns);
  const diag::ResolvectlListing dot = diag::ParseResolvectlListing(kDnsOverTls);
  const diag::ResolvectlListing dnssec = diag::ParseResolvectlListing(kDnssec);
  const diag::ResolvectlListing route = diag::ParseResolvectlListing(kDefaultRoute);

  const std::string physical =
      diag::DnsLinkLine(diag::DnsLinkFrom("default-route", "wlp2s0", dns, dot, dnssec, route));
  UR_EXPECT_TRUE(physical ==
                 "role=default-route link=wlp2s0 servers=2 dot=opportunistic dnssec=yes "
                 "default-route=yes");
  const std::string tunnel =
      diag::DnsLinkLine(diag::DnsLinkFrom("tunnel", "urnet0", dns, dot, dnssec, route));
  UR_EXPECT_TRUE(tunnel == "role=tunnel link=urnet0 servers=2 dot=no dnssec=default "
                           "default-route=no");
  const std::string usbWifi = diag::DnsLinkLine(
      diag::DnsLinkFrom("default-route", "wlx001122334455", dns, dot, dnssec, route));
  UR_EXPECT_TRUE(usbWifi == "role=default-route link=wlx[mac] servers=1 dot=default "
                            "dnssec=default default-route=default");
  const std::string unlisted =
      diag::DnsLinkLine(diag::DnsLinkFrom("default-route", "wwan0", dns, dot, dnssec, route));
  UR_EXPECT_TRUE(unlisted == "role=default-route link=wwan0 resolved=unlisted");
  for (const std::string& line : {physical, tunnel, usbWifi}) {
    for (const char* leak : {"192.168", "fe80", "169.254", "fd00", "10.0.0.1", "cloudflare",
                             "001122334455"}) {
      UR_EXPECT_TRUE_MSG(line + " / " + leak, !Has(line, leak));
    }
  }
}

UR_TEST(diagnosticDnsHostLineNamesTheTierAndModes) {
  diag::DnsHostFacts facts;
  facts.resolved_running = true;
  facts.resolvectl_present = true;
  facts.nss_resolve_before_dns = true;
  facts.resolv_conf_points_at_resolved = true;
  facts.resolved_read = true;
  facts.dns_over_tls = "yes";
  facts.dnssec = "allow-downgrade";
  UR_EXPECT_TRUE(diag::DnsHostLine("start", facts) ==
                 "at=start tier=systemd-resolved resolved=running nss-resolve-first=yes "
                 "resolv.conf-stub=yes dot=yes dnssec=allow-downgrade");

  diag::DnsHostFacts arch;  // resolved not running, no resolvconf: the direct file
  arch.resolvectl_present = true;
  UR_EXPECT_TRUE(diag::DnsHostLine("tunnel-up", arch) ==
                 "at=tunnel-up tier=direct-file resolved=stopped nss-resolve-first=no "
                 "resolv.conf-stub=no");
  diag::DnsHostFacts debian;
  debian.resolvconf_present = true;
  UR_EXPECT_TRUE(Has(diag::DnsHostLine("start", debian), "tier=resolvconf"));
  debian.resolvconf_is_resolvectl = true;  // resolved's shim is not a resolvconf
  UR_EXPECT_TRUE(Has(diag::DnsHostLine("start", debian), "tier=direct-file"));
  // a value resolved never prints becomes "other"
  facts.dns_over_tls = "9.9.9.9";
  UR_EXPECT_TRUE(Has(diag::DnsHostLine("start", facts), " dot=other "));
}

UR_TEST(diagnosticKillSwitchLinesSayWhatIsInForce) {
  diag::KillSwitchFacts facts;
  facts.requested = true;
  facts.state = "armed";
  facts.published = "armed";
  facts.floor = true;
  facts.ipv6_blocked = true;
  UR_EXPECT_TRUE(diag::KillSwitchLine(facts) ==
                 "requested=yes table=armed status=armed floor=yes ipv6-blocked=yes "
                 "dns-floor=no exclusion=no nft=ok");
  facts.applied = false;
  facts.published = "failed";
  UR_EXPECT_TRUE(Has(diag::KillSwitchLine(facts), "status=failed"));
  UR_EXPECT_TRUE(Has(diag::KillSwitchLine(facts), "nft=failed"));
  UR_EXPECT_TRUE(diag::KillSwitchStartLine(true, true) ==
                 "at=start nft=present floor=carried-over");
  UR_EXPECT_TRUE(diag::KillSwitchStartLine(false, false) == "at=start nft=missing floor=none");
}

UR_TEST(diagnosticSplitTunnelLineReadsTheHierarchy) {
  UR_EXPECT_TRUE(diag::SplitTunnelLine("0::/system.slice/urnetworkd.service\n") ==
                 "cgroup-v2=unified exclusion=available");
  const std::string hybrid = R"(12:cpu,cpuacct:/system.slice
0::/system.slice/urnetworkd.service
)";
  UR_EXPECT_TRUE(diag::SplitTunnelLine(hybrid) == "cgroup-v2=hybrid exclusion=unavailable");
  UR_EXPECT_TRUE(diag::SplitTunnelLine("12:cpu,cpuacct:/system.slice\n") ==
                 "cgroup-v2=none exclusion=unavailable");
  UR_EXPECT_TRUE(diag::SplitTunnelLine("") == "cgroup-v2=none exclusion=unavailable");
}

UR_TEST(diagnosticNetworkCountryLineNamesTheSource) {
  urnw::ModemReading modem;
  modem.data_interfaces = {"wwan0"};
  modem.operator_code = "25001";
  UR_EXPECT_TRUE(diag::NetworkCountryLine(urnw::ReadNetworkCountry("wwan0", true, true, {modem})) ==
                 "country=ru source=modem interface=wwan0");
  UR_EXPECT_TRUE(
      diag::NetworkCountryLine(urnw::ReadNetworkCountry("wlx001122334455", true, true, {modem})) ==
      "country=none source=not-cellular interface=wlx[mac]");
  UR_EXPECT_TRUE(diag::NetworkCountryLine(urnw::ReadNetworkCountry("", false, false, {})) ==
                 "country=none source=modemmanager-absent interface=none");
  // the operator itself never appears
  UR_EXPECT_FALSE(
      Has(diag::NetworkCountryLine(urnw::ReadNetworkCountry("wwan0", true, true, {modem})),
          "25001"));
}

UR_TEST(diagnosticConnectivityLineNamesTheState) {
  UR_EXPECT_TRUE(diag::ConnectivityLine("start", true, true, 4) == "at=start networkmanager=full");
  UR_EXPECT_TRUE(diag::ConnectivityLine("path-change", true, true, 2) ==
                 "at=path-change networkmanager=portal");
  UR_EXPECT_TRUE(diag::ConnectivityLine("tunnel-up", true, true, 3) ==
                 "at=tunnel-up networkmanager=limited");
  UR_EXPECT_TRUE(Has(diag::ConnectivityLine("start", true, true, 1), "networkmanager=none"));
  UR_EXPECT_TRUE(Has(diag::ConnectivityLine("start", true, true, 0), "networkmanager=unknown"));
  UR_EXPECT_TRUE(Has(diag::ConnectivityLine("start", true, true, 9), "networkmanager=other"));
  UR_EXPECT_TRUE(Has(diag::ConnectivityLine("start", false, false, 4), "networkmanager=absent"));
  UR_EXPECT_TRUE(Has(diag::ConnectivityLine("start", true, false, 4), "networkmanager=unreadable"));
}

UR_TEST(diagnosticTunnelLinesCarryCodesNotProse) {
  diag::TunnelUpFacts facts;
  facts.dns_backend = "systemd-resolved";
  facts.dns_applied = true;
  facts.ipv6_captured = true;
  facts.egress_protected = true;
  facts.socket_marker = true;
  UR_EXPECT_TRUE(diag::TunnelUpLine(facts) ==
                 "up dns=systemd-resolved dns-applied=yes ipv6=captured egress-protected=yes "
                 "egress=socket-marker");
  UR_EXPECT_TRUE(diag::TunnelEndedLine("start-failed", "start_failed", "dns_apply_failed") ==
                 "start-failed reason=start_failed code=dns_apply_failed");
  UR_EXPECT_TRUE(diag::TunnelEndedLine("stopped", "", "") == "stopped reason=none code=none");
  // an unsafe stop's reason may be a phrase; a message never passes
  UR_EXPECT_TRUE(diag::TunnelEndedLine("stopped", "dns override lost", "dns_apply_failed") ==
                 "stopped reason=dns-override-lost code=dns_apply_failed");
  UR_EXPECT_TRUE(diag::TunnelEndedLine("stopped", "tun 10.0.0.1 failed", "") ==
                 "stopped reason=other code=none");
  UR_EXPECT_TRUE(diag::DnsOverrideLine("lost", false, "direct-file") ==
                 "override=lost applied=no backend=direct-file");
}

UR_TEST(diagnosticLineGateRepeatsOnlyAfterTheWindow) {
  diag::LineGate gate(600000);
  UR_EXPECT_TRUE(gate.Admit("dns", "a", 0));
  UR_EXPECT_FALSE(gate.Admit("dns", "a", 1000));
  UR_EXPECT_TRUE(gate.Admit("dns", "b", 2000));    // a change is written at once
  UR_EXPECT_TRUE(gate.Admit("dns", "a", 3000));    // and so is the change back
  UR_EXPECT_FALSE(gate.Admit("dns", "a", 602999));
  UR_EXPECT_TRUE(gate.Admit("dns", "a", 603000));  // the window has passed
  UR_EXPECT_TRUE(gate.Admit("dns-link/tunnel", "a", 603001));  // keys are separate
}
