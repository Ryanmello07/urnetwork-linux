// Click-to-copy text is a button (Ui.cpp MakeCopyTextButton): the contracts
// sheet's client ids, the provider locations sheet's client ids and the
// provider identities' key hashes and client ids. Each was a label with a
// click gesture, which a keyboard could not reach and a screen reader read
// as plain text. The sheets need GTK and the SDK, so this reads their
// sources with the comments blanked.
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
std::string ReadCopySource(const std::string& relative) {
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
std::string CopyBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

size_t CopyCount(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

// Every needle occurs, in this order.
bool CopyInOrder(const std::string& text, std::initializer_list<const char*> needles) {
  size_t at = 0;
  for (const char* needle : needles) {
    at = text.find(needle, at);
    if (at == std::string::npos) return false;
    at += 1;
  }
  return true;
}

}  // namespace

// The button is named by its text and described by the hint, hides the label
// it names, and copies on its click.
UR_TEST(CopyTextButtonWiring_TheButtonIsNamedByItsText) {
  const std::string ui = ReadCopySource("Ui.cpp");
  UR_EXPECT_TRUE(CopyInOrder(
      CopyBody(ui, "Gtk::Button* MakeCopyTextButton("),
      {"button->add_css_class(\"ur-copy-text\");", "button->set_child(label);",
       "button->set_tooltip_text(hint);", "GTK_ACCESSIBLE_PROPERTY_LABEL",
       "label.get_text().c_str(), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION",
       "GTK_ACCESSIBLE_STATE_HIDDEN", "button->signal_clicked().connect("}));
  UR_EXPECT_TRUE(CopyInOrder(ui, {"button.ur-copy-text { background: none; box-shadow: none; padding: 0;",
                                  "button.ur-copy-text:hover {"}));
}

// Every click-to-copy text in the three sheets is one of those buttons, and
// none is a label with a gesture any more. The provider locations sheet keeps
// the one gesture that selects its row.
UR_TEST(CopyTextButtonWiring_EveryCopyTextIsAButton) {
  const std::pair<const char*, size_t> sheets[] = {
      {"ContractsSheet.cpp", 1}, {"ProviderLocationsSheet.cpp", 1}, {"PostQuantumIdentity.cpp", 1}};
  for (const auto& [file, copies] : sheets) {
    const std::string source = ReadCopySource(file);
    UR_EXPECT_TRUE(!source.empty());
    if (CopyCount(source, "MakeCopyTextButton(") != copies) {
      UR_FAIL(std::string(file) + ": expected " + std::to_string(copies) + " copy button builder");
    }
    if (CopyCount(source, "T_(\"copy_to_clipboard\", \"Copy to Clipboard\")") != copies) {
      UR_FAIL(std::string(file) + ": the copy hint is not the store's");
    }
  }
  UR_EXPECT_TRUE(ReadCopySource("ContractsSheet.cpp").find("GestureClick") == std::string::npos);
  UR_EXPECT_TRUE(ReadCopySource("PostQuantumIdentity.cpp").find("GestureClick") ==
                 std::string::npos);
  UR_EXPECT_TRUE(ReadCopySource("PostQuantumIdentity.cpp").find("MakeClickable") ==
                 std::string::npos);
  const std::string provider = ReadCopySource("ProviderLocationsSheet.cpp");
  UR_EXPECT_TRUE(CopyCount(provider, "GestureClick::create()") == 1);
  UR_EXPECT_TRUE(CopyInOrder(provider, {"auto gesture = Gtk::GestureClick::create();",
                                        "[this, clientId](int, double, double) { Select(clientId); });"}));
  // the identities sheet copies both its hash and its client id through it
  const std::string identities = ReadCopySource("PostQuantumIdentity.cpp");
  UR_EXPECT_TRUE(CopyInOrder(identities, {"column->append(*copyable(hashLabel, row.hash, /*isHash=*/true));",
                                          "column->append(*copyable(idLabel, row.clientId, /*isHash=*/false));"}));
}
