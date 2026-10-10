// Every row of the Account pane carries a leading icon (REVOKE-UI-FINAL.md
// §1.2: the Sessions row gets the face profile, and so the list stays
// consistent, every other Account row an icon too; Linux's rows had none).
// The icons are Material Design Icons glyphs drawn with Cairo
// (BrandIcons.hpp GlyphIcon), each the closest meaning to the row:
// android's and apple's icon where those apps have the row (Profile's user
// in a circle, Referrals' heart). AccountPage needs GTK and the SDK, so this
// reads its sources.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <utility>
#include <vector>

#include "WiringSource.hpp"

using urnw::testing::wiring::Contains;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

namespace {

// The argument text of every call to `name` in `code`, parentheses balanced.
std::vector<std::string> CallArguments(const std::string& code, const std::string& name) {
  std::vector<std::string> calls;
  const std::string open = name + "(";
  for (size_t at = code.find(open); at != std::string::npos; at = code.find(open, at + 1)) {
    const size_t start = at + open.size();
    int depth = 1;
    size_t i = start;
    for (; i < code.size() && depth > 0; ++i) {
      if (code[i] == '(') ++depth;
      if (code[i] == ')') --depth;
    }
    calls.push_back(code.substr(start, i - start - 1));
  }
  return calls;
}

// The Account pane's row builders (pane B: profile, security, referrals and
// the danger group; the login methods are rendered apart).
const char* const kPaneBuilders[] = {
    "void AccountPage::BuildProfileGroup(", "void AccountPage::BuildSecurityGroup(",
    "void AccountPage::BuildReferralsRow(", "void AccountPage::BuildDangerGroup(",
    "void AccountPage::RenderAuthMethods(",
};

// The row species and the argument that carries the icon.
const std::pair<const char*, const char*> kRowCalls[] = {
    {"kit::MakePaneTwoLineRowButton", "RowIcon(MdiGlyph::"},
    {"kit::MakePaneTwoLineRow", "RowIcon(MdiGlyph::"},
    {"AddButtonRow", "MdiGlyph::"},
    {"AddValueActionRow", "MdiGlyph::"},
};

}  // namespace

UR_TEST(AccountRowIcons_EveryAccountPaneRowHasAnIcon) {
  const std::string page = ReadCode("AccountPage.cpp");
  UR_EXPECT_TRUE_MSG("AccountPage.cpp was read", !page.empty());
  size_t rows = 0;
  for (const char* builder : kPaneBuilders) {
    const std::string body = FunctionBody(page, builder);
    UR_EXPECT_TRUE_MSG(builder, !body.empty());
    for (const auto& [call, icon] : kRowCalls) {
      for (const std::string& arguments : CallArguments(body, call)) {
        ++rows;
        UR_EXPECT_TRUE_MSG(std::string(builder) + " " + call + "(" + arguments + ")",
                           Contains(arguments, icon));
      }
    }
  }
  // name, password, methods, add, auth code, client id, referrals, sign out,
  // delete
  UR_EXPECT_TRUE(rows >= 9);
  // ...and the two helpers hand their glyph to the kit
  for (const char* helper : {"Gtk::Button* AddButtonRow(", "ValueActionRow AddValueActionRow("}) {
    const std::string body = FunctionBody(page, helper);
    UR_EXPECT_TRUE_MSG(helper, Contains(body, "kRowTall, RowIcon(glyph))"));
  }
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "Gtk::Widget* RowIcon(MdiGlyph glyph)"),
                          "Gtk::make_managed<GlyphIcon>(glyph, kit::kRowIconPx)"));
}

UR_TEST(AccountRowIcons_EachRowWearsTheGlyphOfItsMeaning) {
  const std::string page = ReadCode("AccountPage.cpp");
  const std::pair<const char*, const char*> rows[] = {
      {"T_(\"network_name_label\", \"Network name\")", "MdiGlyph::AccountCircle"},
      {"T_(\"update_password\", \"Update password\")", "MdiGlyph::LockReset"},
      {"T_(\"site_app_login_methods\", \"Login methods\")", "MdiGlyph::Login"},
      {"T_(\"auth_code\", \"Auth code\")", "MdiGlyph::Barcode"},
      {"T_(\"client_id\", \"Client ID\")", "MdiGlyph::Identifier"},
      {"T_(\"referrals\", \"Referrals\")", "MdiGlyph::Heart"},
      {"T_(\"sign_out\", \"Sign out\")", "MdiGlyph::Logout"},
      {"T_(\"delete_account_2\", \"Delete account\")", "MdiGlyph::Delete"},
  };
  for (const auto& [label, glyph] : rows) {
    bool found = false;
    for (const char* builder : kPaneBuilders) {
      const std::string body = FunctionBody(page, builder);
      for (const auto& [call, icon] : kRowCalls) {
        (void)icon;
        for (const std::string& arguments : CallArguments(body, call)) {
          if (Contains(arguments, label) && Contains(arguments, glyph)) found = true;
        }
      }
    }
    UR_EXPECT_TRUE_MSG(std::string(label) + " wears " + glyph, found);
  }
  // each login method is a key
  const std::string methods = FunctionBody(page, "void AccountPage::RenderAuthMethods(");
  bool key = false;
  for (const std::string& arguments : CallArguments(methods, "AddButtonRow")) {
    key = key || Contains(arguments, "MdiGlyph::Key");
  }
  UR_EXPECT_TRUE(key);
}

UR_TEST(AccountRowIcons_TheKitPrependsAMutedDecorativeIcon) {
  const std::string kit = ReadCode("PaneKit.cpp");
  const std::string prepend = FunctionBody(kit, "void PrependLeadingIcon(");
  UR_EXPECT_TRUE_MSG("PaneKit.cpp defines PrependLeadingIcon", !prepend.empty());
  UR_EXPECT_TRUE(Contains(prepend, "if (leading == nullptr) return;"));
  UR_EXPECT_TRUE(Contains(prepend, "leading->add_css_class(\"ur-row-icon\");"));
  UR_EXPECT_TRUE(Contains(prepend, "MarkDecorative(*leading);"));
  UR_EXPECT_TRUE(Contains(prepend, "row.prepend(*leading);"));
  UR_EXPECT_TRUE(Contains(FunctionBody(kit, "PaneTwoLineRow MakePaneTwoLineRow("),
                          "PrependLeadingIcon(*inner, leading, kRowIconGap);"));
  UR_EXPECT_TRUE(Contains(FunctionBody(kit, "PaneTwoLineRowButton MakePaneTwoLineRowButton("),
                          "PrependLeadingIcon(*grid, leading, kRowIconGap - kRowButtonGridSpacing);"));
  // the muted tint android and apple give the rows' icons
  UR_EXPECT_TRUE(Contains(ReadCode("UrTheme.cpp"), ".ur-row-icon { color: #989898; }"));
}

// The Profile group's auth and status lines and the login methods' state line
// have no icon; they sit in the titles' column.
UR_TEST(AccountRowIcons_TextLinesStayInTheTitlesColumn) {
  const std::string header = ReadCode("PaneKit.hpp");
  UR_EXPECT_TRUE(Contains(header, "inline constexpr int kRowIconInset = kRowIconPx + kRowIconGap;"));
  const std::string page = ReadCode("AccountPage.cpp");
  const std::string profile = FunctionBody(page, "void AccountPage::BuildProfileGroup(");
  UR_EXPECT_TRUE(Contains(profile, "authText_->set_margin_start(kit::kRowIconInset);"));
  UR_EXPECT_TRUE(Contains(profile, "statusLine_->set_margin_start(kit::kRowIconInset);"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void AccountPage::RenderAuthMethods("),
                          "prose.line->set_margin_start(kit::kRowIconInset);"));
}
