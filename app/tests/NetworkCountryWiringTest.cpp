// The call sites of the network country (P052). The rule is pure and tested in
// NetworkCountryTest.cpp; what puts its answer in force — the daemon's
// ModemManager watcher, the sdk setter before any device is built, the status
// field and the GUI's own process — needs glib, GTK and the SDK, so this reads
// their sources: a decision with no caller is this project's most-repeated
// defect.
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

std::string ReadCountrySource(const std::string& relative) {
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

// The daemon reads it and puts it in force through the sdk's process-wide
// setter, from a watcher started before the control socket serves anyone — so
// before any start_tunnel or start_provider can build a device.
UR_TEST(NetworkCountryWiring_TheDaemonAppliesItBeforeServing) {
  const std::string main = ReadCountrySource("daemon/main.cpp");
  UR_EXPECT_TRUE(Has(main, "tunnel.SetNetworkCountryCode(reading.country_code);"));
  UR_EXPECT_TRUE(Before(main, "urnw::TunnelHost tunnel(stateDir);",
                        "urnw::NetworkCountryWatcher networkCountry("));
  UR_EXPECT_TRUE(Before(main, "networkCountry.Start();", "server.Start()"));
  UR_EXPECT_TRUE(Before(main, "networkCountry.Start();", "g_main_loop_run(loop);"));

  const std::string set = FunctionBody(ReadCountrySource("daemon/TunnelHost.cpp"),
                                       "void TunnelHost::SetNetworkCountryCode(");
  UR_EXPECT_TRUE(Has(set, "urnet::setNetworkCountryCode(countryCode);"));
  UR_EXPECT_TRUE(Has(set, "status_.network_country_code = countryCode;"));
}

// The daemon must build the watcher, or none of it runs.
UR_TEST(NetworkCountryWiring_TheDaemonBuildsTheWatcher) {
  const std::string meson = ReadCountrySource("../meson.build");
  const size_t daemon = meson.find("executable('urnetworkd',");
  UR_EXPECT_TRUE(daemon != std::string::npos);
  if (daemon == std::string::npos) return;
  const size_t sources = meson.find("],", daemon);
  UR_EXPECT_TRUE(Has(meson.substr(daemon, sources - daemon),
                     "'src/daemon/NetworkCountryWatcher.cpp'"));
}

// A ModemManager the user disabled is never started by being asked something,
// and nothing here blocks the main loop the control socket is served on.
UR_TEST(NetworkCountryWiring_TheWatcherNeverStartsOrBlocksOnModemManager) {
  const std::string watcher = ReadCountrySource("daemon/NetworkCountryWatcher.cpp");
  UR_EXPECT_TRUE(Count(watcher, "g_dbus_connection_call(") >= 2);
  UR_EXPECT_TRUE(Count(watcher, "g_dbus_connection_call(") ==
                 Count(watcher, "G_DBUS_CALL_FLAGS_NO_AUTO_START"));
  UR_EXPECT_TRUE(Has(watcher, "G_BUS_NAME_WATCHER_FLAGS_NONE"));
  UR_EXPECT_FALSE(Has(watcher, "G_BUS_NAME_WATCHER_FLAGS_AUTO_START"));
  UR_EXPECT_FALSE(Has(watcher, "_sync("));
  // calls go out only while the name has an owner
  UR_EXPECT_TRUE(Before(FunctionBody(watcher, "void NetworkCountryWatcher::ReadModems()"),
                        "if (!modemManagerRunning_ || bus_ == nullptr) return;",
                        "g_dbus_connection_call("));
}

// What it reads: the modems' network ports and bearers, the operator each is
// registered to, and the default route of both families — and it decides with
// the shared rule.
UR_TEST(NetworkCountryWiring_TheWatcherReadsTheModemsAndTheRoute) {
  const std::string watcher = ReadCountrySource("daemon/NetworkCountryWatcher.cpp");
  for (const char* needle :
       {"\"org.freedesktop.ModemManager1\"", "\"GetManagedObjects\"", "\"Ports\"",
        "\"Bearers\"", "\"OperatorCode\"", "\"org.freedesktop.ModemManager1.Bearer\"",
        "\"Interface\"", "\"Connected\"", "kModemPortTypeNet = 2", "\"/proc/net/route\"",
        "\"/proc/net/ipv6_route\"", "ReadNetworkCountry("}) {
    UR_EXPECT_TRUE_MSG(needle, Has(watcher, needle));
  }
  // a bearer names its interface only while it is connected
  const std::string bearer =
      FunctionBody(watcher, "std::string ConnectedBearerInterface(GVariant* reply)");
  UR_EXPECT_TRUE(Before(bearer, "\"Connected\", \"b\", &connected) && connected",
                        "\"Interface\""));
  UR_EXPECT_TRUE(Has(FunctionBody(watcher, "void NetworkCountryWatcher::OnBearerProperties("),
                     "ConnectedBearerInterface(reply)"));
}

// Never the locale or the timezone: they describe the user, not the network,
// and the sdk ranks the reported country above the operator's last answer.
UR_TEST(NetworkCountryWiring_NoLocaleOrTimezoneStandsIn) {
  for (const char* file : {"NetworkCountry.hpp", "daemon/NetworkCountryWatcher.hpp",
                           "daemon/NetworkCountryWatcher.cpp"}) {
    const std::string source = ReadCountrySource(file);
    UR_EXPECT_TRUE_MSG(file, !source.empty());
    for (const char* forbidden :
         {"getenv(", "setlocale(", "std::locale", "g_get_language_names", "/etc/localtime",
          "/etc/timezone", "g_time_zone", "localtime_r(", "tzname"}) {
      UR_EXPECT_TRUE_MSG(std::string(file) + " " + forbidden, !Has(source, forbidden));
    }
  }
}

// The GUI applies the daemon's value to its own process (its sign-in and api
// dials) from the health poll, which runs from the sign-in screen on: the
// disconnected branch asks, the connected branch reuses the status it read.
UR_TEST(NetworkCountryWiring_TheGuiFollowsTheDaemon) {
  const std::string host = ReadCountrySource("SdkHost.cpp");
  const std::string follow = FunctionBody(
      host, "void SdkHost::FollowDaemonNetworkCountry(const ctl::StatusReply& status)");
  UR_EXPECT_TRUE(Has(follow, "status.redacted ? std::string() : status.network_country_code"));
  UR_EXPECT_TRUE(Has(follow, "urnet::setNetworkCountryCode(countryCode);"));

  // both branches of the health poll, with the status its worker read
  const std::string poll =
      FunctionBody(ReadCountrySource("MainWindow.cpp"),
                   "void MainWindow::ApplyDaemonHealth("
                   "const std::optional<ctl::StatusReply>& status)");
  const size_t idle = poll.find("if (!connected_) {");
  const size_t idleEnd = poll.find("return;", idle);
  UR_EXPECT_TRUE(idle != std::string::npos && idleEnd != std::string::npos);
  if (idle == std::string::npos || idleEnd == std::string::npos) return;
  UR_EXPECT_TRUE(
      Has(poll.substr(idle, idleEnd - idle), "host_.FollowDaemonNetworkCountry(*status);"));
  UR_EXPECT_TRUE(Before(poll.substr(idleEnd), "if (!status) return;",
                        "host_.FollowDaemonNetworkCountry(*status);"));
}
