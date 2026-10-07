// The provide indicator (ProvideModeGlyph.hpp): green for the Network tier,
// a ring for Public (amber while paused), and a neutral muted dot when not
// providing, which is a setting and not an error. The colours are Ui.hpp's
// tokens, read from its source, so the rule's hex cannot drift from them.
// The Connect page and the Earnings page draw by the one rule; they need GTK,
// so the wiring case reads their sources.
// SPDX-License-Identifier: MPL-2.0
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "ProvideModeGlyph.hpp"
#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadGlyphSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// A Ui.hpp colour token as markup hex: "#87FB67" for
// `kUrGreen{0x87 / 255.0, 0xFB / 255.0, 0x67 / 255.0, 1.0}`.
std::string UiToken(const std::string& name) {
  const std::string ui = ReadGlyphSource("Ui.hpp");
  const size_t start = ui.find("inline constexpr Rgba " + name + "{");
  const size_t end = start == std::string::npos ? start : ui.find('}', start);
  std::string hex = "#";
  for (size_t at = start; hex.size() < 7;) {
    at = at == std::string::npos ? at : ui.find("0x", at);
    if (at == std::string::npos || end < at) {
      UR_FAIL("Ui.hpp has no hex token " + name);
      return std::string();
    }
    char byte[3];
    std::snprintf(byte, sizeof(byte), "%02X",
                  static_cast<unsigned>(std::strtoul(ui.substr(at + 2, 2).c_str(), nullptr, 16)));
    hex += byte;
    at += 2;
  }
  return hex;
}

bool GlyphIs(const char* glyph, const char* hex, const ProvideModeGlyph& visual) {
  if (std::string(glyph) == visual.glyph && std::string(hex) == visual.colorHex) return true;
  UR_FAIL(std::string("got ") + visual.glyph + " " + visual.colorHex);
  return false;
}

}  // namespace

UR_TEST(ProvideModeGlyph_NetworkTiersAreAGreenDot) {
  const std::string green = UiToken("kUrGreen");
  UR_EXPECT_TRUE(green == "#87FB67");
  GlyphIs("●", green.c_str(), ProvideModeGlyphFor(1, false));
  GlyphIs("●", green.c_str(), ProvideModeGlyphFor(2, false));
  // pause stops public provide only
  GlyphIs("●", green.c_str(), ProvideModeGlyphFor(1, true));
}

UR_TEST(ProvideModeGlyph_PublicIsARingAmberWhilePaused) {
  GlyphIs("◉", UiToken("kUrGreen").c_str(), ProvideModeGlyphFor(3, false));
  GlyphIs("◉", UiToken("kUrAmber").c_str(), ProvideModeGlyphFor(3, true));
}

// Not providing is muted, never the danger coral; an unknown mode reads as
// not providing.
UR_TEST(ProvideModeGlyph_NotProvidingIsMuted) {
  const std::string muted = UiToken("kUrTextMuted");
  GlyphIs("●", muted.c_str(), ProvideModeGlyphFor(0, false));
  GlyphIs("●", muted.c_str(), ProvideModeGlyphFor(0, true));
  GlyphIs("●", muted.c_str(), ProvideModeGlyphFor(7, false));
  GlyphIs("●", muted.c_str(), ProvideModeGlyphFor(-1, false));
}

// Both indicators draw by the rule; neither keeps a switch of its own.
UR_TEST(ProvideModeGlyph_BothPagesDrawByTheRule) {
  for (const char* file : {"ConnectPage.cpp", "EarningsPage.cpp"}) {
    const std::string source = ReadGlyphSource(file);
    UR_EXPECT_TRUE(!source.empty());
    if (source.find("ProvideModeGlyphFor(stats.provideMode, stats.providePaused)") ==
        std::string::npos) {
      UR_FAIL(std::string(file) + " does not draw its provide dot by ProvideModeGlyphFor");
    }
    if (source.find("switch (stats.provideMode)") != std::string::npos) {
      UR_FAIL(std::string(file) + " keeps its own provide mode switch");
    }
  }
}
