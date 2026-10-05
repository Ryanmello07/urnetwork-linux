// The bootstrap DNS-over-HTTPS servers' GTK and SDK call sites, read as
// source the way VlessPresentationTest reads the VLESS sheet's: every text the
// section shows is the catalog's own entry, its buttons do what every platform
// does, Account > Extenders hosts it without gating it on the view controller
// and the login screen's network sheet opens it, the host saves through the
// space's own setter, and the import sheet names the servers a code would set.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string Source(const std::string& relative) {
  return ReadFile(std::string(UR_SRC_DIR) + "/" + relative);
}

// The body of `signature` (up to the next top-level definition), "" if absent.
std::string Body(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Has(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// Every T_("key", "English") call in `source`: the key and the English
// literal, which these sources write whole.
std::vector<std::pair<std::string, std::string>> Texts(const std::string& source) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t at = 0;
  while ((at = source.find("T_(\"", at)) != std::string::npos) {
    const size_t keyStart = at + 4;
    const size_t keyEnd = source.find('"', keyStart);
    const size_t open = keyEnd == std::string::npos ? keyEnd : source.find('"', keyEnd + 1);
    if (open == std::string::npos) break;
    size_t close = open + 1;
    while (close < source.size() && source[close] != '"') close += source[close] == '\\' ? 2 : 1;
    out.emplace_back(source.substr(keyStart, keyEnd - keyStart),
                     source.substr(open + 1, close - open - 1));
    at = close;
  }
  return out;
}

bool IsControlDohKey(const std::string& key) {
  return key.rfind("control_doh_", 0) == 0 || key == "import_extenders_control_doh_urls";
}

}  // namespace

// The English is the msgid T_ looks the translation up by, so each text has to
// be the catalog's entry byte for byte -- in the section and wherever else the
// servers' texts are shown.
UR_TEST(ControlDohWiring_EveryTextIsTheCatalogs) {
  const std::string catalog = ReadFile(std::string(UR_SRC_DIR) + "/../po/en.po");
  UR_EXPECT_TRUE(!catalog.empty());
  std::set<std::string> keys;
  for (const auto& [key, english] : Texts(Source("ControlDohSection.cpp"))) {
    keys.insert(key);
    const std::string entry = "msgctxt \"" + key + "\"\nmsgid \"" + english + "\"\n";
    UR_EXPECT_TRUE_MSG(entry, Has(catalog, entry));
  }
  for (const char* key :
       {"control_doh_urls", "control_doh_urls_description", "control_doh_urls_hint",
        "control_doh_use_china", "control_doh_use_china_hint", "control_doh_urls_reset", "save",
        "something_went_wrong"}) {
    UR_EXPECT_TRUE_MSG(std::string("the section does not show ") + key, keys.count(key) == 1);
  }
  int elsewhere = 0;
  for (const char* file : {"NetworkServerSheet.cpp", "ExtenderImportSheet.cpp"}) {
    for (const auto& [key, english] : Texts(Source(file))) {
      if (!IsControlDohKey(key)) continue;
      ++elsewhere;
      const std::string entry = "msgctxt \"" + key + "\"\nmsgid \"" + english + "\"\n";
      UR_EXPECT_TRUE_MSG(std::string(file) + ": " + entry, Has(catalog, entry));
    }
  }
  UR_EXPECT_EQ(2, elsewhere);
}

// Save hands the box's lines to the host and reads back what was stored; the
// preset fills the box from the SDK and saves nothing; "Use built-in servers
// only" clears the box and saves the empty list.
UR_TEST(ControlDohWiring_TheSectionsButtons) {
  const std::string section = Source("ControlDohSection.cpp");
  UR_EXPECT_TRUE(
      Has(section, "Save(extender::SplitControlDohLines(urls_->get_buffer()->get_text().raw()));"));
  const std::string save = Body(section, "void ControlDohSection::Save(");
  UR_EXPECT_TRUE(Has(save, "host_.SetControlDohUrls(urls)"));
  UR_EXPECT_TRUE(Has(save, "extender::ControlDohSaveOutcomeFor(*answer)"));
  UR_EXPECT_TRUE_MSG("a save reads back what the SDK stored", Has(save, "host_.GetControlDohUrls()"));
  const std::string china = Body(section, "void ControlDohSection::UseChinaResolvers(");
  UR_EXPECT_TRUE(Has(china, "SdkHost::RegionalControlDohUrls(extender::kControlDohChinaCountryCode)"));
  UR_EXPECT_TRUE(Has(china, "SetBox(extender::JoinHostLines(preset));"));
  UR_EXPECT_TRUE_MSG("the preset saves nothing", !Has(china, "Save(") && !Has(china, "SetControlDohUrls"));
  UR_EXPECT_TRUE(Has(section, "SetBox(\"\");\n    Save({});"));
  UR_EXPECT_TRUE(Has(Body(section, "void ControlDohSection::Load("), "host_.GetControlDohUrls()"));
}

// Account > Extenders shows the section inline and reloads it with the rest,
// never gated on the view controller (the servers are the GUI's own space's);
// the login screen's network sheet opens it in its own sheet, which reads the
// servers again on every open.
UR_TEST(ControlDohWiring_BothDoorsShowTheSection) {
  const std::string extenders = Source("ExtenderSection.cpp");
  UR_EXPECT_TRUE(Has(Body(extenders, "ExtenderSection::ExtenderSection("), "BuildControlDoh(*this);"));
  UR_EXPECT_TRUE(Has(Body(extenders, "void ExtenderSection::BuildControlDoh("),
                     "Gtk::make_managed<ControlDohSection>(host_, /*withTitle=*/true)"));
  UR_EXPECT_TRUE(Has(Body(extenders, "void ExtenderSection::Load("), "controlDoh_->Load();"));
  UR_EXPECT_TRUE_MSG("the servers are gated on the view controller",
                     !Has(Body(extenders, "void ExtenderSection::ApplyEnabled("), "controlDoh_"));

  const std::string login = Source("NetworkServerSheet.cpp");
  UR_EXPECT_TRUE(Has(login, "MakePaneTwoLineRowButton(T_(\"control_doh_urls\", \"Bootstrap DNS-over-HTTPS servers\"))"));
  UR_EXPECT_TRUE(Has(login, "signal_clicked().connect([this] { OpenControlDoh(); });"));
  const std::string open = Body(login, "void NetworkServerSheet::OpenControlDoh(");
  UR_EXPECT_TRUE(Has(open, "std::make_unique<ControlDohSheet>(*this, sdk_)"));
  UR_EXPECT_TRUE(Has(open, "controlDohSheet_->Open();"));
  UR_EXPECT_TRUE(Has(Body(Source("ControlDohSection.cpp"), "void ControlDohSheet::Open("),
                     "section_->Load();"));

  // the new source is a translation source
  UR_EXPECT_TRUE(Has(ReadFile(std::string(UR_SRC_DIR) + "/../po/POTFILES.in"),
                     "\nsrc/ControlDohSection.cpp\n"));
}

// The host saves through the space's own setter, which validates and applies
// in place, never a whole-values write that would rebuild the space.
UR_TEST(ControlDohWiring_TheHostSavesThroughTheSpacesSetter) {
  const std::string host = Source("SdkHost.cpp");
  UR_EXPECT_TRUE(Has(Body(host, "std::optional<std::vector<std::string>> SdkHost::GetControlDohUrls("),
                     "networkSpace_->getControlDohUrls()"));
  const std::string set = Body(host, "std::optional<std::string> SdkHost::SetControlDohUrls(");
  UR_EXPECT_TRUE(Has(set, "networkSpace_->setControlDohUrls(urnet::StringList(urls))"));
  UR_EXPECT_TRUE_MSG("a save of the servers rebuilds the space", !Has(set, "updateNetworkSpaceValues"));
  UR_EXPECT_TRUE(Has(Body(host, "std::vector<std::string> SdkHost::RegionalControlDohUrls("),
                     "urnet::regionalControlDohUrls(countryCode)"));
}

// The import sheet names the servers a code's settings would set, beside the
// switch and again in the confirmation.
UR_TEST(ControlDohWiring_TheImportSheetNamesTheServers) {
  const std::string sheet = Source("ExtenderImportSheet.cpp");
  const std::string refresh = Body(sheet, "void ExtenderImportSheet::Refresh(");
  UR_EXPECT_TRUE(Has(refresh, "extender::ControlDohImportArg("));
  UR_EXPECT_TRUE(Has(refresh, "decoded->ControlDohUrls"));
  UR_EXPECT_TRUE(Has(refresh, "controlDohLine_->set_text(ControlDohLineText(controlDohServers_))"));
  UR_EXPECT_TRUE(Has(Body(sheet, "void ExtenderImportSheet::ConfirmThenImport("),
                     "ControlDohLineText(controlDohServers_)"));
}
