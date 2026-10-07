// A start that failed before anything was on screen (StartupFailure.hpp): the
// error goes to stderr with the log folder, to a dialog that names the folder
// and quotes the error, and main exits with status 1. The dialog and main need
// GTK, so the wiring cases read main.cpp and StartupFailure.cpp with the
// comments blanked.
// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "StartupFailure.hpp"
#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

namespace startup_failure = urnw::startup_failure;

// A C++ source with every // comment blanked; string literals are kept.
std::string ReadStartupSource(const std::string& relative) {
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

// The text from `from` to the first `to` after it, or nothing.
std::string StartupSpan(const std::string& text, const std::string& from, const std::string& to) {
  const size_t start = text.find(from);
  if (start == std::string::npos) return std::string();
  const size_t end = text.find(to, start + from.size());
  return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

// Each needle occurs after the one before it.
bool StartupInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

}  // namespace

UR_TEST(StartupFailure_TheBodyNamesTheLogsThenQuotesTheError) {
  UR_EXPECT_TRUE(startup_failure::Body("Logs are in /x.", "storage: permission denied") ==
                 "Logs are in /x.\n\nstorage: permission denied");
  // no error text: the sentence alone, no empty paragraph
  UR_EXPECT_TRUE(startup_failure::Body("Logs are in /x.", "") == "Logs are in /x.");
}

UR_TEST(StartupFailure_StderrSaysWhyAndWhere) {
  UR_EXPECT_TRUE(startup_failure::StderrLine("bad dir", "/home/u/.local/state/urnetwork") ==
                 "urnetwork: could not start: bad dir (logs in /home/u/.local/state/urnetwork)\n");
  UR_EXPECT_TRUE(startup_failure::StderrLine("", "/s") ==
                 "urnetwork: could not start: unknown error (logs in /s)\n");
  UR_EXPECT_EQ(1, startup_failure::kExitStatus);
}

// A failed Initialize writes stderr, refuses later launches, shows the dialog
// (quitting at once when it cannot), and main returns the failure's status.
UR_TEST(StartupFailureWiring_AFailedStartIsShownAndEndsInStatusOne) {
  const std::string main = ReadStartupSource("main.cpp");
  const std::string failed = StartupSpan(main, "if (!host->Initialize(storageDir, logDir)) {", "adw_init();");
  UR_EXPECT_TRUE(StartupInOrder(
      failed, {"startFailed = true;",
               "g_printerr(\"%s\", urnw::startup_failure::StderrLine(host->InitializeError(), logDir).c_str());",
               "urnw::instance::BeginExiting();",
               "if (!urnw::startup_failure::Show(*app, host->InitializeError(), logDir)) app->quit();",
               "return;"}));
  UR_EXPECT_TRUE(StartupInOrder(
      main, {"const int status = app->run(argc, argv);",
             "return startFailed ? urnw::startup_failure::kExitStatus : status;"}));
  const std::string host = ReadStartupSource("SdkHost.cpp");
  const std::string init = StartupSpan(host, "bool SdkHost::Initialize(", "\n}\n");
  UR_EXPECT_TRUE(StartupInOrder(init, {"initializeError_.clear();", "catch (const std::exception& e) {",
                                       "initializeError_ = e.what();", "return false;"}));
}

// The dialog: only with a display, adwaita initialized, the heading and the
// body from the store, one Close, joined to the app so it keeps it running,
// and any response quits.
UR_TEST(StartupFailureWiring_TheDialogNamesTheFailureAndClosingQuits) {
  const std::string dialog = ReadStartupSource("StartupFailure.cpp");
  UR_EXPECT_TRUE(StartupInOrder(
      dialog, {"if (gdk_display_get_default() == nullptr) return false;", "adw_init();",
               "T_(\"app_start_failed_detail\", \"URnetwork could not start. Its logs are in {}.\")",
               "error);", "adw_message_dialog_new(",
               "T_(\"something_went_wrong\", \"Something went wrong.\"), body.c_str());",
               "T_(\"close\", \"Close\")", "adw_message_dialog_set_close_response(",
               "gtk_window_set_application(GTK_WINDOW(dialog), app.gobj());",
               "\"response\"", "g_application_quit(G_APPLICATION(application));",
               "gtk_window_present(GTK_WINDOW(dialog));", "return true;"}));
}
