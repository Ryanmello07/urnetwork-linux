// Every connect gesture's path to a provider, as Windows takes it: the
// Connect button and the tray go where the provider row says (the selected
// location, or the best available with none), read before the start, because
// a start that builds a new device answers SelectedLocation from that device.
// Before, the press ran ConnectBestAvailable whatever was selected. MainWindow
// needs gtkmm and the SDK, so this reads its source with the comments blanked.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>
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
    "TunnelStartResult MainWindow::StartTunnelUi(const std::optional<urnet::ConnectLocation>& "
    "target) {";

}  // namespace

// The Connect button and the tray connect through the start path to the
// selection, and issue no connect of their own.
UR_TEST(ConnectFunnelWiring_TheButtonConnectsToTheSelection) {
  const std::string window = ReadFunnelSource("MainWindow.cpp");
  const std::string toggle = FunnelBody(window, "void MainWindow::ToggleConnect(bool disconnect) {");
  UR_EXPECT_TRUE(!toggle.empty());
  const std::string connectHalf = toggle.substr(toggle.find("ClearDisconnectIntent()"));
  UR_EXPECT_TRUE(FunnelHas(connectHalf, "StartTunnelUi();"));
  UR_EXPECT_FALSE(FunnelHas(connectHalf, "ConnectBestAvailable"));
  UR_EXPECT_FALSE(FunnelHas(connectHalf, "host_.Connect("));
  // the selection is read before the start, and handed to it
  const std::string selected = FunnelBody(window, "TunnelStartResult MainWindow::StartTunnelUi() {");
  UR_EXPECT_TRUE(FunnelHas(selected, "return StartTunnelUi(host_.SelectedLocation());"));
}

// After the start, the target or the best available, and no second read of
// the selection from the device the start may have just built.
UR_TEST(ConnectFunnelWiring_TheStartConnectsItsTarget) {
  const std::string window = ReadFunnelSource("MainWindow.cpp");
  const std::string start = FunnelBody(window, kStartToTarget);
  UR_EXPECT_TRUE(!start.empty());
  UR_EXPECT_TRUE(FunnelInOrder(start, {"host_.StartTunnel(", "if (result == TunnelStartResult::Started) {",
                                       "if (IsBestAvailableSelected(target)) {",
                                       "host_.ConnectBestAvailable();", "} else {",
                                       "host_.Connect(target);"}));
  UR_EXPECT_FALSE(FunnelHas(start, "SelectedLocation("));
  UR_EXPECT_FALSE(FunnelHas(start, "connectDestination"));
}
