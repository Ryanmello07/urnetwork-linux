// SPDX-License-Identifier: MPL-2.0
#include "daemon/SupportDiagnostics.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>

#include <gio/gio.h>

#include <urnetwork_sdk.hpp>

#include "NetworkQuality.hpp"

namespace urnw {
namespace support {
namespace {

// The same line under one key again only after this long.
constexpr int64_t kRepeatMillis = 10 * 60 * 1000;
// Each resolvectl read, where timeout(1) exists to bound it.
constexpr const char* kResolvectlTimeoutSeconds = "2";
constexpr gint kNetworkManagerTimeoutMillis = 2000;

struct Gate {
  std::mutex mutex;
  diag::LineGate lines{kRepeatMillis};
};

// Never destroyed, like DaemonLog: a line written during static destruction
// must not touch a dead gate.
Gate& TheGate() {
  static Gate* gate = new Gate();
  return *gate;
}

bool Admit(const std::string& key, const std::string& line) {
  Gate& gate = TheGate();
  std::scoped_lock lock(gate.mutex);
  return gate.lines.Admit(key, line, g_get_monotonic_time() / 1000);
}

void Write(const std::string& key, const char* tag, const std::string& line) {
  if (!Admit(key, line)) return;
  urnet::logAppInfo(tag, line);
}

std::string ReadProcText(const char* path) {
  std::ifstream in(path);
  if (!in) return {};
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The link the default route leaves through (IPv4, else IPv6), never the tun.
std::string DefaultRouteInterface(const std::string& tunnelInterface) {
  std::string iface = LinuxDefaultRouteInterface(ReadProcText("/proc/net/route"), tunnelInterface);
  if (iface.empty()) {
    iface = LinuxDefaultRouteInterfaceV6(ReadProcText("/proc/net/ipv6_route"), tunnelInterface);
  }
  return iface;
}

// systemd-resolved's settings for every link, read with resolvectl's listing
// verbs. Only where resolved runs (resolvectl would D-Bus-activate a stopped
// one), and each read bounded by timeout(1) when the host has it.
struct ResolvedListings {
  bool read = false;
  diag::ResolvectlListing dns_over_tls;
  diag::ResolvectlListing dnssec;
  diag::ResolvectlListing servers;
  diag::ResolvectlListing default_route;
};

ResolvedListings ReadResolved(const DnsHostProbe& probe) {
  ResolvedListings listings;
  if (!probe.resolved_running || !probe.resolvectl_present) return listings;
  const std::string resolvectl = FindTool("resolvectl");
  if (resolvectl.empty()) return listings;
  const std::string timeout = FindTool("timeout");
  const auto listing = [&](const char* verb, diag::ResolvectlListing* out) {
    std::vector<std::string> argv;
    if (!timeout.empty()) argv = {timeout, kResolvectlTimeoutSeconds};
    argv.push_back(resolvectl);
    argv.push_back(verb);
    const CommandResult result = RunCommand(argv);
    if (!result.ok()) return false;
    *out = diag::ParseResolvectlListing(result.output);
    return true;
  };
  if (!listing("dnsovertls", &listings.dns_over_tls)) return listings;
  if (!listing("dnssec", &listings.dnssec)) return listings;
  listing("dns", &listings.servers);
  listing("default-route", &listings.default_route);
  listings.read = true;
  return listings;
}

diag::DnsHostFacts HostFacts(const DnsHostProbe& probe, const ResolvedListings& resolved) {
  diag::DnsHostFacts facts;
  facts.resolved_running = probe.resolved_running;
  facts.resolvectl_present = probe.resolvectl_present;
  facts.nss_resolve_before_dns = probe.nss_resolve_before_dns;
  facts.resolv_conf_points_at_resolved = probe.resolv_conf_points_at_resolved;
  facts.resolvconf_present = probe.resolvconf_present;
  facts.resolvconf_is_resolvectl = probe.resolvconf_is_resolvectl;
  facts.resolved_read = resolved.read;
  facts.dns_over_tls = resolved.dns_over_tls.global;
  facts.dnssec = resolved.dnssec.global;
  return facts;
}

void WriteDnsLink(const char* role, const std::string& name, const ResolvedListings& resolved) {
  if (!resolved.read || name.empty()) return;
  Write(std::string(diag::kTagDnsLink) + "/" + role, diag::kTagDnsLink,
        diag::DnsLinkLine(diag::DnsLinkFrom(role, name, resolved.servers, resolved.dns_over_tls,
                                            resolved.dnssec, resolved.default_route)));
}

// What every NetworkManager callback carries.
struct ConnectivityRead {
  std::string moment;
};

void OnConnectivity(GObject* source, GAsyncResult* result, gpointer data) {
  std::unique_ptr<ConnectivityRead> read(static_cast<ConnectivityRead*>(data));
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  // Not running is a different fact from running and refusing the read.
  const bool running =
      reply != nullptr || !(g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
                            g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER));
  g_clear_error(&error);
  bool answered = false;
  uint32_t state = 0;
  if (reply != nullptr) {
    GVariant* value = nullptr;
    g_variant_get(reply, "(v)", &value);
    if (value != nullptr && g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
      state = g_variant_get_uint32(value);
      answered = true;
    }
    if (value != nullptr) g_variant_unref(value);
    g_variant_unref(reply);
  }
  Write(diag::kTagConnectivity, diag::kTagConnectivity,
        diag::ConnectivityLine(read->moment.c_str(), running, answered, state));
}

void OnSystemBus(GObject*, GAsyncResult* result, gpointer data) {
  std::unique_ptr<ConnectivityRead> read(static_cast<ConnectivityRead*>(data));
  GError* error = nullptr;
  GDBusConnection* bus = g_bus_get_finish(result, &error);
  g_clear_error(&error);
  if (bus == nullptr) {
    Write(diag::kTagConnectivity, diag::kTagConnectivity,
          diag::ConnectivityLine(read->moment.c_str(), false, false, 0));
    return;
  }
  // NO_AUTO_START: a NetworkManager that is not running is reported absent,
  // never started by being asked.
  g_dbus_connection_call(bus, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                         "org.freedesktop.DBus.Properties", "Get",
                         g_variant_new("(ss)", "org.freedesktop.NetworkManager", "Connectivity"),
                         G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                         kNetworkManagerTimeoutMillis, nullptr, &OnConnectivity, read.release());
  g_object_unref(bus);
}

}  // namespace

const char* DnsBackendName(DnsBackend backend) {
  switch (backend) {
    case DnsBackend::None: return "none";
    case DnsBackend::SystemdResolved: return "systemd-resolved";
    case DnsBackend::Resolvconf: return "resolvconf";
    case DnsBackend::DirectFile: return "direct-file";
  }
  return "none";
}

void LogDaemonStart(const std::string& tunnelInterface, bool armedFloorCarriedOver) {
  Write(diag::kTagSplitTunnel, diag::kTagSplitTunnel,
        diag::SplitTunnelLine(ReadProcText("/proc/self/cgroup")));
  Write(diag::kTagKillSwitch, diag::kTagKillSwitch,
        diag::KillSwitchStartLine(!FindTool("nft").empty(), armedFloorCarriedOver));
  const DnsHostProbe probe = ProbeDnsHost();
  const ResolvedListings resolved = ReadResolved(probe);
  Write(diag::kTagDns, diag::kTagDns, diag::DnsHostLine("start", HostFacts(probe, resolved)));
  WriteDnsLink("default-route", DefaultRouteInterface(tunnelInterface), resolved);
  LogConnectivity("start");
}

void LogNetworkCountry(const NetworkCountryReading& reading) {
  Write(diag::kTagNetworkCountry, diag::kTagNetworkCountry, diag::NetworkCountryLine(reading));
}

void LogTunnelUp(const diag::TunnelUpFacts& facts, const std::string& tunnelInterface) {
  Write(diag::kTagTunnel, diag::kTagTunnel, diag::TunnelUpLine(facts));
  const DnsHostProbe probe = ProbeDnsHost();
  const ResolvedListings resolved = ReadResolved(probe);
  Write(diag::kTagDns, diag::kTagDns, diag::DnsHostLine("tunnel-up", HostFacts(probe, resolved)));
  WriteDnsLink("tunnel", tunnelInterface, resolved);
  WriteDnsLink("default-route", DefaultRouteInterface(tunnelInterface), resolved);
  LogConnectivity("tunnel-up");
}

void LogTunnelEnded(const char* what, const std::string& reason, const std::string& code) {
  Write(diag::kTagTunnel, diag::kTagTunnel, diag::TunnelEndedLine(what, reason, code));
}

void LogKillSwitch(const diag::KillSwitchFacts& facts) {
  Write(diag::kTagKillSwitch, diag::kTagKillSwitch, diag::KillSwitchLine(facts));
}

void LogDnsOverride(const char* event, bool applied, DnsBackend backend) {
  Write(std::string(diag::kTagDns) + "/override", diag::kTagDns,
        diag::DnsOverrideLine(event, applied, DnsBackendName(backend)));
}

void LogConnectivity(const char* moment) {
  g_bus_get(G_BUS_TYPE_SYSTEM, nullptr, &OnSystemBus, new ConnectivityRead{moment});
}

}  // namespace support
}  // namespace urnw
