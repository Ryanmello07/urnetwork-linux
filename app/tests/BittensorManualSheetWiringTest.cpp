// The manual proof sheet (BittensorManualSheet.cpp) shows the exact message to
// sign above Copy, the address and signature fields and Continue, and the sheet
// takes its content's height. A payout consent is about 2 KB of JSON: laid out
// in full it made the sheet taller than a laptop screen, with the fields and
// Continue past its edge, so a consent could not be signed by hand (support
// inbox 8058, 8073). The sheet needs GTK and the SDK, so this reads its source
// with the comments blanked.
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

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadManualSheetSource(const std::string& relative) {
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

// From the definition that starts with `signature` to the closing brace in
// column 0 that ends it; empty when it is gone.
std::string ManualSheetBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Every needle occurs, in this order.
bool ManualSheetInOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// The message scrolls in a box of bounded height that takes a short message's
// own height, and Copy, the fields and Continue follow the box.
UR_TEST(BittensorManualSheetWiring_TheMessageScrollsInABoundedBox) {
  const std::string source = ReadManualSheetSource("BittensorManualSheet.cpp");
  const std::string sheet =
      ManualSheetBody(source, "BittensorManualSheet::BittensorManualSheet(Gtk::Window& parent");
  UR_EXPECT_FALSE(sheet.empty());
  UR_EXPECT_TRUE(source.find("constexpr int kMessageMaxHeight = ") != std::string::npos);
  // never the message straight into the sheet's column
  UR_EXPECT_TRUE(sheet.find("column->append(*message)") == std::string::npos);
  UR_EXPECT_TRUE(ManualSheetInOrder(
      sheet, {"Gtk::make_managed<Gtk::Label>(request.message)",
              "messageScroller = Gtk::make_managed<Gtk::ScrolledWindow>()",
              "messageScroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC)",
              "messageScroller->set_propagate_natural_height(true)",
              "messageScroller->set_max_content_height(kMessageMaxHeight)",
              "messageScroller->set_child(*message)", "column->append(*messageScroller)",
              "T_(\"copy\", \"Copy\")", "signatureEntry_ = Gtk::make_managed<Gtk::Entry>()",
              "T_(\"continue_txt\", \"Continue\")"}));
}
