// A start that failed before anything was on screen (main.cpp):
// SdkHost::Initialize threw, so there is no window and no tray, and an app
// launched from the desktop vanished without a word, its only report a line
// on a stderr nobody reads. The failure is still written to stderr, for a
// terminal launch and the journal, and is now also shown in a dialog that
// names the log folder and quotes the error; the process exits with status 1
// once the dialog is closed. Without a display the stderr line is all there is.
//
// The text is decided here, free of GTK (tests/StartupFailureTest.cpp); Show
// (StartupFailure.cpp) puts it on screen.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

namespace Gtk {
class Application;
}

namespace urnw::startup_failure {

// main's exit status for a start that failed
inline constexpr int kExitStatus = 1;

// The dialog's body: the sentence naming the log folder, then the error as the
// SDK gave it, untranslated, when it gave one.
inline std::string Body(const std::string& sentence, const std::string& error) {
  return error.empty() ? sentence : sentence + "\n\n" + error;
}

// The stderr line, written whether or not the dialog can show.
inline std::string StderrLine(const std::string& error, const std::string& logDir) {
  return "urnetwork: could not start: " + (error.empty() ? std::string("unknown error") : error) +
         " (logs in " + logDir + ")\n";
}

// Shows the failure in a dialog of its own, which joins `app` so the app runs
// until it is closed; closing it quits. False when there is no display to show
// it on, and the caller quits.
bool Show(Gtk::Application& app, const std::string& error, const std::string& logDir);

}  // namespace urnw::startup_failure
