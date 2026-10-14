// The approval page's descriptor check as wired (ApprovalCheck.cpp): one GET of
// the build constant, made on a worker thread with a session of its own, no
// redirect, no credentials, no decompression, three seconds in all, the body
// capped, the answer kept for the run, and only closed words in the log. And
// the build constants it reads (Config.hpp): the approval is off, the page's
// origin is the real page's unless a developer's compiler command line says
// otherwise, and nothing in this repository says otherwise. The check needs
// libsoup, so this reads the sources with their comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::CountOf;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;
using urnw::testing::wiring::ReadRaw;

namespace {

// Each needle occurs after the one before it.
bool CheckInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

}  // namespace

// One GET of the descriptor constant, bounded on every side.
UR_TEST(ApprovalCheckWiring_TheCheckIsOneBoundedRequest) {
  const std::string check = ReadCode("ApprovalCheck.cpp");
  const std::string probe = FunctionBody(check, "std::string_view Probe(std::uint32_t& listed)");
  UR_EXPECT_TRUE_MSG("Probe", !probe.empty());
  UR_EXPECT_TRUE(
      Contains(probe, "g_uri_parse(config::kApprovalDescriptorUrl, G_URI_FLAGS_NONE, nullptr)"));
  UR_EXPECT_TRUE(Contains(probe, "return \"bad-url\";"));
  // the session is the worker's own, sends no credentials and decodes nothing
  UR_EXPECT_TRUE(Contains(probe, "hold.session = soup_session_new();"));
  UR_EXPECT_TRUE(Contains(
      probe, "soup_session_set_user_agent(hold.session, \"URnetwork-Linux/\" UR_APP_VERSION);"));
  UR_EXPECT_TRUE(Contains(probe, "soup_session_set_timeout(hold.session, kBudgetSeconds);"));
  UR_EXPECT_TRUE(Contains(
      check,
      "constexpr guint kBudgetSeconds = static_cast<guint>(\n"
      "    std::chrono::duration_cast<std::chrono::seconds>(approval::kCheckBudget).count());"));
  UR_EXPECT_TRUE(Contains(
      probe, "soup_session_remove_feature_by_type(hold.session, SOUP_TYPE_AUTH_MANAGER);"));
  UR_EXPECT_TRUE(Contains(
      probe, "soup_session_remove_feature_by_type(hold.session, SOUP_TYPE_CONTENT_DECODER);"));
  // the one request: GET, no redirect, json asked for
  UR_EXPECT_EQ(std::size_t{1}, CountOf(check, "soup_message_new("));
  UR_EXPECT_TRUE(
      Contains(probe, "soup_message_new(SOUP_METHOD_GET, config::kApprovalDescriptorUrl);"));
  UR_EXPECT_TRUE(
      Contains(probe, "soup_message_add_flags(hold.message, SOUP_MESSAGE_NO_REDIRECT);"));
  UR_EXPECT_TRUE(Contains(
      probe,
      "soup_message_headers_append(soup_message_get_request_headers(hold.message), \"Accept\",\n"
      "                              \"application/json\");"));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(check, "soup_session_send("));
  // the whole check's budget: a deadline on the main loop cancels what is still running
  UR_EXPECT_TRUE(Contains(
      check, "constexpr guint kBudgetMs = static_cast<guint>(approval::kCheckBudget.count());"));
  UR_EXPECT_TRUE(CheckInOrder(
      probe,
      {"hold.cancellable = g_cancellable_new();",
       "hold.deadline = g_timeout_source_new(kBudgetMs);",
       "g_source_set_callback(hold.deadline, CancelOnDeadline, g_object_ref(hold.cancellable),",
       "g_source_attach(hold.deadline, nullptr);",
       "soup_session_send(hold.session, hold.message, hold.cancellable, &error);"}));
  UR_EXPECT_TRUE(Contains(FunctionBody(check, "gboolean CancelOnDeadline(gpointer cancellable)"),
                          "g_cancellable_cancel(G_CANCELLABLE(cancellable));"));
  UR_EXPECT_TRUE(
      Contains(check, "std::chrono::steady_clock::now() - started > approval::kCheckBudget"));
  // what the probe holds is let go on every way out, the deadline first
  const std::string release = FunctionBody(check, "~ProbeHold()");
  UR_EXPECT_TRUE(CheckInOrder(
      release,
      {"g_source_destroy(deadline);", "g_source_unref(deadline);",
       "g_input_stream_close(body, nullptr, nullptr);", "g_object_unref(body);",
       "g_object_unref(message);", "g_object_unref(session);", "g_object_unref(cancellable);"}));
  UR_EXPECT_TRUE(Contains(check, "ProbeHold(const ProbeHold&) = delete;"));
  // the answer: a 200, its raw content type, a capped body, the descriptor's judgment
  UR_EXPECT_TRUE(
      Contains(probe, "if (soup_message_get_status(hold.message) != 200) return \"bad-status\";"));
  UR_EXPECT_TRUE(Contains(probe,
                          "soup_message_headers_get_one(soup_message_get_response_headers(hold."
                          "message), \"Content-Type\");"));
  UR_EXPECT_TRUE(!Contains(check, "soup_message_headers_get_content_type"));
  UR_EXPECT_TRUE(CheckInOrder(
      probe, {"for (;;) {", "if (tooSlow()) return \"too-slow\";",
              "g_input_stream_read(hold.body, chunk, sizeof chunk, hold.cancellable, &error);",
              "return tooSlow() ? \"too-slow\" : \"unreadable\";", "if (n == 0) break;",
              "if (body.size() > approval::kMaxDescriptorBytes) break;", "}",
              "if (tooSlow()) return \"too-slow\";",
              "approval::ReadDescriptor(contentType ? contentType : \"\", body);",
              "listed = answer.contracts;", "return approval::ToString(answer.verdict);"}));
  // nothing else is sent, and the certificate is checked as GIO checks every one
  for (const char* forbidden :
       {"SOUP_METHOD_POST", "SOUP_METHOD_PUT", "soup_message_set_request_body",
        "accept-certificate", "tls-database", "set_tls_database", "G_TLS_CERTIFICATE",
        "soup_session_add_feature", "SoupCookieJar", "soup_cookie_jar", "restarted"}) {
    UR_EXPECT_TRUE_MSG(std::string("ApprovalCheck.cpp has ") + forbidden,
                       !Contains(check, forbidden));
  }
  // the worker touches no widget
  for (const char* gtk : {"<gtk", "<gtkmm", "Gtk::", "Glib::", "gtk_", "PostToMain"}) {
    UR_EXPECT_TRUE_MSG(std::string("ApprovalCheck.cpp has ") + gtk, !Contains(check, gtk));
  }
}

// The one check of a run starts once, keeps its answer, and publishes the versions before the
// state that says Live.
UR_TEST(ApprovalCheckWiring_TheAnswerIsKeptForTheRun) {
  const std::string check = ReadCode("ApprovalCheck.cpp");
  const std::string start = FunctionBody(check, "void StartApprovalPageCheck()");
  UR_EXPECT_TRUE(CheckInOrder(
      start, {"int expected = static_cast<int>(ApprovalPage::NotChecked);",
              "if (!g_state.compare_exchange_strong(expected, "
              "static_cast<int>(ApprovalPage::Checking))) return;",
              "try {", "std::thread(Run).detach();", "} catch (const std::system_error&) {",
              "g_state.store(static_cast<int>(ApprovalPage::Down), std::memory_order_release);",
              "g_warning(\"approval: the page check could not start\");"}));
  const std::string run = FunctionBody(check, "void Run()");
  UR_EXPECT_TRUE(CheckInOrder(
      run, {"const std::string_view word = Probe(listed);",
            "const bool live = word == approval::ToString(approval::DescriptorVerdict::Offered);",
            "g_contracts.store(live ? listed : 0U, std::memory_order_release);",
            "g_state.store(static_cast<int>(live ? ApprovalPage::Live : ApprovalPage::Down),",
            "g_message(\"approval: the page check: %.*s\", static_cast<int>(word.size()), "
            "word.data());"}));
  UR_EXPECT_TRUE(Before(run, "g_contracts.store(", "g_state.store("));
  // success and failure alike are kept: nothing sets the state back to NotChecked
  UR_EXPECT_EQ(std::size_t{2}, CountOf(check, "ApprovalPage::NotChecked"));
  UR_EXPECT_TRUE(Contains(check, "std::atomic<std::uint32_t> g_contracts{0};"));
  UR_EXPECT_EQ(std::size_t{3}, CountOf(check, "g_contracts"));
  const std::string lists = FunctionBody(check, "bool ApprovalPageLists(std::int64_t contract)");
  UR_EXPECT_TRUE(
      CheckInOrder(lists, {"if (ApprovalPageState() != ApprovalPage::Live) return false;",
                           "answer.contracts = g_contracts.load(std::memory_order_acquire);",
                           "return answer.Lists(contract);"}));
  UR_EXPECT_TRUE(Contains(FunctionBody(check, "ApprovalPage ApprovalPageState()"),
                          "g_state.load(std::memory_order_acquire)"));
}

// Root, or set-uid: the answer's handover could never reach this process.
UR_TEST(ApprovalCheckWiring_ARootProcessIsElevated) {
  const std::string check = ReadCode("ApprovalCheck.cpp");
  UR_EXPECT_TRUE(Contains(FunctionBody(check, "bool ProcessIsElevated()"),
                          "return ::geteuid() == 0 || ::getuid() != ::geteuid();"));
  UR_EXPECT_TRUE(Contains(check, "#include <unistd.h>"));
}

// The log gets closed words: two lines, the page check's word or that it could not start. The
// words are hyphenated or plain, and none is a key of the store.
UR_TEST(ApprovalCheckWiring_TheLogGetsClosedWords) {
  const std::string check = ReadCode("ApprovalCheck.cpp");
  UR_EXPECT_EQ(std::size_t{1}, CountOf(check, "g_message("));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(check, "g_warning("));
  for (const char* other :
       {"g_critical(", "g_debug(", "g_info(", "printf(", "std::cerr", "error->message"}) {
    UR_EXPECT_TRUE_MSG(std::string("ApprovalCheck.cpp has ") + other, !Contains(check, other));
  }
  std::set<std::string> keys;
  std::istringstream lines(ReadRaw("../po/linux-keys.txt"));
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] != '#') keys.insert(line);
  }
  UR_EXPECT_TRUE_MSG("po/linux-keys.txt was read", keys.size() > 500);
  for (const char* word : {"bad-url", "no-connection", "too-slow", "bad-status", "unreadable"}) {
    UR_EXPECT_TRUE_MSG(std::string("Probe answers ") + word,
                       Contains(check, std::string("\"") + word + "\""));
    UR_EXPECT_TRUE_MSG(std::string("the word ") + word + " is a key of the store",
                       keys.count(word) == 0);
  }
}

// The approval is off in this build, and its page's origin is a build constant.
UR_TEST(ApprovalCheckWiring_TheApprovalIsOffAndItsOriginIsABuildConstant) {
  const std::string config = ReadCode("Config.hpp");
  for (const char* line : {
           "inline constexpr bool kBrowserApprovalEnabled = false;",
           "#if defined(UR_APPROVAL_TEST_ORIGIN)\n#define UR_APPROVAL_ORIGIN "
           "UR_APPROVAL_TEST_ORIGIN\n"
           "inline constexpr bool kApprovalOriginIsATest = true;\n#else\n"
           "#define UR_APPROVAL_ORIGIN \"https://beta.app.ur.network\"\n"
           "inline constexpr bool kApprovalOriginIsATest = false;\n#endif",
           "inline constexpr char kApprovalPageUrl[] = UR_APPROVAL_ORIGIN \"/login-app\";",
           "inline constexpr char kApprovalDescriptorUrl[] = UR_APPROVAL_ORIGIN "
           "\"/approval-contract.json\";",
           "#undef UR_APPROVAL_ORIGIN",
           "inline constexpr std::string_view kApprovalHost = HostOfUrl(kApprovalPageUrl);",
           "static_assert(HostOfUrl(\"https://beta.app.ur.network/login-app\") == "
           "\"beta.app.ur.network\");",
           "static_assert(HostOfUrl(\"http://localhost:5173\") == \"localhost:5173\");",
           "static_assert(HostOfUrl(\"no scheme\").empty());",
           "#if !defined(UR_APPROVAL_TEST_ORIGIN)\nstatic_assert(!kApprovalOriginIsATest);",
           "static_assert(std::string_view(kApprovalPageUrl) == "
           "\"https://beta.app.ur.network/login-app\");",
           "static_assert(std::string_view(kApprovalDescriptorUrl) ==\n"
           "              \"https://beta.app.ur.network/approval-contract.json\");",
           "static_assert(kApprovalHost == \"beta.app.ur.network\");",
           "namespace urnw::config {",
       }) {
    UR_EXPECT_TRUE_MSG(std::string("Config.hpp is missing ") + line, Contains(config, line));
  }
  UR_EXPECT_EQ(std::size_t{1}, CountOf(config, "kBrowserApprovalEnabled"));
  // set on each branch, and asserted false for the real page's
  UR_EXPECT_EQ(std::size_t{3}, CountOf(config, "kApprovalOriginIsATest"));
  // the wallet id stays where it was
  UR_EXPECT_TRUE(Contains(
      config,
      "inline constexpr const char* kWalletConnectProjectId = UR_WALLETCONNECT_PROJECT_ID;"));
}

// Config.hpp sets the two flags; the window (MainWindow.cpp) is the one source that may read
// them, to decide whether the approval is offered and to say at start that a build talks to a
// test origin.
UR_TEST(ApprovalCheckWiring_OnlyTheWindowReadsTheFlags) {
  const std::filesystem::path src(UR_SRC_DIR);
  int scanned = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
    if (!entry.is_regular_file()) continue;
    const std::string ext = entry.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
    ++scanned;
    const std::string name = std::filesystem::relative(entry.path(), src).string();
    if (name == "Config.hpp" || name == "MainWindow.cpp") continue;
    const std::string code = ReadCode(name);
    for (const char* flag :
         {"kBrowserApprovalEnabled", "kApprovalOriginIsATest", "UR_APPROVAL_TEST_ORIGIN"}) {
      UR_EXPECT_TRUE_MSG(name + " reads " + flag, !Contains(code, flag));
    }
    if (name != "ApprovalCheck.cpp" && name != "ApprovalCheck.hpp") {
      UR_EXPECT_TRUE_MSG(name + " asks what the page lists", !Contains(code, "ApprovalPageLists("));
    }
  }
  UR_EXPECT_TRUE_MSG("the scan saw the sources", scanned > 10);
}

// No file of the repository that builds, packages or releases the app defines the override: a
// shipped build always talks to the real page.
UR_TEST(ApprovalCheckWiring_NothingHereDefinesTheTestOrigin) {
  const std::filesystem::path src(UR_SRC_DIR);
  int read = 0;
  const auto clean = [&read](const std::filesystem::path& path) {
    std::error_code unknown;
    if (!std::filesystem::is_regular_file(path, unknown)) return;
    ++read;
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    UR_EXPECT_TRUE_MSG(path.string() + " names UR_APPROVAL_TEST_ORIGIN",
                       !Contains(buffer.str(), "UR_APPROVAL_TEST_ORIGIN"));
  };
  for (const char* file : {"../meson.build", "../meson_options.txt", "../../build.sh",
                           "../po/meson.build", "../../test-main.sh"}) {
    clean(src / file);
  }
  UR_EXPECT_TRUE_MSG("meson.build, meson_options.txt and build.sh were read", read >= 3);
  for (const char* dir : {"../packaging", "../../packaging", "../scripts"}) {
    int inDir = 0;
    std::error_code unknown;
    for (std::filesystem::recursive_directory_iterator it(src / dir, unknown), end;
         !unknown && it != end; it.increment(unknown)) {
      if (!it->is_regular_file()) continue;
      ++inDir;
      clean(it->path());
    }
    UR_EXPECT_TRUE_MSG(std::string(dir) + " was read", inDir > 0);
  }
}
