// The browser approval's call sites as wired: a urnetwork://auth link reaches
// its gate (MainWindow::HandleAuthLink) before anything of its launch is shown,
// and the gate raises the window only for the live attempt's denial or its
// approval; the attempt starts only from the sheet's click, in the order the
// contract gives, and ends at every hook it names; the sheet holds no secret,
// confirms before anything is registered and closes as the contract says; the
// pasted code has one way in; and no log line of the files that handle a link
// or a secret of an attempt is given one. The window, the sheet and main.cpp
// need gtkmm and the SDK, so this reads their sources with the comments
// blanked.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::BlankComments;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::CountOf;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

namespace {

// Each needle occurs after the one before it.
bool ApprovalInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

// Every run of whitespace made one space.
std::string Squash(const std::string& text) {
  std::string out;
  bool space = false;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      space = true;
      continue;
    }
    if (space && !out.empty()) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  return out;
}

// The text from `from` up to the first `to` after it ("" without `from`).
std::string Between(const std::string& text, const std::string& from, const std::string& to) {
  const size_t start = text.find(from);
  if (start == std::string::npos) return std::string();
  const size_t end = text.find(to, start + from.size());
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// The calls of `name` (e.g. "set_visible(") that are not a member's of another
// object or part of a longer name: the window's own.
size_t OwnCalls(const std::string& code, const std::string& name) {
  size_t count = 0;
  for (size_t at = code.find(name); at != std::string::npos; at = code.find(name, at + 1)) {
    if (at > 0 && (IsWordChar(code[at - 1]) || code[at - 1] == '.' || code[at - 1] == '>')) {
      continue;
    }
    ++count;
  }
  return count;
}

// The sources under src/ (the daemon's too), by their path under src/, with
// their comments blanked.
std::vector<std::pair<std::string, std::string>> Sources() {
  std::vector<std::pair<std::string, std::string>> out;
  const std::filesystem::path src(UR_SRC_DIR);
  std::error_code unknown;
  for (std::filesystem::recursive_directory_iterator it(src, unknown), end; !unknown && it != end;
       it.increment(unknown)) {
    if (!it->is_regular_file()) continue;
    const std::string ext = it->path().extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
    const std::string name = std::filesystem::relative(it->path(), src).generic_string();
    out.emplace_back(name, ReadCode(name));
  }
  return out;
}

// ---- the log-leak scanner (the Windows app's authLinkLogLeaks) ------------

// Names that hold a link, or a value taken from one or from an attempt,
// wherever they stand alone.
const std::set<std::string>& SecretNames() {
  static const std::set<std::string> names = {
      "url",       "uri",       "uris",  "link",   "code", "state",    "data",   "epk",  "nonce",
      "publicKey", "secretKey", "byJwt", "by_jwt", "jwt",  "authCode", "pasted", "argv", "paths"};
  return names;
}

// An object that holds a link, a key of an attempt or a launch, and the members
// of it that are secrets. The member of anything else (a tunnel's
// status.state) is not what this guards.
const std::set<std::string>& SecretHolders() {
  static const std::set<std::string> holders = {"link",   "request", "view",     "keys",
                                                "params", "pair",    "answer",   "attempt",
                                                "opened", "launch",  "arguments"};
  return holders;
}

const std::set<std::string>& SecretMembers() {
  static const std::set<std::string> members = {"state", "code",      "data",      "epk",
                                                "nonce", "publicKey", "secretKey", "deepLink",
                                                "uris",  "paths"};
  return members;
}

// The arguments of the call whose '(' is at `open`, split at the commas of its
// own level; a string or character literal is one piece.
std::vector<std::string> CallArguments(const std::string& code, size_t open) {
  std::vector<std::string> arguments;
  int depth = 0;
  size_t start = open + 1;
  for (size_t at = open; at < code.size(); ++at) {
    const char c = code[at];
    if (c == '"' || c == '\'') {
      for (++at; at < code.size() && code[at] != c && code[at] != '\n'; ++at) {
        if (code[at] == '\\') ++at;
      }
      continue;
    }
    if (c == '(' || c == '[' || c == '{') ++depth;
    if (c == ')' || c == ']' || c == '}') {
      if (--depth == 0) {
        arguments.push_back(code.substr(start, at - start));
        return arguments;
      }
    }
    if (c == ',' && depth == 1) {
      arguments.push_back(code.substr(start, at - start));
      start = at + 1;
    }
  }
  arguments.push_back(code.substr(start));
  return arguments;
}

// The argument with every string and character literal made empty.
std::string WithoutLiterals(const std::string& text) {
  std::string out;
  for (size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (c != '"' && c != '\'') {
      out.push_back(c);
      continue;
    }
    out.push_back(c);
    for (++at; at < text.size() && text[at] != c && text[at] != '\n'; ++at) {
      if (text[at] == '\\') ++at;
    }
    out.push_back(c);
  }
  return out;
}

std::string TrimLeft(const std::string& text, size_t from) {
  while (from < text.size() && std::isspace(static_cast<unsigned char>(text[from])) != 0) ++from;
  return text.substr(from);
}

std::string TrimRight(const std::string& text) {
  size_t end = text.size();
  while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return text.substr(0, end);
}

bool StartsWith(const std::string& text, const std::string& prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The identifier at the front of `text` ("" when it starts with none).
std::string LeadingWord(const std::string& text) {
  size_t end = 0;
  while (end < text.size() && IsWordChar(text[end])) ++end;
  return text.substr(0, end);
}

// The secrets one argument is given: a link-bearing name standing alone (not
// the member of something else, not a call, not its length), a member that is
// one of a link-bearing object, or a GError's message.
std::vector<std::string> ArgumentLeaks(const std::string& raw) {
  std::vector<std::string> leaks;
  const std::string argument = WithoutLiterals(raw);
  for (size_t at = 0; at < argument.size();) {
    if (!IsWordChar(argument[at]) || (at > 0 && IsWordChar(argument[at - 1]))) {
      ++at;
      continue;
    }
    const std::string name = LeadingWord(argument.substr(at));
    const size_t next = at + name.size();
    at = next;
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])) != 0) continue;
    const std::string before = TrimRight(argument.substr(0, next - name.size()));
    const bool member = EndsWith(before, ".") || EndsWith(before, "->") || EndsWith(before, "::");
    const std::string after = TrimLeft(argument, next);
    std::string accessed;
    if (StartsWith(after, ".")) accessed = LeadingWord(TrimLeft(after, 1));
    if (StartsWith(after, "->")) accessed = LeadingWord(TrimLeft(after, 2));
    if (!member && SecretHolders().count(name) != 0 && SecretMembers().count(accessed) != 0) {
      leaks.push_back(name + "." + accessed);
      continue;
    }
    if (!member && StartsWith(after, "->") && accessed == "message" &&
        (name == "error" || name == "err" || EndsWith(name, "Error") || EndsWith(name, "error"))) {
      leaks.push_back(name + "->message");
      continue;
    }
    if (member || SecretNames().count(name) == 0) continue;
    if (StartsWith(after, "(")) continue;  // a function of that name
    if (accessed == "size" || accessed == "length" || accessed == "empty") continue;
    leaks.push_back(name);
  }
  return leaks;
}

// The leaks of every log call in `code`: glib's (after the format), printf's
// (after the format), fprintf to stderr (after the stream and the format) and
// std::cerr (every operand of its <<).
std::vector<std::string> LogLeaks(const std::string& code) {
  std::vector<std::string> leaks;
  const struct Sink {
    const char* name;
    size_t skip;
  } sinks[] = {{"g_message(", 1}, {"g_warning(", 1}, {"g_critical(", 1}, {"g_debug(", 1},
               {"g_info(", 1},    {"printf(", 1},    {"fprintf(", 2}};
  for (const Sink& sink : sinks) {
    const std::string needle = sink.name;
    for (size_t at = code.find(needle); at != std::string::npos; at = code.find(needle, at + 1)) {
      if (at > 0 && IsWordChar(code[at - 1])) continue;  // snprintf, g_printf
      std::vector<std::string> arguments = CallArguments(code, at + needle.size() - 1);
      if (needle == "fprintf(" && (arguments.empty() || Squash(arguments[0]) != "stderr")) {
        continue;  // a file of the app's own, not the log
      }
      for (size_t i = sink.skip; i < arguments.size(); ++i) {
        for (const std::string& leak : ArgumentLeaks(arguments[i])) {
          leaks.push_back(std::string(sink.name) + " is given " + leak);
        }
      }
    }
  }
  for (size_t at = code.find("std::cerr"); at != std::string::npos;
       at = code.find("std::cerr", at + 1)) {
    const size_t end = code.find(';', at);
    const std::string statement =
        code.substr(at, end == std::string::npos ? std::string::npos : end - at);
    for (size_t op = statement.find("<<"); op != std::string::npos;) {
      const size_t following = statement.find("<<", op + 2);
      const std::string operand = statement.substr(
          op + 2, following == std::string::npos ? std::string::npos : following - op - 2);
      for (const std::string& leak : ArgumentLeaks(operand)) {
        leaks.push_back("std::cerr is given " + leak);
      }
      op = following;
    }
  }
  return leaks;
}

// The approval's functions of a file: the text of each body, in one piece.
std::string Bodies(const std::string& code, const std::vector<std::string>& signatures) {
  std::string out;
  for (const std::string& signature : signatures) {
    const std::string body = FunctionBody(code, signature);
    UR_EXPECT_TRUE_MSG(signature + " is defined", !body.empty());
    out += body;
    out += "\n";
  }
  return out;
}

const std::vector<std::string>& WindowApprovalFunctions() {
  static const std::vector<std::string> functions = {
      "void MainWindow::OnUseCode() {",
      "void MainWindow::ShowAuthCodeDialog() {",
      "void MainWindow::SignInWithAuthCode(std::string code) {",
      "void MainWindow::HandleAuthLink(const std::string& url, bool ownLaunch) {",
      "void MainWindow::ShowStrayNotice(bool ownLaunch) {",
      "approval::Context MainWindow::ApprovalContext() {",
      "bool MainWindow::ApprovalOffered() {",
      "bool MainWindow::ApprovalOfferedIn(const approval::Network& network) {",
      "void MainWindow::StartApprovalCheck() {",
      "std::optional<approval::Slot::View> MainWindow::BeginApproval() {",
      "std::string MainWindow::ApprovalRequestUrl(const approval::Slot::View& view) const {",
      "void MainWindow::CancelApproval(approval::ClearReason reason) {",
      "void MainWindow::OnApprovalTimer() {",
      "void MainWindow::ShutdownApproval() {",
      "void MainWindow::OnApprovedCode(std::string code) {",
      "void MainWindow::OnApprovalDenied() {",
      "void MainWindow::OnApprovalExpired() {",
      "void MainWindow::ShowApprovalSheet(std::string approvedCode) {",
      "BrowserApprovalSheet::Controller MainWindow::ApprovalSheetController() {",
  };
  return functions;
}

const std::vector<std::string>& HostApprovalFunctions() {
  static const std::vector<std::string> functions = {
      "void SdkHost::RedeemApprovedCode(",
      "void SdkHost::ConfirmApprovedNetwork(std::function<void(AuthResult)> done) {",
      "void SdkHost::DiscardApprovedNetwork() {",
      "uint64_t SdkHost::BeginBrowserApproval() {",
      "approval::Network SdkHost::CurrentApprovalNetwork() const {",
      "void SdkHost::SetApprovalNetwork(approval::Network network) {",
      "void SdkHost::UpdateApprovalNetworkLocked() {",
      "uint64_t SdkHost::CancelPendingWalletFlows(const std::string& reason) {",
      "void SdkHost::NoteSignInStarted() {",
  };
  return functions;
}

std::string Window() { return ReadCode("MainWindow.cpp"); }
std::string Sheet() { return ReadCode("BrowserApprovalSheet.cpp"); }
std::string Main() { return ReadCode("main.cpp"); }

const char* const kHandleAuthLink =
    "void MainWindow::HandleAuthLink(const std::string& url, bool ownLaunch) {";

}  // namespace

// ---- the gate ---------------------------------------------------------------

// Every launch is served by main.cpp's serve: an auth link goes to its gate,
// before any other link is routed and before the window is shown, and every
// other link goes to the host inside a try, so a link any program can fire
// cannot end the app. A launch that carries only auth links shows nothing
// itself; the own launch is told apart by the first activation or open.
UR_TEST(BrowserApprovalWiring_AnAuthLinkIsGatedBeforeTheWindowIsRaised) {
  const std::string main = Main();
  const std::string serve = Between(
      main, "const auto serve = [&](const urnw::instance::Launch& launch, bool ownLaunch) {",
      "\n  };\n");
  UR_EXPECT_TRUE_MSG("serve", !serve.empty());
  UR_EXPECT_TRUE(ApprovalInOrder(
      serve,
      {"for (const std::string& uri : launch.uris) {", "if (urnw::authlink::IsAuthHost(uri)) {",
       "if (window) window->HandleAuthLink(uri, ownLaunch);", "continue;", "}", "try {",
       "host->HandleDeepLink(uri);", "} catch (const std::exception&) {",
       "g_warning(\"launch: a deep link could not be handled\");", "}", "}",
       "if (window && urnw::instance::ShowsWindowFor(launch)) window->present();"}));
  UR_EXPECT_TRUE(
      Before(serve, "window->HandleAuthLink(uri, ownLaunch);", "host->HandleDeepLink(uri);"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(serve, "present("));
  // the gate is reached from here and from nowhere else
  for (const auto& [name, code] : Sources()) {
    if (name == "main.cpp" || name == "MainWindow.hpp" || name == "MainWindow.cpp") continue;
    UR_EXPECT_TRUE_MSG(name + " calls HandleAuthLink", !Contains(code, "HandleAuthLink("));
  }
  UR_EXPECT_EQ(std::size_t{1}, CountOf(main, "HandleAuthLink("));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(Window(), "HandleAuthLink("));
  // the own launch: written by the first activation or open only
  UR_EXPECT_EQ(std::size_t{2}, CountOf(main, "!std::exchange(ownLaunchServed, true)"));
  UR_EXPECT_EQ(std::size_t{3}, CountOf(main, "ownLaunchServed"));
  UR_EXPECT_TRUE(Contains(Squash(main),
                          "urnw::instance::OpenLaunches(G_APPLICATION(app->gobj()), "
                          "[&](const urnw::instance::Launch& launch) { serve(launch, "
                          "false); });"));
  UR_EXPECT_EQ(std::size_t{3}, CountOf(main, "serve(launch,"));
  // the host routes no auth link of its own any more
  const std::string host = ReadCode("SdkHost.cpp");
  UR_EXPECT_TRUE(!Contains(
      FunctionBody(host, "void SdkHost::HandleDeepLink(const std::string& url) {"), "auth"));
}

// The gate: not while the app quits; the parser, the attempt, the box opened
// with the attempt's key only for an answer the attempt accepted, one closed
// line, then by verdict. The window is raised for the live attempt's denial and
// for its approval, after the attempt is consumed, and for nothing else but the
// own launch's one stray line.
UR_TEST(BrowserApprovalWiring_TheGateDecidesInTheContractsOrder) {
  const std::string window = Window();
  const std::string gate = FunctionBody(window, kHandleAuthLink);
  UR_EXPECT_TRUE_MSG("HandleAuthLink", !gate.empty());
  UR_EXPECT_TRUE(ApprovalInOrder(
      gate,
      {"if (approvalQuitting_) return;",
       "const authlink::Return link = authlink::Parse(url);",
       "approval::Decision decision = approval_.Check(link, approval::Now(), ApprovalContext());",
       "std::optional<std::string> code;",
       "if (decision.verdict == approval::Verdict::Accepted) {",
       "code = approval::Unseal(approval_.SecretKey(), link);",
       "if (!code) decision = approval::Decision{approval::Verdict::Stray, "
       "approval::Why::BadSeal};",
       "}",
       "AuthLinkWhy(link, decision);",
       "g_message(\"app: auth link: %.*s (%.*s)\"",
       "switch (decision.verdict) {",
       "case approval::Verdict::Ignored:",
       "return;",
       "case approval::Verdict::Stray:",
       "if (decision.why == approval::Why::Expired || decision.why == approval::Why::Superseded) {",
       "OnApprovalExpired();",
       "ShowStrayNotice(ownLaunch);",
       "return;",
       "case approval::Verdict::Denied:",
       "present();",
       "OnApprovalDenied();",
       "return;",
       "case approval::Verdict::Accepted:",
       "if (!approval_.Consume(link.state)) return;",
       "present();",
       "OnApprovedCode(std::move(*code));",
       "return;"}));
  UR_EXPECT_EQ(std::size_t{2}, CountOf(gate, "present();"));
  UR_EXPECT_TRUE(!Contains(gate, "default:"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(gate, "approval::Unseal("));
  // the stray's one line: the own launch, once, as often as the neutral line
  // allows, and no real error overwritten
  const std::string stray =
      FunctionBody(window, "void MainWindow::ShowStrayNotice(bool ownLaunch) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      stray,
      {"if (!ownLaunch || std::exchange(strayNoticeTaken_, true)) return;",
       "if (!strayLine_.Allow(std::chrono::steady_clock::now())) return;", "present();",
       "if (stack_.get_visible_child_name() == \"login\" && loginError_.get_text().empty()) {",
       "SetLoginNotice(T_(\"auth_code_error\","}));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "strayNoticeTaken_"));
  // signed in reads the window's own state first
  UR_EXPECT_TRUE(Contains(FunctionBody(window, "approval::Context MainWindow::ApprovalContext() {"),
                          "context.signedIn = authSignedIn_ || host_.IsLoggedIn();"));
  // the parser's reason is the more useful word for a link that was no answer at all
  const std::string why = FunctionBody(window,
                                       "std::string_view AuthLinkWhy(const authlink::Return& link, "
                                       "const approval::Decision& decision) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      why,
      {"decision.why == approval::Why::NoAttempt || decision.why == approval::Why::InvalidShape;",
       "if (link.kind == authlink::Kind::Invalid && parserKnowsBetter) {",
       "return authlink::ToString(link.reject);", "return approval::ToString(decision.why);"}));
}

// The attempt is the window's: started, checked, consumed and cleared there and
// nowhere else, each in its one place; an auth link is parsed only by the gate,
// and its values are read by AuthLink.hpp's parser and no decoder of its own.
UR_TEST(BrowserApprovalWiring_OnlyTheWindowReachesTheAttempt) {
  const std::string window = Window();
  for (const char* once :
       {"approval_.Begin(", "approval_.Consume(", "approval_.Clear(", "approval_.Check(",
        "approval_.Peek(", "approval_.Reopenable(", "approval_.SecretKey()"}) {
    UR_EXPECT_TRUE_MSG(std::string("MainWindow.cpp calls ") + once + " once",
                       CountOf(window, once) == 1);
  }
  // ApplyAuthState (a network change leaves a dead attempt) and BeginApproval
  // (a second click)
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "approval_.Flow()"));
  // ApplyAuthState and CancelApproval
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "approval_.Live()"));
  for (const auto& [name, code] : Sources()) {
    if (name != "MainWindow.cpp") {
      UR_EXPECT_TRUE_MSG(name + " reaches the approval slot", !Contains(code, "approval_."));
    }
    if (name != "MainWindow.cpp") {
      UR_EXPECT_TRUE_MSG(name + " parses an auth link", !Contains(code, "authlink::Parse("));
    }
    if (name != "AuthLink.hpp") {
      UR_EXPECT_TRUE_MSG(name + " names the auth host", !Contains(code, "\"urnetwork://auth"));
    }
  }
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "authlink::Parse("));
  UR_EXPECT_TRUE(Contains(FunctionBody(window, kHandleAuthLink), "authlink::Parse("));
}

// The decoders of link values each decode their own host's links: the wallet
// bridges' returns, the sso return, the pay hand-back and the campaign emails'
// onboarding links. The gate decodes nothing, and neither does the parser: a
// '%' in an auth link is refused, never read.
UR_TEST(BrowserApprovalWiring_NoSourceDecodesAnAuthLinksValues) {
  const std::set<std::string> decoders = {"WalletConnect.cpp", "SsoBridge.hpp", "UpgradeSheet.cpp",
                                          "MainWindow.cpp"};
  const auto decodes = [](const std::string& code) {
    for (const char* use : {"g_uri_unescape", "Glib::uri_unescape", "PercentDecode(",
                            "g_uri_parse_params", "unescape_string", "hexv"}) {
      if (Contains(code, use)) return true;
    }
    return false;
  };
  for (const auto& [name, code] : Sources()) {
    if (decoders.count(name) == 0) {
      UR_EXPECT_TRUE_MSG(name + " decodes link values itself", !decodes(code));
    }
  }
  const std::string window = Window();
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "g_uri_unescape_string("));
  UR_EXPECT_TRUE(Contains(
      FunctionBody(window, "void MainWindow::HandleOnboardingLink(const std::string& url) {"),
      "g_uri_unescape_string("));
  UR_EXPECT_TRUE(!decodes(FunctionBody(window, kHandleAuthLink)));
  UR_EXPECT_TRUE(!Contains(FunctionBody(window, kHandleAuthLink), "'%'"));
  UR_EXPECT_TRUE(!decodes(ReadCode("AuthLink.hpp")));
  // the check itself flags the kind of decoder the gate must not have
  UR_EXPECT_TRUE(decodes("char* dec = g_uri_unescape_string(value.c_str(), nullptr);"));
  UR_EXPECT_TRUE(decodes("out[detail::PercentDecode(pair.substr(0, eq))] = x;"));
}

// ---- the attempt -------------------------------------------------------------

// Begun only from the sheet's click: a live attempt is read back while the page
// would accept it and its flow is still current; else the network read once,
// the seven checks for it, a fresh key pair, the flow taken last, and the state
// from the OS.
UR_TEST(BrowserApprovalWiring_TheAttemptStartsInTheContractsOrder) {
  const std::string window = Window();
  const std::string begin =
      FunctionBody(window, "std::optional<approval::Slot::View> MainWindow::BeginApproval() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      begin, {"const approval::Clocks now = approval::Now();",
              "if (std::optional<approval::Slot::View> live = approval_.Peek(now)) {",
              "if (approval_.Flow() == host_.CurrentBrowserFlow() && approval_.Reopenable(now)) "
              "return live;",
              "CancelApproval(approval::ClearReason::Superseded);",
              "const approval::Network network = host_.CurrentApprovalNetwork();",
              "if (!ApprovalOfferedIn(network)) return std::nullopt;",
              "approval::FreshKeyPair(approval::NewKeyPair);",
              "g_warning(\"approval: no usable key pair; the paste flow stays\");",
              "params.publicKey = std::move(keys->publicKey);",
              "params.secretKey = std::move(keys->secretKey);", "params.api = network.api;",
              "params.now = approval::Now();"}));
  UR_EXPECT_TRUE(Contains(Squash(begin),
                          "params.flow = host_.BeginBrowserApproval(); "
                          "std::optional<approval::Slot::View> view = "
                          "approval_.Begin(std::move(params), osrandom::RandomBytes);"));
  UR_EXPECT_TRUE(ApprovalInOrder(
      begin, {"approval_.Begin(std::move(params), osrandom::RandomBytes);",
              "g_warning(\"approval: no entropy for a state; the paste flow stays\");",
              "approvalTimer_.disconnect();", "approvalTimer_ = Glib::signal_timeout().connect(",
              "OnApprovalTimer();", "return false;",
              "std::chrono::duration_cast<std::chrono::milliseconds>(approval::kLife).count());",
              "g_message(\"approval: an attempt started (version %d)\"", "return view;"}));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "host_.BeginBrowserApproval()"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "BeginApproval();"));
  UR_EXPECT_TRUE(Contains(
      FunctionBody(window,
                   "BrowserApprovalSheet::Controller MainWindow::ApprovalSheetController() {"),
      "controller.begin = [this] { return BeginApproval(); };"));
  // the request is the page's own address and the attempt's view, and nothing else
  UR_EXPECT_TRUE(Contains(
      FunctionBody(
          window,
          "std::string MainWindow::ApprovalRequestUrl(const approval::Slot::View& view) const {"),
      "return approval::BuildRequestUrl(config::kApprovalPageUrl, view);"));
}

// The state comes from the OS random source, never GLib's: nothing on the way
// of an attempt draws from another.
UR_TEST(BrowserApprovalWiring_TheStateComesFromTheOsRandomSource) {
  const std::string approvalCode =
      Bodies(Window(), WindowApprovalFunctions()) + Sheet() + ReadCode("OsRandom.hpp");
  for (const char* other :
       {"g_random", "g_uuid_string_random", "rand(", "std::random_device", "mt19937", "GRand"}) {
    UR_EXPECT_TRUE_MSG(std::string("the approval draws from ") + other,
                       !Contains(approvalCode, other));
  }
  UR_EXPECT_TRUE(Contains(ReadCode("OsRandom.hpp"), "::getrandom("));
}

// Offered only when everything checks, in this order, each refusal one closed
// line: the build flag, a network a request can name, not root, signed out,
// the page live and listing the network's version, and the urnetwork://
// handler read back as this app's own. The flag is read there and in the
// check's start only; the check starts from a signed-out window on screen.
UR_TEST(BrowserApprovalWiring_TheApprovalIsOfferedOnlyWhenEverythingChecks) {
  const std::string window = Window();
  const std::string offered = FunctionBody(
      window, "bool MainWindow::ApprovalOfferedIn(const approval::Network& network) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      offered,
      {"if (!config::kBrowserApprovalEnabled) return false;",
       "if (network.kind == approval::NetworkKind::None) {",
       "g_message(\"approval: not offered: this network's API cannot be named in a request\");",
       "return false;",
       "if (ProcessIsElevated()) {",
       "g_message(\"approval: not offered: this process runs as root\");",
       "return false;",
       "if (ApprovalContext().signedIn) {",
       "g_message(\"approval: not offered: the app is signed in\");",
       "return false;",
       "StartApprovalPageCheck();",
       "if (ApprovalPageState() != ApprovalPage::Live) {",
       "g_message(\"approval: not offered: the page is not known to be live\");",
       "return false;",
       "if (!ApprovalPageLists(approval::ContractFor(network.kind))) {",
       "g_message(\"approval: not offered: the page does not list this network's contract "
       "version\");",
       "return false;",
       "switch (scheme::HandlerStatus()) {",
       "case scheme::HandlerState::Ours:",
       "return true;",
       "case scheme::HandlerState::Foreign:",
       "g_message(\"approval: not offered: another program handles urnetwork:// links\");",
       "return false;",
       "case scheme::HandlerState::Absent:",
       "g_message(\"approval: not offered: no program handles urnetwork:// links\");",
       "return false;"}));
  UR_EXPECT_TRUE(!Contains(offered, "default:"));
  UR_EXPECT_TRUE(Contains(FunctionBody(window, "bool MainWindow::ApprovalOffered() {"),
                          "return ApprovalOfferedIn(host_.CurrentApprovalNetwork());"));
  // the flag: the offer and the check's start
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "config::kBrowserApprovalEnabled"));
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "kBrowserApprovalEnabled"));
  const std::string start = FunctionBody(window, "void MainWindow::StartApprovalCheck() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      start, {"if constexpr (config::kBrowserApprovalEnabled) {",
              "if (host_.CurrentApprovalNetwork().kind != approval::NetworkKind::None &&",
              "!ProcessIsElevated()) {", "StartApprovalPageCheck();"}));
  // the one GET of the run starts from these two and nowhere else
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "StartApprovalPageCheck();"));
  for (const auto& [name, code] : Sources()) {
    if (name == "MainWindow.cpp" || name == "ApprovalCheck.cpp" || name == "ApprovalCheck.hpp") {
      continue;
    }
    UR_EXPECT_TRUE_MSG(name + " starts the page check", !Contains(code, "StartApprovalPageCheck("));
  }
  // and the check's start from a signed-out window on screen: its show, and a
  // sign-out (or a network change) landing with it on screen
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "StartApprovalCheck();"));
  UR_EXPECT_TRUE(Contains(window, "if (windowVisible_) {\n"));
  UR_EXPECT_TRUE(ApprovalInOrder(
      window, {"auto reconcilePresentation = [this] {", "if (windowVisible_) {",
               "if (!ApprovalContext().signedIn) StartApprovalCheck();",
               "property_visible().signal_changed().connect(reconcilePresentation);"}));
  UR_EXPECT_TRUE(Contains(FunctionBody(window, "void MainWindow::ApplyAuthState(bool loggedIn) {"),
                          "if (!loggedIn && windowVisible_) StartApprovalCheck();"));
  // a build that talks to a test origin says so, by its host
  UR_EXPECT_TRUE(Contains(Squash(window),
                          "if constexpr (config::kApprovalOriginIsATest) { g_warning(\"approval: "
                          "this build talks to a test origin: %.*s\", "
                          "static_cast<int>(config::kApprovalHost.size()), "
                          "config::kApprovalHost.data()); }"));
}

// The attempt ends wherever the contract says: a match (consumed by the gate),
// a cancel in the browser (the slot's denial), the sheet closed, a launch that
// failed, the five minutes (the sheet told even if the gate wiped the attempt
// first), another sign-in starting or succeeding, a sign-out, a network change,
// a new attempt and the quit. A hide to the tray is not an end.
UR_TEST(BrowserApprovalWiring_TheAttemptEndsAtEveryHook) {
  const std::string window = Window();
  const std::string apply =
      FunctionBody(window, "void MainWindow::ApplyAuthState(bool loggedIn) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      apply, {"const bool wasSignedIn = std::exchange(authSignedIn_, loggedIn);", "if (loggedIn) {",
              "CancelApproval(approval::ClearReason::SignedIn);", "} else if (wasSignedIn) {",
              "CancelApproval(approval::ClearReason::SignedOut);",
              "} else if (approval_.Live() && approval_.Flow() != host_.CurrentBrowserFlow()) {",
              "CancelApproval(approval::ClearReason::Superseded);", "}",
              "if (!loggedIn && windowVisible_) StartApprovalCheck();"}));
  // after the old branches, which keep their first "} else {"
  UR_EXPECT_TRUE(Before(apply, "} else {", "std::exchange(authSignedIn_, loggedIn)"));
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "authSignedIn_"));
  UR_EXPECT_TRUE(ApprovalInOrder(
      window, {"host_.SetSignInStartedHandler([this] {",
               "PostToMain([this] { CancelApproval(approval::ClearReason::SignInStarted); });"}));
  const std::string cancel =
      FunctionBody(window, "void MainWindow::CancelApproval(approval::ClearReason reason) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      cancel, {"approvalTimer_.disconnect();", "if (!approval_.Live()) return;",
               "approval_.Clear(reason);", "g_message(\"approval: the attempt ended: %.*s\""}));
  const std::string timer = FunctionBody(window, "void MainWindow::OnApprovalTimer() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      timer, {"CancelApproval(approval::ClearReason::TimedOut);", "OnApprovalExpired();"}));
  UR_EXPECT_TRUE_MSG("the sheet hears of a timeout the gate already wiped",
                     !Contains(timer, "return") && !Contains(timer, "Live()"));
  UR_EXPECT_TRUE(ApprovalInOrder(
      FunctionBody(window, "void MainWindow::ShutdownApproval() {"),
      {"approvalQuitting_ = true;", "CancelApproval(approval::ClearReason::Shutdown);"}));
  UR_EXPECT_EQ(std::size_t{2}, CountOf(window, "approvalQuitting_"));
  UR_EXPECT_TRUE(Contains(FunctionBody(window, "MainWindow::~MainWindow() {"),
                          "approvalTimer_.disconnect();"));
  // the quit: after the geometry, before the host's teardown
  const std::string quit = Between(Main(), "tray->on_quit = [&] {", "\n    };\n");
  UR_EXPECT_TRUE(
      ApprovalInOrder(quit, {"urnw::instance::BeginExiting();", "window->SaveGeometry();",
                             "window->ShutdownApproval();", "host->Shutdown();", "app->quit();"}));
  // the sheet closed, and a request that could not be opened
  const std::string sheet = Sheet();
  UR_EXPECT_TRUE(Contains(FunctionBody(sheet, "void BrowserApprovalSheet::OnClosed() {"),
                          "controller_.cancel(approval::ClearReason::Cancel);"));
  UR_EXPECT_EQ(std::size_t{2},
               CountOf(FunctionBody(sheet, "void BrowserApprovalSheet::StartApproval() {"),
                       "controller_.cancel(approval::ClearReason::Cancel);"));
  // the closes the window counts as a hide (the tray) do nothing to the sheet
  const std::string main = Main();
  const std::string close =
      Between(main, "window->signal_close_request().connect(", "return true;");
  UR_EXPECT_TRUE(!Contains(close, "approval") && !Contains(close, "Approval"));
  UR_EXPECT_TRUE(
      !Contains(FunctionBody(window, "void MainWindow::NoteHiddenToTray() {"), "Approval"));
  // the controller's cancel is CancelApproval itself
  UR_EXPECT_TRUE(Contains(
      FunctionBody(window,
                   "BrowserApprovalSheet::Controller MainWindow::ApprovalSheetController() {"),
      "controller.cancel = [this](approval::ClearReason reason) { CancelApproval(reason); };"));
}

// What the gate hands on reaches the sheet: an approved code to the open sheet,
// or to a fresh one (the person may have closed it, and the answer still came);
// a denial and a timeout to whatever sheet there is, which acts only on the
// pages it applies to. A closed sheet is replaced in one place.
UR_TEST(BrowserApprovalWiring_TheHooksHandTheSheetWhatArrives) {
  const std::string window = Window();
  UR_EXPECT_TRUE(
      ApprovalInOrder(FunctionBody(window, "void MainWindow::OnApprovedCode(std::string code) {"),
                      {"if (approvalSheet_ && approvalSheet_->Open()) {",
                       "approvalSheet_->BeginRedeem(std::move(code));", "return;",
                       "ShowApprovalSheet(std::move(code));"}));
  UR_EXPECT_TRUE(
      ApprovalInOrder(FunctionBody(window, "void MainWindow::OnApprovalDenied() {"),
                      {"g_message(\"login: the browser sign-in was cancelled in the browser\");",
                       "if (approvalSheet_) approvalSheet_->ShowDenied();"}));
  UR_EXPECT_TRUE(Contains(FunctionBody(window, "void MainWindow::OnApprovalExpired() {"),
                          "if (approvalSheet_) approvalSheet_->ShowTimedOut();"));
  const std::string show =
      FunctionBody(window, "void MainWindow::ShowApprovalSheet(std::string approvedCode) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      show, {"if (approvalSheet_ && approvalSheet_->Open()) {", "approval::Wipe(approvedCode);",
             "approvalSheet_->present();", "return;",
             "approvalSheet_ = std::make_unique<BrowserApprovalSheet>(*this, host_, "
             "ApprovalSheetController());",
             "if (!approvedCode.empty()) approvalSheet_->BeginRedeem(std::move(approvedCode));",
             "approvalSheet_->present();", "approval::Wipe(approvedCode);"}));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "approvalSheet_ = "));
  UR_EXPECT_TRUE(!Contains(window, "approvalSheet_.reset("));
  const std::string sheet = Sheet();
  UR_EXPECT_TRUE(Contains(FunctionBody(sheet, "void BrowserApprovalSheet::ShowTimedOut() {"),
                          "if (closed_ || page_ != Page::Waiting) return;"));
  UR_EXPECT_TRUE(
      ApprovalInOrder(FunctionBody(sheet, "void BrowserApprovalSheet::ShowDenied() {"),
                      {"if (closed_ || (page_ != Page::Waiting && page_ != Page::Start)) return;",
                       "ShowPage(Page::Start);"}));
  // the code is handed to the host and wiped; an answer that finds the sheet
  // gone drops what it redeemed
  const std::string redeem =
      FunctionBody(sheet, "void BrowserApprovalSheet::BeginRedeem(std::string code) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      redeem, {"if (!Open()) {", "approval::Wipe(code);", "return;", "confirmed_ = false;",
               "ShowPage(Page::SigningIn);", "std::weak_ptr<int> life = life_;",
               "host_.RedeemApprovedCode(code,", "PostToMain(", "if (!life.expired()) {",
               "OnRedeemed(std::move(answer));", "} else if (answer.ok) {",
               "host->DiscardApprovedNetwork();", "approval::Wipe(code);"}));
  const std::string redeemed = FunctionBody(
      sheet, "void BrowserApprovalSheet::OnRedeemed(SdkHost::ApprovedNetwork answer) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      redeemed, {"if (closed_) {", "if (answer.ok) host_.DiscardApprovedNetwork();", "return;",
                 "if (!answer.ok) {",
                 "g_warning(\"browser sign-in sheet: the approval could not be redeemed\");",
                 "answer.failure == SdkHost::ApprovedNetwork::Failure::Transport",
                 "T_(\"something_went_wrong\"", "T_(\"auth_code_error\"", "return;",
                 "const std::string name = SanitizeExternalDisplayText(answer.networkName);",
                 "if (name.empty()) {", "host_.DiscardApprovedNetwork();",
                 "g_warning(\"browser sign-in sheet: the approval named no network the person "
                 "could recognise\");",
                 "ShowFailed(T_(\"auth_code_error\",", "return;", "ShowConfirm(name);"}));
  const std::string confirm = FunctionBody(sheet, "void BrowserApprovalSheet::Confirm() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      confirm, {"confirmed_ = true;", "ShowPage(Page::SigningIn);", "host_.ConfirmApprovedNetwork(",
                "PostToMain(", "if (!life.expired()) OnConfirmed(result);"}));
  const std::string confirmed =
      FunctionBody(sheet, "void BrowserApprovalSheet::OnConfirmed(const AuthResult& result) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      confirmed,
      {"if (closed_) return;", "if (result.ok) {", "controller_.signedIn();", "Finish();",
       "return;", "g_warning(\"browser sign-in sheet: the device could not be registered\");",
       "ShowFailed(result.error.empty() ? T_(\"something_went_wrong\""}));
  // every sign-in that succeeds starts the tunnel
  UR_EXPECT_TRUE(Contains(
      FunctionBody(window,
                   "BrowserApprovalSheet::Controller MainWindow::ApprovalSheetController() {"),
      "controller.signedIn = [this] { StartTunnelUi(\"browser approval sign-in\"); };"));
}

// ---- the sheet ---------------------------------------------------------------

// The sheet closes as the contract says: Escape and every Cancel ask to close,
// which is refused only while a confirmed sign-in registers; it hides itself
// only in Finish; whatever way it hides, it runs OnClosed once, which drops a
// network redeemed for a yes that never came and cancels the attempt; and a
// stray Enter on the Confirm page is no yes.
UR_TEST(BrowserApprovalWiring_TheSheetClosesAsTheContractSays) {
  const std::string sheet = Sheet();
  const std::string ctor = FunctionBody(
      sheet, "BrowserApprovalSheet::BrowserApprovalSheet(Gtk::Window& parent, SdkHost& host,");
  UR_EXPECT_TRUE(ApprovalInOrder(
      ctor, {"EnsureDrawerCss();", "set_transient_for(parent);", "set_modal(true);",
             "set_default_size(420, -1);", "set_resizable(false);", "set_hide_on_close(true);",
             "Gtk::EventControllerKey::create();", "if (keyval != GDK_KEY_Escape) return false;",
             "close();", "return true;", "add_controller(keys);", "signal_close_request().connect(",
             "[this]() -> bool { return page_ == Page::SigningIn && confirmed_; }, false);",
             "hidden_ = signal_hide().connect([this] { OnClosed(); });"}));
  UR_EXPECT_TRUE(!Contains(sheet, "AddEscapeToClose"));
  UR_EXPECT_EQ(std::size_t{1}, OwnCalls(sheet, "set_visible("));
  UR_EXPECT_TRUE(Contains(FunctionBody(sheet, "void BrowserApprovalSheet::Finish() {"),
                          "if (!closed_) set_visible(false);"));
  for (const char* other : {"hide(", "destroy(", "unmap(", "minimize("}) {
    UR_EXPECT_TRUE_MSG(std::string("the sheet hides itself with ") + other,
                       OwnCalls(sheet, other) == 0);
  }
  // every Cancel asks to close, as Escape does
  UR_EXPECT_EQ(std::size_t{5}, CountOf(sheet, "T_(\"cancel\", \"Cancel\")"));
  UR_EXPECT_EQ(std::size_t{6}, CountOf(sheet, "close();"));
  const std::string closed = FunctionBody(sheet, "void BrowserApprovalSheet::OnClosed() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      closed, {"if (closed_) return;", "closed_ = true;", "pasteEntry_->set_text(\"\");",
               "if (page_ == Page::Confirm && !confirmed_) {", "host_.DiscardApprovedNetwork();",
               "g_message(\"browser sign-in sheet: closed without a yes; the redeemed network was "
               "dropped\");",
               "controller_.cancel(approval::ClearReason::Cancel);"}));
  // the yes has one way in, from the Confirm page's Sign in
  UR_EXPECT_EQ(std::size_t{1}, CountOf(sheet, "Confirm();"));
  UR_EXPECT_TRUE(
      Contains(Squash(FunctionBody(sheet, "Gtk::Widget* BrowserApprovalSheet::BuildConfirm() {")),
               "confirmSignIn_->signal_clicked().connect([this] { if (page_ == "
               "Page::Confirm) Confirm(); });"));
  const std::string show = FunctionBody(sheet, "void BrowserApprovalSheet::ShowPage(Page page) {");
  const std::string confirmCase = Between(show, "case Page::Confirm:", "break;");
  UR_EXPECT_TRUE(Contains(confirmCase, "unset_default_widget();"));
  UR_EXPECT_TRUE(Contains(confirmCase, "confirmCancel_->grab_focus();"));
  UR_EXPECT_TRUE(!Contains(confirmCase, "set_default_widget(*"));
  // the sheet says nothing on its way out with the window
  UR_EXPECT_TRUE(Contains(FunctionBody(sheet, "BrowserApprovalSheet::~BrowserApprovalSheet() {"),
                          "hidden_.disconnect();"));
}

// Each page holds what it shows, from the build constant and the store's keys:
// the action and the host it goes to, the masked paste field, the wait, the
// network's sanitized name, the message and the way back to the start. The
// request is opened in the browser and wiped, never shown or copied.
UR_TEST(BrowserApprovalWiring_TheSheetsPagesHoldWhatTheyShow) {
  const std::string sheet = Sheet();
  UR_EXPECT_TRUE(ApprovalInOrder(
      FunctionBody(sheet, "void BrowserApprovalSheet::BuildUi() {"),
      {"pages_->add(*BuildStart(), \"start\");", "pages_->add(*BuildWaiting(), \"waiting\");",
       "pages_->add(*BuildSigningIn(), \"signing-in\");",
       "pages_->add(*BuildConfirm(), \"confirm\");",
       "pages_->add(*BuildMessage(), \"message\");"}));
  const std::string start =
      FunctionBody(sheet, "Gtk::Widget* BrowserApprovalSheet::BuildStart() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      start,
      {"actionButton_ = Gtk::make_managed<Gtk::Button>(ActionText());",
       "actionButton_->add_css_class(\"suggested-action\");",
       "actionButton_->signal_clicked().connect([this] { StartApproval(); });", "MakeHostLabel();",
       "startNote_->add_css_class(\"ur-error-text\");", "startNote_->set_visible(false);",
       "T_(\"or\", \"or\")", "either->add_css_class(\"dim-label\");", "BodyText()",
       "body->add_css_class(\"dim-label\");", "body->set_wrap(true);",
       "pasteEntry_->add_css_class(\"ur-input\");", "pasteEntry_->set_visibility(false);",
       "pasteEntry_->set_input_purpose(Gtk::InputPurpose::PASSWORD);",
       "pasteEntry_->set_placeholder_text(T_(\"auth_code\", \"Auth code\"));",
       "pasteSignIn_->set_sensitive(!TrimWhitespace(pasteEntry_->get_text()).empty());",
       "pasteEntry_->signal_activate().connect([this] { SubmitPastedCode(); });",
       "pasteSignIn_->set_sensitive(false);",
       "pasteSignIn_->signal_clicked().connect([this] { SubmitPastedCode(); });"}));
  UR_EXPECT_TRUE(Contains(FunctionBody(sheet, "Gtk::Label* MakeHostLabel() {"),
                          "Gtk::make_managed<Gtk::Label>(std::string(config::kApprovalHost));"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(sheet, "config::kApprovalHost"));
  // its definition, and the Start and Waiting pages
  UR_EXPECT_EQ(std::size_t{3}, CountOf(sheet, "MakeHostLabel()"));
  for (const char* forbidden : {"kApprovalPageUrl", "kApprovalDescriptorUrl", "BuildRequestUrl(",
                                "kBrowserApprovalEnabled", "ApprovalPageLists("}) {
    UR_EXPECT_TRUE_MSG(std::string("BrowserApprovalSheet.cpp names ") + forbidden,
                       !Contains(sheet, forbidden));
  }
  const std::string waiting =
      FunctionBody(sheet, "Gtk::Widget* BrowserApprovalSheet::BuildWaiting() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      waiting, {"Gtk::make_managed<Gtk::Spinner>();", "spinner->start();",
                "T_(\"waiting_for_approval\", \"Waiting for approval\")",
                "title->add_css_class(\"title-3\");", "MakeHostLabel()",
                "againButton_ = Gtk::make_managed<Gtk::Button>(ActionText());",
                "againButton_->signal_clicked().connect([this] { StartApproval(); });"}));
  UR_EXPECT_TRUE(ApprovalInOrder(
      FunctionBody(sheet, "Gtk::Widget* BrowserApprovalSheet::BuildSigningIn() {"),
      {"spinner->start();", "T_(\"sign_in\", \"Sign in\")", "GTK_ACCESSIBLE_PROPERTY_LABEL"}));
  UR_EXPECT_TRUE(ApprovalInOrder(
      FunctionBody(sheet, "Gtk::Widget* BrowserApprovalSheet::BuildConfirm() {"),
      {"T_(\"network_name_label\", \"Network name\")", "caption->add_css_class(\"dim-label\");",
       "caption->add_css_class(\"caption\");", "networkText_->add_css_class(\"title-2\");"}));
  UR_EXPECT_TRUE(
      ApprovalInOrder(FunctionBody(sheet, "Gtk::Widget* BrowserApprovalSheet::BuildMessage() {"),
                      {"T_(\"try_again\", \"Try again\")",
                       "tryAgain_->signal_clicked().connect([this] { ShowPage(Page::Start); });"}));
  const std::string show = FunctionBody(sheet, "void BrowserApprovalSheet::ShowPage(Page page) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      show, {"if (page != Page::Start) pasteEntry_->set_text(\"\");", "page_ = page;",
             "case Page::Start:", "startNote_->set_visible(false);", "case Page::SigningIn:",
             "signingInCancel_->set_visible(!confirmed_);", "case Page::Confirm:",
             "case Page::TimedOut:", "case Page::Failed:", "set_default_widget(*tryAgain_);"}));
  UR_EXPECT_TRUE(!Contains(show, "default:"));
  // the name the person says yes to is the network's own, sanitized, and is
  // the only thing ever set on the Confirm page's label
  UR_EXPECT_EQ(std::size_t{1}, CountOf(sheet, "networkText_->set_text("));
  UR_EXPECT_TRUE(Contains(
      FunctionBody(sheet,
                   "void BrowserApprovalSheet::ShowConfirm(const std::string& networkName) {"),
      "networkText_->set_text(networkName);"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(sheet, "ShowConfirm(name);"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(sheet, "SanitizeExternalDisplayText("));
  // the request: built, opened through the desktop's launcher with the
  // display's launch context, wiped; its error never read
  const std::string approve = FunctionBody(sheet, "void BrowserApprovalSheet::StartApproval() {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      approve,
      {"if (closed_) return;",
       "controller_.begin()",
       "controller_.requestUrl(*view)",
       "approval::Wipe(view->state);",
       "approval::Wipe(view->publicKey);",
       "if (url.empty()) {",
       "controller_.cancel(approval::ClearReason::Cancel);",
       "g_warning(\"browser sign-in sheet: the approval could not be started; the paste flow "
       "stays\");",
       "ShowPage(Page::Start);",
       "ShowStartNote(T_(\"something_went_wrong\",",
       "return;",
       "gdk_display_get_app_launch_context(get_display()->gobj());",
       "g_app_info_launch_default_for_uri(url.c_str(), G_APP_LAUNCH_CONTEXT(context), &error);",
       "g_object_unref(context);",
       "g_clear_error(&error);",
       "approval::Wipe(url);",
       "if (launched) {",
       "ShowPage(Page::Waiting);",
       "g_message(\"browser sign-in sheet: the browser was opened on the approval page\");",
       "return;",
       "controller_.cancel(approval::ClearReason::Cancel);",
       "g_warning(\"browser sign-in sheet: the browser could not be opened; the attempt ended\");",
       "ShowPage(Page::Start);",
       "ShowStartNote(T_(\"sso_browser_open_failed\","}));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(sheet, "g_app_info_launch_default_for_uri("));
  UR_EXPECT_TRUE(!Contains(sheet, "error->message"));
  // no text a widget shows is given the request or a value of the attempt
  for (const char* setter : {"set_text(", "set_label(", "set_tooltip_text(", "set_tooltip_markup(",
                             "set_markup(", "set_title("}) {
    const std::string needle = setter;
    for (size_t at = sheet.find(needle); at != std::string::npos; at = sheet.find(needle, at + 1)) {
      const std::vector<std::string> arguments = CallArguments(sheet, at + needle.size() - 1);
      for (const std::string& argument : arguments) {
        for (const char* secret : {"url", "view", "state", "publicKey", "code"}) {
          const std::string text = WithoutLiterals(argument);
          size_t found = text.find(secret);
          const bool standsAlone = found != std::string::npos &&
                                   (found == 0 || !IsWordChar(text[found - 1])) &&
                                   (found + std::string(secret).size() >= text.size() ||
                                    !IsWordChar(text[found + std::string(secret).size()]));
          UR_EXPECT_TRUE_MSG(needle + " is given " + secret, !standsAlone);
        }
      }
    }
  }
  // no clipboard: Linux offers no Copy link
  for (const char* clipboard : {"Clipboard", "clipboard", "copy_to_clipboard"}) {
    UR_EXPECT_TRUE_MSG(std::string("BrowserApprovalSheet.cpp has ") + clipboard,
                       !Contains(sheet, clipboard));
  }
}

// The sheet keeps no secret: no string member at all (no link, no code, no
// pasted text), and the pasted code is read, emptied from the field and handed
// on in one call.
UR_TEST(BrowserApprovalWiring_TheSheetHoldsNoString) {
  const std::string header = ReadCode("BrowserApprovalSheet.hpp");
  UR_EXPECT_TRUE_MSG("BrowserApprovalSheet.hpp",
                     Contains(header, "class BrowserApprovalSheet : public Gtk::Window {"));
  const size_t privateAt = header.find(" private:");
  UR_EXPECT_TRUE(privateAt != std::string::npos);
  const std::string members = header.substr(privateAt == std::string::npos ? 0 : privateAt);
  for (const char* type : {"std::string ", "Glib::ustring ", "std::optional<std::string>",
                           "std::vector<std::string>", "std::vector<char>", "std::string_view "}) {
    UR_EXPECT_TRUE_MSG(std::string("the sheet holds a ") + type, !Contains(members, type));
  }
  for (const char* name : {"link_", "url_", "code_", "pasted_", "state_"}) {
    UR_EXPECT_TRUE_MSG(std::string("the sheet holds ") + name, !Contains(header, name));
  }
  const std::string pasted =
      FunctionBody(Sheet(), "void BrowserApprovalSheet::SubmitPastedCode() {");
  UR_EXPECT_TRUE(
      ApprovalInOrder(pasted, {"std::string code = TrimWhitespace(pasteEntry_->get_text());",
                               "pasteEntry_->set_text(\"\");", "if (code.empty()) return;",
                               "Finish();", "controller_.signInWithCode(std::move(code));"}));
}

// The strings: the sheet looks up exactly the keys the port lists, every one
// tagged linux (CatalogLookupTest checks the tags and the English).
UR_TEST(BrowserApprovalWiring_TheSheetUsesOnlyTheStringsThatAreListed) {
  const std::string sheet = Sheet();
  std::set<std::string> used;
  for (size_t at = sheet.find("T_(\""); at != std::string::npos; at = sheet.find("T_(\"", at + 1)) {
    if (at > 0 && IsWordChar(sheet[at - 1])) continue;
    const size_t open = at + 4;
    const size_t close = sheet.find('"', open);
    used.insert(sheet.substr(open, close - open));
  }
  const std::set<std::string> listed = {"adv_browser_sign_in_title",
                                        "adv_open_ur_io",
                                        "adv_browser_sign_in_body",
                                        "sso_sign_in_timed_out",
                                        "sso_browser_open_failed",
                                        "or",
                                        "auth_code",
                                        "sign_in",
                                        "cancel",
                                        "waiting_for_approval",
                                        "network_name_label",
                                        "try_again",
                                        "something_went_wrong",
                                        "auth_code_error"};
  for (const std::string& key : used) {
    UR_EXPECT_TRUE_MSG("the sheet looks up " + key, listed.count(key) != 0);
  }
  for (const std::string& key : listed) {
    UR_EXPECT_TRUE_MSG("the sheet does not look up " + key, used.count(key) != 0);
  }
  UR_EXPECT_TRUE(!Contains(sheet, "TN_("));
  // the action's English, and the window adds only the stray line's key
  UR_EXPECT_TRUE(Contains(sheet, "T_(\"adv_open_ur_io\", \"Open ur.io\")"));
  for (const char* unused : {"adv_sign_in_browser", "copy_to_clipboard"}) {
    for (const std::string& code : {sheet, Window()}) {
      UR_EXPECT_TRUE_MSG(std::string("the sign-in looks up ") + unused,
                         !Contains(code, std::string("T_(\"") + unused + "\""));
    }
  }
  UR_EXPECT_TRUE(
      Contains(FunctionBody(Window(), "void MainWindow::ShowStrayNotice(bool ownLaunch) {"),
               "T_(\"auth_code_error\""));
}

// ---- the paste ---------------------------------------------------------------

// A pasted code has one way in, SignInWithAuthCode, from the dialog and from
// the sheet; the dialog stays the flow wherever the approval is not offered,
// and both fields mask the code.
UR_TEST(BrowserApprovalWiring_TheCodeLoginIsOnlyReachedFromTheSheets) {
  const std::string window = Window();
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "LoginWithCode("));
  const std::string signIn =
      FunctionBody(window, "void MainWindow::SignInWithAuthCode(std::string code) {");
  UR_EXPECT_TRUE(ApprovalInOrder(
      signIn, {"code = TrimWhitespace(code);", "if (code.empty()) return;", "SetLoginBusy(true);",
               "host_.LoginWithCode(code, [this](AuthResult r) {", "PostToMain([this, r] {",
               "SetLoginBusy(false);", "if (!r.ok) {",
               "T_(\"code_sign_in_failed\", \"Code sign in failed\")",
               "StartTunnelUi(\"code sign-in\");"}));
  for (const auto& [name, code] : Sources()) {
    if (name == "MainWindow.cpp" || name == "SdkHost.cpp" || name == "SdkHost.hpp") continue;
    UR_EXPECT_TRUE_MSG(name + " signs in with a code", !Contains(code, "LoginWithCode("));
  }
  // the definition and its two callers
  UR_EXPECT_EQ(std::size_t{3}, CountOf(window, "SignInWithAuthCode("));
  const std::string dialog = FunctionBody(window, "void MainWindow::ShowAuthCodeDialog() {");
  UR_EXPECT_TRUE(Contains(dialog, "ctx->self->SignInWithAuthCode(ctx->field->get_text());"));
  UR_EXPECT_TRUE(Contains(
      FunctionBody(window,
                   "BrowserApprovalSheet::Controller MainWindow::ApprovalSheetController() {"),
      "controller.signInWithCode = [this](std::string code) { SignInWithAuthCode(std::move(code)); "
      "};"));
  UR_EXPECT_TRUE(ApprovalInOrder(
      dialog, {"T_(\"auth_code_login_sheet_header\", \"Auth code login\")",
               "field->add_css_class(\"ur-input\");", "field->set_visibility(false);",
               "field->set_input_purpose(Gtk::InputPurpose::PASSWORD);",
               "field->set_placeholder_text(T_(\"auth_code\", \"Auth code\"));",
               "T_(\"cancel\", \"Cancel\")",
               "T_(\"auth_code_login_button_text\", \"Log in with Auth Code\")",
               "adw_message_dialog_set_default_response(ADW_MESSAGE_DIALOG(dialog), \"login\");",
               "if (g_strcmp0(response, \"login\") != 0) return;"}));
  UR_EXPECT_TRUE(Contains(Squash(FunctionBody(window, "void MainWindow::OnUseCode() {")),
                          "{ loginError_.set_text(\"\"); if (!ApprovalOffered()) { "
                          "ShowAuthCodeDialog(); return; } ShowApprovalSheet(std::string()); }"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(window, "ShowAuthCodeDialog();"));
  // the login tile is still the entry, and no new link was added
  UR_EXPECT_TRUE(Contains(window, "sigc::mem_fun(*this, &MainWindow::OnUseCode)"));
  const std::string sheet = Sheet();
  UR_EXPECT_TRUE(Contains(sheet, "pasteEntry_->set_visibility(false);"));
  UR_EXPECT_TRUE(Contains(sheet, "pasteEntry_->set_input_purpose(Gtk::InputPurpose::PASSWORD);"));
}

// ---- the log -----------------------------------------------------------------

// The files that receive, route or open a link, or hold a secret of an attempt,
// log closed words only: no log call is given a link, a value of one, a key of
// an attempt, the pasted text, the command line or a launcher's error text.
UR_TEST(BrowserApprovalWiring_NoLogCallReceivesALinkOrASecret) {
  int checked = 0;
  const auto scan = [&checked](const std::string& name, const std::string& code) {
    UR_EXPECT_TRUE_MSG(name + " was read", !code.empty());
    if (code.empty()) return;
    ++checked;
    for (const std::string& leak : LogLeaks(code)) {
      UR_FAIL(name + ": " + leak + ": a link and the values in it are secrets; log a closed word");
    }
  };
  for (const char* name :
       {"BrowserApprovalSheet.cpp", "ApprovalCheck.cpp", "SchemeHandler.cpp", "main.cpp"}) {
    scan(name, ReadCode(name));
  }
  scan("MainWindow.cpp's approval functions", Bodies(Window(), WindowApprovalFunctions()));
  scan("SdkHost.cpp's approval functions",
       Bodies(ReadCode("SdkHost.cpp"), HostApprovalFunctions()));
  UR_EXPECT_EQ(6, checked);

  // the check itself: the Windows app's examples, as this app's log calls
  for (const char* bad : {
           "g_message(\"app: deep link %s\", url.c_str());",
           "g_warning(\"app: %s\",\n          request.deepLink.c_str());",
           "g_message(\"x %s %s\", ToString(verdict), link.state.c_str());",
           "g_critical(\"x %s\", Narrow(code).c_str());",
           "g_debug(\"sealed %s\", view.publicKey.c_str());",
           "g_warning(\"keys %s\", keys->secretKey.c_str());",
           "std::fprintf(stderr, \"%s\\n\", url.c_str());",
           "fprintf(stderr, \"[sso] %s\", jwt.c_str());",
           "std::cerr << \"link \" << uri << std::endl;",
           "g_warning(\"launch failed: %s\", error->message);",
           "printf(\"%s\\n\", argv[1]);",
           "g_info(\"%s\", pasted.c_str());",
           "g_message(\"%zu uris: %s\", launch.uris.size(), launch.uris[0].c_str());",
           "g_message(\"%s\", byJwt.c_str());",
       }) {
    UR_EXPECT_TRUE_MSG(std::string("the check does not flag ") + bad,
                       !LogLeaks(BlankComments(bad)).empty());
  }
  for (const char* good : {
           "g_message(\"app: auth link: %.*s (%.*s)\", static_cast<int>(verdict.size()), "
           "verdict.data(), static_cast<int>(why.size()), why.data());",
           "g_message(\"the code and the url are never logged\");",
           "g_warning(\"x: %s (%d)\", e.what(), static_cast<int>(e.code()));",
           "g_message(\"startup: %s\", line.c_str());",
           "g_message(\"tunnel %s\", status.state.c_str());",
           "g_message(\"a %zu b %zu\", data.size(), url.length());",
           "g_message(\"approval: the attempt ended: %.*s\", static_cast<int>(word.size()), "
           "word.data());",
           "std::fprintf(stderr, \"[sso] rejected return for %s: %s\\n\", provider.c_str(), why);",
           "std::fprintf(file, \"%s\", url.c_str());",
           "snprintf(buffer, sizeof buffer, \"%s\", url.c_str());",
           "g_message(\"approval: an attempt started (version %d)\", "
           "static_cast<int>(approval::ContractFor(network.kind)));",
       }) {
    const std::vector<std::string> leaks = LogLeaks(BlankComments(good));
    UR_EXPECT_TRUE_MSG(
        std::string("the check flags ") + good + (leaks.empty() ? "" : ": " + leaks[0]),
        leaks.empty());
  }
}

// What the approval removed or never had stays gone: no check code, Export logs
// copies through the redaction only.
UR_TEST(BrowserApprovalWiring_NoDeadApprovalCodeIsLeftBehind) {
  for (const auto& [name, code] : Sources()) {
    for (const char* gone : {"CheckCode", "check_code", "XJ4BNP", "IsAuthDeepLink(",
                             "DeepLinkQueryParam(", "approval()."}) {
      UR_EXPECT_TRUE_MSG(name + " has " + gone, !Contains(code, gone));
    }
  }
  const std::string save =
      FunctionBody(ReadCode("SettingsPage.cpp"), "void SettingsPage::SaveLogsToFile() {");
  UR_EXPECT_TRUE(Contains(save, "logexport::CopyWithoutLinks("));
  UR_EXPECT_TRUE(!Contains(save, "g_file_copy"));
}
