// The call sites of the daemon's support log (daemon/SupportDiagnostics.cpp).
// The lines are pure and tested in DiagnosticLinesTest.cpp; that they are
// written through the sdk's log at the moments they explain, and that nothing
// but codes and tokens is handed to them, lives in the daemon, which needs
// glib and the SDK, so this reads its sources: a decision with no caller is
// this project's most-repeated defect.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadDiagnosticSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// `first` occurs, and before `second` (which must occur too).
bool Before(const std::string& text, const std::string& first, const std::string& second) {
  const size_t a = text.find(first);
  const size_t b = text.find(second);
  return a != std::string::npos && b != std::string::npos && a < b;
}

size_t Count(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

}  // namespace

// Through the sdk's log, which is what feedback uploads, behind the gate.
UR_TEST(DiagnosticWiring_LinesGoThroughTheSdkLog) {
  const std::string support = ReadDiagnosticSource("daemon/SupportDiagnostics.cpp");
  const std::string write =
      FunctionBody(support, "void Write(const std::string& key, const char* tag,");
  UR_EXPECT_TRUE(Before(write, "if (!Admit(key, line)) return;", "urnet::logAppInfo(tag, line);"));
  UR_EXPECT_EQ(1, Count(support, "urnet::logAppInfo("));
  // and the daemon builds it
  const std::string meson = ReadDiagnosticSource("../meson.build");
  const size_t daemon = meson.find("executable('urnetworkd',");
  UR_EXPECT_TRUE(daemon != std::string::npos);
  if (daemon == std::string::npos) return;
  UR_EXPECT_TRUE(Has(meson.substr(daemon, meson.find("],", daemon) - daemon),
                     "'src/daemon/SupportDiagnostics.cpp'"));
}

// Daemon start: the split tunnel, the kill switch, the DNS host and its
// default-route link, and connectivity; and every network country reading.
UR_TEST(DiagnosticWiring_TheDaemonLogsItsHostAndTheNetworkCountry) {
  const std::string main = ReadDiagnosticSource("daemon/main.cpp");
  UR_EXPECT_TRUE(Before(
      main, "urnw::support::LogDaemonStart(urnw::TunnelConfig().name, sweptArmedFloor);",
      "g_main_loop_run(loop);"));
  UR_EXPECT_TRUE(Before(main, "tunnel.SetNetworkCountryCode(reading.country_code);",
                        "urnw::support::LogNetworkCountry(reading);"));
  const std::string start = FunctionBody(ReadDiagnosticSource("daemon/SupportDiagnostics.cpp"),
                                         "void LogDaemonStart(");
  for (const char* needle :
       {"diag::SplitTunnelLine(ReadProcText(\"/proc/self/cgroup\"))", "diag::KillSwitchStartLine(",
        "diag::DnsHostLine(\"start\"", "WriteDnsLink(\"default-route\"",
        "LogConnectivity(\"start\");"}) {
    UR_EXPECT_TRUE_MSG(needle, Has(start, needle));
  }
}

// The up edge: the result, the DNS host, the tunnel's link and the
// default-route link, and connectivity — written after opMutex_ is released,
// because they fork resolvectl.
UR_TEST(DiagnosticWiring_TheUpEdgeIsLoggedOutsideTheLock) {
  const std::string host = ReadDiagnosticSource("daemon/TunnelHost.cpp");
  const std::string run = FunctionBody(host, "void TunnelHost::RunStart(");
  UR_EXPECT_TRUE(Before(run, "status_.tunnel_state = ctl::TunnelState::Up;", "upFacts.emplace();"));
  UR_EXPECT_TRUE(Before(run, "busy_.store(false);",
                        "if (upFacts) support::LogTunnelUp(*upFacts, upInterface);"));
  const std::string up = FunctionBody(ReadDiagnosticSource("daemon/SupportDiagnostics.cpp"),
                                      "void LogTunnelUp(");
  for (const char* needle :
       {"diag::TunnelUpLine(facts)", "diag::DnsHostLine(\"tunnel-up\"",
        "WriteDnsLink(\"tunnel\", tunnelInterface", "WriteDnsLink(\"default-route\"",
        "LogConnectivity(\"tunnel-up\");"}) {
    UR_EXPECT_TRUE_MSG(needle, Has(up, needle));
  }
}

// A failed start, a session the daemon stopped as unsafe and an io loop that
// died are logged with their code and reason — published first, so the status
// ordering rule holds.
UR_TEST(DiagnosticWiring_EndsAreLoggedWithCodes) {
  const std::string host = ReadDiagnosticSource("daemon/TunnelHost.cpp");
  UR_EXPECT_TRUE(Before(FunctionBody(host, "void TunnelHost::RunStart("),
                        "status_.stop_reason = \"start_failed\";",
                        "support::LogTunnelEnded(\"start-failed\", \"start_failed\", keptCode);"));
  UR_EXPECT_TRUE(Has(FunctionBody(host, "void TunnelHost::RunStart("),
                     "support::LogConnectivity(\"start-failed\");"));
  UR_EXPECT_TRUE(Before(FunctionBody(host, "void TunnelHost::StopUnsafeSessionLocked("),
                        "status_.error_code = code;",
                        "support::LogTunnelEnded(\"stopped\", reason, code);"));
  UR_EXPECT_TRUE(
      Before(FunctionBody(host, "void TunnelHost::Reap()"), "status_.stop_reason = \"io_loop\";",
             "support::LogTunnelEnded(\"stopped\", \"io_loop\", ctl::kCodeTunOpenFailed);"));
}

// The kill switch after every nft transaction, the DNS override's repairs, and
// connectivity at a path change.
UR_TEST(DiagnosticWiring_KillSwitchDnsAndPathChangesAreLogged) {
  const std::string host = ReadDiagnosticSource("daemon/TunnelHost.cpp");
  const std::string install = FunctionBody(host, "bool TunnelHost::InstallFilterLocked(");
  UR_EXPECT_TRUE(
      Before(install, "status_.kill_switch = published;", "support::LogKillSwitch(facts);"));
  UR_EXPECT_TRUE(Has(install, "facts.published = ctl::ToString(Status().kill_switch);"));
  UR_EXPECT_TRUE(Before(FunctionBody(host, "void TunnelHost::MaintainDnsLocked()"),
                        "const bool applied = tunnel_->ApplyDns();",
                        "support::LogDnsOverride(\"lost\", applied, tunnel_->dnsBackend());"));
  UR_EXPECT_TRUE(Has(FunctionBody(host, "void TunnelHost::OnResolvedAppeared("),
                     "support::LogDnsOverride(\"resolved-restarted\", applied"));
  const std::string reap = FunctionBody(host, "void TunnelHost::Reap()");
  UR_EXPECT_TRUE(Before(reap, "if (networkChange == LinuxNetworkChange::Path) {",
                        "support::LogConnectivity(\"path-change\");"));
}

// No error message, DNS detail or exception text is handed to the support log:
// those name paths, addresses and servers. And the reads never start a
// service: NetworkManager without auto-start, resolvectl only where
// systemd-resolved already runs, each bounded by timeout(1).
UR_TEST(DiagnosticWiring_OnlyCodesAndTokensReachTheLog) {
  for (const char* file : {"daemon/TunnelHost.cpp", "daemon/main.cpp"}) {
    const std::string source = ReadDiagnosticSource(file);
    std::istringstream lines(source);
    std::string line;
    int calls = 0;
    while (std::getline(lines, line)) {
      if (!Has(line, "support::Log")) continue;
      ++calls;
      for (const char* leak : {"error", "Error", "detail", "message", "what()", "dns_servers",
                               "client_id", "jwt"}) {
        UR_EXPECT_TRUE_MSG(std::string(file) + ": " + line, !Has(line, leak));
      }
    }
    UR_EXPECT_TRUE_MSG(file, calls > 0);
  }
  const std::string support = ReadDiagnosticSource("daemon/SupportDiagnostics.cpp");
  UR_EXPECT_EQ(Count(support, "g_dbus_connection_call("),
               Count(support, "G_DBUS_CALL_FLAGS_NO_AUTO_START"));
  UR_EXPECT_FALSE(Has(support, "_sync("));
  const std::string read = FunctionBody(support, "ResolvedListings ReadResolved(");
  UR_EXPECT_TRUE(Before(
      read, "if (!probe.resolved_running || !probe.resolvectl_present) return listings;",
      "RunCommand("));
  UR_EXPECT_TRUE(Has(read, "FindTool(\"timeout\")"));
}
