// Account -> API keys as wired into the app: the Account row right after
// Sessions with MDI's "api" glyph, the window's navigation like the Sessions
// page's, the rail keeping Account lit, the requests made on the GUI's Api
// with their answers marshaled to the main loop, every delete confirmed with
// Cancel as the default before it is sent, a create sheet that stays while
// its request is out, and the new key kept only by the sheet: copied whole,
// cleared from its label when the sheet closes, never selectable, logged or
// stored. The decisions themselves are tested in ApiKeysPresentationTest and
// ApiKeysBindingTest; the page needs GTK and the SDK, so this reads its
// sources.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "MdiGlyphs.hpp"
#include "ShellLayout.hpp"
#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::CountOf;
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

}  // namespace

UR_TEST(ApiKeysWiring_TheAccountRowFollowsSessions) {
  const std::string page = ReadCode("AccountPage.cpp");
  const std::string profile = FunctionBody(page, "void AccountPage::BuildProfileGroup(");
  const std::string row =
      "kit::MakePaneTwoLineRowButton(T_(\"api_keys_title\", \"API keys\"), {}, kRowTall,\n"
      "                                             RowIcon(MdiGlyph::Api));";
  UR_EXPECT_TRUE_MSG("the API keys row with the api glyph", Contains(profile, row));
  // the row built right after Sessions, and the last of the profile group
  const size_t sessions = profile.find("T_(\"sessions_title\", \"Sessions\")");
  const size_t apiKeys = profile.find("T_(\"api_keys_title\", \"API keys\")");
  UR_EXPECT_TRUE(sessions != std::string::npos && apiKeys != std::string::npos && sessions < apiKeys);
  if (sessions != std::string::npos && apiKeys != std::string::npos && sessions < apiKeys) {
    UR_EXPECT_EQ(1u, CountOf(profile.substr(sessions, apiKeys - sessions), "kit::Make"));
    UR_EXPECT_EQ(std::string::npos, profile.find("kit::Make", apiKeys));
  }
  UR_EXPECT_TRUE(Contains(profile, "if (on_open_api_keys) {"));
  UR_EXPECT_TRUE(Contains(ReadCode("AccountPage.hpp"), "std::function<void()> on_open_api_keys;"));
  // MDI's "api", drawn from its path data like every Account row's icon
  UR_EXPECT_TRUE(std::string(urnw::kMdiGlyphs[static_cast<size_t>(urnw::MdiGlyph::Api)].mdiName) ==
                 "api");
}

UR_TEST(ApiKeysWiring_TheWindowNavigatesLikeSessions) {
  const std::string window = ReadCode("MainWindow.cpp");
  UR_EXPECT_TRUE(Contains(window, "apiKeysPage_ = Gtk::make_managed<ApiKeysPage>(host_);"));
  UR_EXPECT_TRUE(Contains(window, "shell_->SetPage(\"api_keys\", *apiKeysPage_);"));
  UR_EXPECT_TRUE(Contains(window, "accountPage_->on_open_api_keys = [this] {\n"
                                  "    if (shell_) shell_->Navigate(\"api_keys\");"));
  UR_EXPECT_TRUE(Contains(window, "apiKeysPage_->on_back = [this] {\n"
                                  "    if (shell_) shell_->Navigate(\"account\");"));
  // loads on open, through the shell's navigation (and again on a sign-in
  // while it is on screen)
  UR_EXPECT_TRUE(Contains(window, "if (tag == \"api_keys\" && apiKeysPage_) apiKeysPage_->Load();"));
  // a sign-out releases the departed account's requests and closes the sheet
  const std::string auth = FunctionBody(window, "void MainWindow::ApplyAuthState(bool loggedIn)");
  UR_EXPECT_TRUE(Contains(auth, "if (apiKeysPage_) apiKeysPage_->ResetForSignOut();"));
  // the window's one-modal gate, and the preview harness
  UR_EXPECT_TRUE(Contains(window, "apiKeysPage_->sheet_open = [this] { return sheetOpen_; };"));
  UR_EXPECT_TRUE(Contains(window, "apiKeysPage_->on_sheet_open_changed = [this](bool open) { sheetOpen_ = open; };"));
  UR_EXPECT_TRUE(Contains(window, "if (apiKeysPage_) apiKeysPage_->SetPreviewMode(true);"));
  UR_EXPECT_TRUE(Contains(window, "if (tag == \"api_keys\" && apiKeysPage_) apiKeysPage_->ShowPreviewState();"));
  UR_EXPECT_TRUE(Contains(ReadCode("MainWindow.hpp"), "ApiKeysPage* apiKeysPage_ = nullptr;"));
}

UR_TEST(ApiKeysWiring_TheRailKeepsAccountLit) {
  UR_EXPECT_TRUE(urnw::shell::RailTagFor("api_keys") == "account");
  UR_EXPECT_TRUE(urnw::shell::RailTagFor("sessions") == "account");
  UR_EXPECT_TRUE(urnw::shell::RailTagFor("account") == "account");
  UR_EXPECT_TRUE(urnw::shell::RailTagFor("settings") == "settings");
}

// The requests go to host_.api() in the GUI process, one service per Api; an
// answer is copied out on the sdk thread and only posted, and it is handed to
// the binding only while the page is alive.
UR_TEST(ApiKeysWiring_TheRequestsAreTheGuiApisAndTheirAnswersOnlyPost) {
  const std::string page = ReadCode("ApiKeysPage.cpp");
  const std::string ensure = FunctionBody(page, "void ApiKeysPage::EnsureService()");
  UR_EXPECT_TRUE(Contains(ensure, "const uint64_t source = host_.api().handle();"));
  UR_EXPECT_TRUE(Contains(ensure, "if (binding_.Attached() && binding_.Source() == source) return;"));
  UR_EXPECT_TRUE(Contains(ensure, "binding_.Attach(std::make_unique<SdkService>(host_, alive_), source);"));
  UR_EXPECT_TRUE(Contains(page, "host_.api().listApiKeys("));
  UR_EXPECT_TRUE(Contains(page, "host_.api().createApiKey("));
  UR_EXPECT_TRUE(Contains(page, "host_.api().deleteApiKey("));
  UR_EXPECT_EQ(3u, CountOf(page, "PostToMain([alive, done"));
  UR_EXPECT_TRUE(Contains(page, "if (*alive) done(std::move(answer));"));
  UR_EXPECT_TRUE(Contains(page, "if (*alive) done(answer);"));
  // the give-ups run on the main loop's timer, and not once the page is gone;
  // every change the binding makes is rendered (in the constructor, after its
  // initializers)
  UR_EXPECT_TRUE(Contains(page, "binding_(settings_, [alive = alive_](int delayMs, std::function<void()> fire) {"));
  UR_EXPECT_TRUE(Contains(page, "if (*alive) fire();"));
  UR_EXPECT_TRUE(Before(page, "ApiKeysPage::ApiKeysPage(SdkHost& host)",
                        "binding_.on_changed = [this] { Render(); };"));
  UR_EXPECT_TRUE(Before(page, "binding_.on_changed = [this] { Render(); };",
                        "ApiKeysPage::~ApiKeysPage()"));
  // alive_ is set up before the binding whose timers read it
  UR_EXPECT_TRUE(Before(ReadCode("ApiKeysPage.hpp"), "std::shared_ptr<bool> alive_",
                        "api_keys::Binding binding_;"));
}

// A result with neither a transport error nor the server's error is the
// answer. The C ABI hands the server's time over as RFC 3339 text.
UR_TEST(ApiKeysWiring_TheSdkAnswersAreCopiedInTheirUnits) {
  const std::string page = ReadCode("ApiKeysPage.cpp");
  const std::string list = FunctionBody(page, "api_keys::ListAnswer ListAnswerOf(");
  UR_EXPECT_TRUE(Contains(list, "answer.ok = result.has_value() && !err.has_value() && !result->error.has_value();"));
  UR_EXPECT_TRUE(Contains(list, "out.id = key.id.value_or(std::string());"));
  UR_EXPECT_TRUE(Contains(list, "api_keys::ParseRfc3339UnixSeconds(key.create_time.value_or(std::string())).value_or(0);"));
  const std::string create = FunctionBody(page, "api_keys::CreateAnswer CreateAnswerOf(");
  UR_EXPECT_TRUE(Contains(create, "if (result && !err && !result->error && result->api_key) {"));
  UR_EXPECT_TRUE(Contains(create, "answer.apiKey = std::move(*result->api_key);"));
  UR_EXPECT_TRUE(Contains(create, "if (result && result->api_key) api_keys::Wipe(*result->api_key);"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "api_keys::DeleteAnswer DeleteAnswerOf("),
                          "answer.ok = result.has_value() && !err.has_value() && !result->error.has_value();"));
  // the delete names its key by id, the create sends the name it was given
  UR_EXPECT_TRUE(Contains(page, "args.id = keyId;"));
  UR_EXPECT_TRUE(Contains(page, "args.name = name;"));
}

// The new key is a full account credential: it is never logged, stored,
// selectable or kept past its sheet.
UR_TEST(ApiKeysWiring_TheKeyIsNeverLoggedStoredOrKept) {
  const std::string page = ReadCode("ApiKeysPage.cpp");
  for (const char* log : {"g_warning", "g_message", "g_debug", "g_info", "g_print", "g_printerr",
                          "printf", "fprintf", "puts"}) {
    for (const std::string& arguments : CallArguments(page, log)) {
      for (const char* secret : {"apiKey", "->api_key", "ShownKey", "key->", "*key", ".Copy()"}) {
        UR_EXPECT_TRUE_MSG(std::string(log) + "(" + arguments + ")", !Contains(arguments, secret));
      }
    }
  }
  for (const char* file : {"ApiKeysPage.cpp", "ApiKeysPage.hpp", "ApiKeysBinding.hpp",
                           "ApiKeysPresentation.hpp"}) {
    const std::string code = ReadCode(file);
    UR_EXPECT_TRUE_MSG(file, !code.empty());
    for (const char* store : {"prefs::", "events()", "ClientEvent", "ofstream", "fopen",
                              "g_file_set_contents", "Gio::File", "std::cerr", "std::cout",
                              "SecretService", "RpcSessionStore"}) {
      UR_EXPECT_TRUE_MSG(std::string(file) + " " + store, !Contains(code, store));
    }
  }
  // the key's label: not selectable (no primary selection), cleared as the
  // sheet hides, and the flow wiped once it is gone
  UR_EXPECT_FALSE(Contains(page, "sheet_.key->set_selectable"));
  // it wraps without a hyphen, which would read as part of the key
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void ApiKeysPage::OpenCreateSheet()"),
                          "Pango::Attribute::create_attr_insert_hyphens(false)"));
  const std::string open = FunctionBody(page, "void ApiKeysPage::OpenCreateSheet()");
  UR_EXPECT_TRUE(Contains(open, "if (sheet_.key != nullptr) sheet_.key->set_text({});"));
  const std::string hidden = FunctionBody(page, "void ApiKeysPage::CreateSheetHidden()");
  UR_EXPECT_TRUE(Contains(hidden, "createFlow_.Close();"));
  UR_EXPECT_TRUE(Contains(hidden, "EndSheet();"));
  // the label shows the key only on the step that shows it
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void ApiKeysPage::RenderCreateSheet()"),
                          "sheet_.key->set_text(created ? createFlow_.ShownKey() : std::string());"));
  // an answer posted after the page is gone is wiped, not dropped whole
  UR_EXPECT_TRUE(Contains(page, "api_keys::Wipe(answer.apiKey);"));
  // the destructor wipes the flow
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "ApiKeysPage::~ApiKeysPage()"), "createFlow_.Close();"));
}

// Copy key puts the whole key on the clipboard, and the copy is wiped.
UR_TEST(ApiKeysWiring_CopyCopiesTheWholeKey) {
  const std::string copy = FunctionBody(ReadCode("ApiKeysPage.cpp"), "void ApiKeysPage::CopyCreatedKey()");
  UR_EXPECT_TRUE(Before(copy, "std::optional<std::string> key = createFlow_.Copy();",
                        "get_clipboard()->set_text(*key);"));
  UR_EXPECT_TRUE(Before(copy, "get_clipboard()->set_text(*key);", "api_keys::Wipe(*key);"));
  // Copied reads in its place for the settings' moment
  UR_EXPECT_TRUE(Contains(copy, "settings_.copiedMs"));
  UR_EXPECT_TRUE(Contains(copy, "createFlow_.EndCopied();"));
}

// The sheet stays while its request is out (Escape, Cancel and the window
// manager are refused), so the key has somewhere to show; a sign-out closes
// it anyway, after its request is released.
UR_TEST(ApiKeysWiring_TheCreateSheetStaysWhileItsRequestIsOut) {
  const std::string page = ReadCode("ApiKeysPage.cpp");
  const std::string open = FunctionBody(page, "void ApiKeysPage::OpenCreateSheet()");
  UR_EXPECT_TRUE(Contains(open, "[this]() -> bool { return !createFlow_.CanClose(); }, false);"));
  UR_EXPECT_TRUE(Contains(open, "CloseCreateSheet(false);"));
  UR_EXPECT_TRUE(Contains(open, "createDialog_->set_modal(true);"));
  UR_EXPECT_FALSE(Contains(open, "AddEscapeToClose(*createDialog_)"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void ApiKeysPage::CloseCreateSheet(bool force)"),
                          "if (!force && !createFlow_.CanClose()) return;"));
  const std::string reset = FunctionBody(page, "void ApiKeysPage::ResetForSignOut()");
  UR_EXPECT_TRUE(Before(reset, "binding_.Release();", "CloseCreateSheet(true);"));
  // Create sends the name once, through the flow
  const std::string submit = FunctionBody(page, "void ApiKeysPage::SubmitCreate()");
  UR_EXPECT_TRUE(Before(submit, "createFlow_.Submit(", "binding_.Create(*name,"));
  // the entry takes no more than the server stores
  UR_EXPECT_TRUE(Contains(open, "sheet_.name->set_max_length(static_cast<int>(api_keys::kMaxNameCharacters));"));
}

// The row's trailing Delete is flat and destructive and only opens the
// confirmation; the confirmation names the key and its default is Cancel;
// only its Delete sends.
UR_TEST(ApiKeysWiring_EveryDeleteIsConfirmedFirst) {
  const std::string page = ReadCode("ApiKeysPage.cpp");
  const std::string row = FunctionBody(page, "ApiKeysPage::RowWidgets ApiKeysPage::BuildRow(");
  UR_EXPECT_TRUE(Contains(row, "row.del->add_css_class(\"flat\");"));
  UR_EXPECT_TRUE(Contains(row, "row.del->add_css_class(\"destructive-action\");"));
  UR_EXPECT_TRUE(Contains(row, "row.del->signal_clicked().connect([this, keyId] { PressDelete(keyId); });"));
  const std::string press = FunctionBody(page, "void ApiKeysPage::PressDelete(");
  UR_EXPECT_TRUE(Before(press, "deleteFlow_.Press(binding_.Read(), keyId, MakeWords())",
                        "ShowDeleteConfirmation("));
  UR_EXPECT_FALSE(Contains(press, "binding_.Delete("));
  const std::string confirm = FunctionBody(page, "void ApiKeysPage::ShowDeleteConfirmation(");
  UR_EXPECT_TRUE(Contains(confirm, "api_keys::ConfirmationBody(confirmation, MakeWords())"));
  UR_EXPECT_TRUE(Contains(confirm, "cancel->set_receives_default(true);"));
  UR_EXPECT_TRUE(Contains(confirm, "confirmDialog_->set_default_widget(*cancel);"));
  UR_EXPECT_TRUE(Contains(confirm, "confirmDialog_->set_modal(true);"));
  UR_EXPECT_TRUE(Contains(confirm, "AddEscapeToClose(*confirmDialog_);"));
  UR_EXPECT_TRUE(Before(confirm, "const std::optional<std::string> keyId = deleteFlow_.Confirm();",
                        "if (keyId) binding_.Delete(*keyId);"));
  UR_EXPECT_TRUE(Contains(confirm, "deleteFlow_.Cancel();\n        EndSheet();\n        RestoreFocus(keyId);"));
  // the row names its action for a screen reader
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void ApiKeysPage::UpdateRow("),
                          "kit::SetAccessibleLabel(*row.del, text.deleteName);"));
}

UR_TEST(ApiKeysWiring_SignOutAndDestructionReleaseTheRequests) {
  const std::string page = ReadCode("ApiKeysPage.cpp");
  const std::string reset = FunctionBody(page, "void ApiKeysPage::ResetForSignOut()");
  UR_EXPECT_TRUE(Before(reset, "binding_.Release();", "Render();"));
  UR_EXPECT_TRUE(Contains(reset, "deleteFlow_.Cancel();"));
  const std::string destructor = FunctionBody(page, "ApiKeysPage::~ApiKeysPage()");
  UR_EXPECT_TRUE(Before(destructor, "*alive_ = false;", "binding_.Release();"));
  // the destructor touches no page widget (EarningsPage, issue #13)
  for (const char* widgetCall : {"Render(", "rowsBox_", "->set_", "remove("}) {
    UR_EXPECT_TRUE_MSG(widgetCall, !Contains(destructor, widgetCall));
  }
  // signed out, no service and no request
  const std::string load = FunctionBody(page, "void ApiKeysPage::Load()");
  UR_EXPECT_TRUE(Before(load, "if (!CanCallApi()) {", "EnsureService();"));
  UR_EXPECT_TRUE(Before(load, "EnsureService();", "binding_.Refresh();"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "bool ApiKeysPage::CanCallApi()"),
                          "return !previewMode_ && host_.IsLoggedIn();"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void ApiKeysPage::Retry()"), "binding_.Refresh();"));
}

UR_TEST(ApiKeysWiring_TheIntroLinksTheDocs) {
  const std::string page = ReadCode("ApiKeysPage.cpp");
  const std::string build = FunctionBody(page, "void ApiKeysPage::BuildPane()");
  UR_EXPECT_TRUE(Contains(build, "Lookup(api_keys::kIntroText)"));
  UR_EXPECT_TRUE(Contains(build, "Glib::Markup::escape_text(api_keys::kDocsUrl)"));
  UR_EXPECT_TRUE(Contains(build, "docs->add_css_class(\"ur-learn-more\");"));
  UR_EXPECT_TRUE(Contains(FunctionBody(page, "void ApiKeysPage::OpenDocs()"),
                          "g_app_info_launch_default_for_uri(api_keys::kDocsUrl, nullptr, &err)"));
  // the header's refresh and the failed load's Retry ask for the list
  UR_EXPECT_TRUE(Contains(build, "refreshButton_->signal_clicked().connect([this] { binding_.Refresh(); });"));
  UR_EXPECT_TRUE(Contains(build, "retryButton_->signal_clicked().connect([this] { Retry(); });"));
  UR_EXPECT_TRUE(Contains(build, "createButton_->signal_clicked().connect([this] { OpenCreateSheet(); });"));
}
