// The location chooser's rows (LocationsSheet.cpp) are buttons: in the tab
// order, picked with Enter or Space, and a named button to a screen reader,
// as the Network page's rows already were. They used to be boxes with a
// click gesture, which a keyboard could not reach and a screen reader read
// as text. The sheet needs GTK and the SDK, so this reads its source with
// the comments blanked; the name itself is LocationRowNameTest.cpp's.
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
std::string ReadChooserSource(const std::string& relative) {
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
std::string ChooserBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool ChooserHas(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// Every needle occurs, in this order.
bool ChooserInOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// The shell every row is built on is a button; what it holds is hidden from
// the accessibility tree, because the button's name says all of it.
UR_TEST(LocationsSheetWiring_TheRowShellIsAButton) {
  const std::string sheet = ReadChooserSource("LocationsSheet.cpp");
  UR_EXPECT_TRUE(!sheet.empty());
  UR_EXPECT_FALSE(ChooserHas(sheet, "GestureClick"));
  const std::string shell = ChooserBody(sheet, "RowShell MakeRowShell(");
  UR_EXPECT_TRUE(ChooserInOrder(shell, {"shell.button = Gtk::make_managed<Gtk::Button>();",
                                        "shell.button->add_css_class(\"ur-card-tappable\");",
                                        "shell.button->set_child(*shell.content);",
                                        "kit::MarkDecorative(*dot);",
                                        "kit::MarkDecorative(*column);", "return shell;"}));
  UR_EXPECT_TRUE(ChooserHas(ChooserBody(sheet, "Gtk::Image* MakeTrailingIcon("),
                            "kit::MarkDecorative(*icon);"));
}

// Each of the three rows is named and connects on the button's click.
UR_TEST(LocationsSheetWiring_EveryRowIsANamedButton) {
  const std::string sheet = ReadChooserSource("LocationsSheet.cpp");
  for (const char* signature : {"Gtk::Button* LocationsSheet::MakeLocationRow(",
                                "Gtk::Button* LocationsSheet::MakePeerRow(",
                                "Gtk::Button* LocationsSheet::MakeBestAvailableRow("}) {
    const std::string row = ChooserBody(sheet, signature);
    if (row.empty()) {
      UR_FAIL(std::string("missing ") + signature);
      continue;
    }
    if (!ChooserInOrder(row, {"MakeRowShell(", "kit::SetAccessibleLabel(*row.button, LocationRowName(",
                              "row.button->signal_clicked().connect(", "host_.ConnectFromRow(",
                              "set_visible(false);", "return row.button;"})) {
      UR_FAIL(std::string(signature) + " is not a named button that connects on its click");
    }
  }
}

// The chooser reads a row's states with the Network page's words, through
// the one name the two lists share.
UR_TEST(LocationsSheetWiring_TheListsShareTheRowName) {
  const std::string sheet = ReadChooserSource("LocationsSheet.cpp");
  const std::string page = ReadChooserSource("NetworkPage.cpp");
  for (const std::string* source : {&sheet, &page}) {
    UR_EXPECT_TRUE(ChooserHas(*source, "#include \"LocationRowName.hpp\""));
    UR_EXPECT_TRUE(ChooserInOrder(*source, {"T_(\"unstable_providers_warning\", \"* (may be unstable)\")",
                                            "T_(\"strong_anonymization\", \"Strong Anonymization\")",
                                            "T_(\"network_peers\", \"Network peers\")",
                                            "T_(\"selected_provider\", \"Selected provider\")"}));
  }
  UR_EXPECT_TRUE(ChooserHas(ChooserBody(page, "Gtk::Button* NetworkPage::MakeRow("),
                            "kit::SetAccessibleLabel(*row.root, LocationRowName(title, meta, states, words));"));
}

// A button row keeps the look of the box it replaced: the platform button's
// fill, padding and bold face are off.
UR_TEST(LocationsSheetWiring_TheButtonRowWearsNoButtonChrome) {
  const std::string css = ReadChooserSource("Ui.cpp");
  UR_EXPECT_TRUE(ChooserInOrder(css, {"button.ur-card-tappable { background: none; box-shadow: none; padding: 0;",
                                      "min-height: 0; min-width: 0; font-weight: normal; }"}));
}
