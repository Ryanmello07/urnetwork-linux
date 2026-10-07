// Every connect gesture's path to a provider, as Windows takes it: the
// Connect button and the tray go where the provider row says (the selected
// location, or the best available with none), read before the start, because
// a start that builds a new device answers SelectedLocation from that device.
// Before, the press ran ConnectBestAvailable whatever was selected. A location
// row's click takes the same start path to its own location, so it starts a
// tunnel when there is none; before, it only drove a session that was already
// up. Disconnect, and quit, stop the daemon's tunnel before they unwind the
// SDK, so the machine's network comes back without waiting on a device rpc.
// Every start_tunnel names the gesture that asked for it in the journal.
// MainWindow, SdkHost and the rows need gtkmm and the SDK, so this reads their
// sources with the comments blanked.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadFunnelSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  bool inString = false;
  for (size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (inString) {
      if (c == '\\') {
        ++at;
      } else if (c == '"' || c == '\n') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
      while (at < text.size() && text[at] != '\n') text[at++] = ' ';
    }
  }
  return text;
}

// The definition that starts with `signature`, up to the closing brace at the
// start of a line.
std::string FunnelBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool FunnelHas(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Each needle occurs after the one before it.
bool FunnelInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

const char* const kStartToTarget =
    "TunnelStartResult MainWindow::StartTunnelUi(const char* reason,\n"
    "                                            const std::optional<urnet::ConnectLocation>& "
    "target) {";

}  // namespace

// The Connect button and the tray connect through the start path to the
// selection, and issue no connect of their own.
UR_TEST(ConnectFunnelWiring_TheButtonConnectsToTheSelection) {
  const std::string window = ReadFunnelSource("MainWindow.cpp");
  const std::string toggle =
      FunnelBody(window, "void MainWindow::ToggleConnect(bool disconnect) {");
  UR_EXPECT_TRUE(!toggle.empty());
  const std::string connectHalf = toggle.substr(toggle.find("ClearDisconnectIntent()"));
  UR_EXPECT_TRUE(FunnelHas(connectHalf, "StartTunnelUi(\"connect press\");"));
  UR_EXPECT_FALSE(FunnelHas(connectHalf, "ConnectBestAvailable"));
  UR_EXPECT_FALSE(FunnelHas(connectHalf, "host_.Connect("));
  // the selection is read before the start, and handed to it
  const std::string selected =
      FunnelBody(window, "TunnelStartResult MainWindow::StartTunnelUi(const char* reason) {");
  UR_EXPECT_TRUE(FunnelHas(selected, "return StartTunnelUi(reason, host_.SelectedLocation());"));
}

// After the start, the target or the best available, and no second read of
// the selection from the device the start may have just built.
UR_TEST(ConnectFunnelWiring_TheStartConnectsItsTarget) {
  const std::string window = ReadFunnelSource("MainWindow.cpp");
  const std::string start = FunnelBody(window, kStartToTarget);
  UR_EXPECT_TRUE(!start.empty());
  UR_EXPECT_TRUE(FunnelInOrder(start, {"host_.StartTunnel(",
                                       "if (result == TunnelStartResult::Started) {",
                                       "if (IsBestAvailableSelected(target)) {",
                                       "host_.ConnectBestAvailable();", "} else {",
                                       "host_.Connect(target);"}));
  UR_EXPECT_FALSE(FunnelHas(start, "SelectedLocation("));
  UR_EXPECT_FALSE(FunnelHas(start, "connectDestination"));
}

// Every location row clicks through SdkHost::ConnectFromRow, never straight
// into the connect controller.
UR_TEST(ConnectFunnelWiring_EveryRowConnectsFromTheRow) {
  const std::pair<const char*, int> rows[] = {{"NetworkPage.cpp", 4}, {"LocationsSheet.cpp", 3}};
  for (const auto& [file, count] : rows) {
    const std::string source = ReadFunnelSource(file);
    UR_EXPECT_TRUE(!source.empty());
    UR_EXPECT_FALSE(FunnelHas(source, "host_.Connect("));
    UR_EXPECT_FALSE(FunnelHas(source, "host_.ConnectBestAvailable("));
    int found = 0;
    for (size_t at = source.find("host_.ConnectFromRow("); at != std::string::npos;
         at = source.find("host_.ConnectFromRow(", at + 1)) {
      ++found;
    }
    if (found != count) UR_FAIL(std::string(file) + ": " + std::to_string(found) + " row connects");
  }
}

// A row click runs the window's start path to the row's location, which
// retires a disconnect the user is no longer waiting on, as the tray does.
UR_TEST(ConnectFunnelWiring_ARowClickStartsThroughTheWindow) {
  const std::string host = ReadFunnelSource("SdkHost.cpp");
  const std::string run = FunnelBody(host, "void SdkHost::RunRowConnect(");
  UR_EXPECT_TRUE(FunnelHas(run, "rowConnect_(location);"));
  const std::string window = ReadFunnelSource("MainWindow.cpp");
  UR_EXPECT_TRUE(FunnelInOrder(window, {"host_.SetRowConnect([this](const std::optional<"
                                        "urnet::ConnectLocation>& location) {",
                                        "connectPage_->ClearDisconnectIntent();",
                                        "StartTunnelUi(\"location row\", location);"}));
  // the hook reads the window, so it goes with it
  const std::string destructor = FunnelBody(window, "MainWindow::~MainWindow() {");
  UR_EXPECT_TRUE(FunnelHas(destructor, "host_.SetRowConnect(nullptr);"));
}

// The machine back first: stop_tunnel before the connect controller's
// disconnect, and on quit before the device's view controllers close.
UR_TEST(ConnectFunnelWiring_DisconnectStopsTheTunnelFirst) {
  const std::string host = ReadFunnelSource("SdkHost.cpp");
  const std::string disconnect = FunnelBody(host, "void SdkHost::Disconnect() {");
  UR_EXPECT_TRUE(FunnelInOrder(disconnect, {"control_.StopTunnel();", "connectVc_->disconnect();",
                                            "controller.disconnect();",
                                            "PublishConnectReading();"}));
  const std::string shutdown = FunnelBody(host, "void SdkHost::Shutdown() {");
  UR_EXPECT_TRUE(FunnelInOrder(shutdown, {"control_.StopTunnel();", "TeardownDeviceLocked();"}));
}

// Each call names its gesture with a literal, and the start logs it: the
// window's starts, and the host's own rebuild after a stale device.
UR_TEST(ConnectFunnelWiring_EveryStartNamesItsReason) {
  struct Site {
    const char* file;
    const char* call;
  };
  for (const Site& site :
       {Site{"MainWindow.cpp", "StartTunnelUi("}, Site{"SdkHost.cpp", "StartTunnelLocked("}}) {
    const std::string source = ReadFunnelSource(site.file);
    int calls = 0;
    for (size_t at = source.find(site.call); at != std::string::npos;
         at = source.find(site.call, at + 1)) {
      const std::string rest = source.substr(at + std::string(site.call).size(), 32);
      // a definition or a delegation passes its own `reason` on
      if (rest.rfind("const char* reason", 0) == 0 || rest.rfind("reason", 0) == 0) continue;
      ++calls;
      if (rest.empty() || rest[0] != '"') {
        UR_FAIL(std::string(site.file) + ": " + site.call + rest.substr(0, 20) +
                " names no reason");
      }
    }
    if (calls == 0) UR_FAIL(std::string(site.file) + ": no " + site.call + " call");
  }
  // the window's one start passes its gesture's reason on
  const std::string window = ReadFunnelSource("MainWindow.cpp");
  UR_EXPECT_TRUE(FunnelHas(FunnelBody(window, kStartToTarget), "host_.StartTunnel(reason);"));
  UR_EXPECT_EQ(window.find("host_.StartTunnel("), window.rfind("host_.StartTunnel("));
  const std::string start =
      FunnelBody(ReadFunnelSource("SdkHost.cpp"),
                 "TunnelStartResult SdkHost::StartTunnelLocked(const char* reason) {");
  UR_EXPECT_TRUE(FunnelHas(start, "g_message(\"connect: start_tunnel requested (%s)\", reason);"));
}
