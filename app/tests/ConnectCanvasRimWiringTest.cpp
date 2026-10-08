// The resting hero's structure (ConnectCanvas::DrawCanvas): two orbits and a
// halo behind the disconnected core, in the idle layer so only Disconnected
// shows them, and the globe's rim drawn after the clip in every state. The
// canvas needs GTK and cairo, so this reads its source.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadCanvasSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it; empty when it is gone.
std::string CanvasBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Every needle occurs, in this order.
bool CanvasInOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

UR_TEST(ConnectCanvasRimWiring_OrbitsAndHaloRestBehindTheCore) {
  const std::string canvas = ReadCanvasSource("ConnectCanvas.cpp");
  UR_EXPECT_TRUE(canvas.find("constexpr double kOrbit1D = 128.0, kOrbit2D = 192.0, kHaloD = 64.0;") !=
                 std::string::npos);
  UR_EXPECT_TRUE(canvas.find("constexpr double kOrbitAlpha = 0.08, kHaloAlpha = 0.35, kRimAlpha = 0.12;") !=
                 std::string::npos);
  const std::string draw = CanvasBody(canvas, "void ConnectCanvas::DrawCanvas(");
  UR_EXPECT_TRUE(CanvasInOrder(
      draw, {"if (idleOpacity_ > 0.01) {", "cr->push_group();", "cr->set_line_width(1.0);",
             "cr->set_source_rgba(1, 1, 1, kOrbitAlpha);", "kOrbit1D * s / 2.0", "kOrbit2D * s / 2.0",
             "kHaloAlpha);", "kHaloD * s / 2.0", "if (pulseOpacity_ > 0.001) {",
             "kCoreRingD * s / 2.0", "cr->pop_group_to_source();",
             "cr->paint_with_alpha(idleOpacity_);"}));
}

// The rim is stroked after the clip is restored, under the hover lift, in
// every state.
UR_TEST(ConnectCanvasRimWiring_TheRimIsDrawnAfterTheClip) {
  const std::string draw =
      CanvasBody(ReadCanvasSource("ConnectCanvas.cpp"), "void ConnectCanvas::DrawCanvas(");
  UR_EXPECT_TRUE(CanvasInOrder(
      draw, {"cr->clip();", "if (glyphOpacity_ > 0.01) {", "cr->restore();", "cr->save();",
             "cr->scale(hoverScale_, hoverScale_);", "AddGlobePath(cr, ox, oy, side);",
             "cr->set_source_rgba(1, 1, 1, kRimAlpha);", "cr->set_line_width(1.0);",
             "cr->stroke();", "cr->restore();"}));
}
