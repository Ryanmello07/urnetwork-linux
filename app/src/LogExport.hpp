// Export logs (Settings, SettingsPage::SaveLogsToFile): the app's log, copied to
// where the person asked for it, with every urnetwork: link in it reduced to its
// host and a length (authlink::RedactLogText).
//
// A urnetwork: link can carry a sign-in return (an identity token, the browser
// approval's sealed code) or a wallet's answer. The app logs no link, but the log
// is the sdk's glog too, and the file a person exports is theirs to send to
// someone, so a link that reached the log by any way is copied without its values.
// The log itself is never opened for writing, renamed or removed.
//
// Pure standard C++ with no GLib header: tests/LogExportTest.cpp runs it on any
// host. The Windows app's LogExport.h, but for the first line of CopyWithoutLinks:
// libstdc++ opens a directory as a stream, which fails only at its first read.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "AuthLink.hpp"

namespace urnw::logexport {

// The log, line by line, through authlink::RedactLogText, written to `destination`. False when
// either file could not be used.
//
// Nothing is opened for writing before the source has been found to be a file, has been read
// open and `destination` has been found not to be the source: the picker suggests the log's own
// name, a file opened to be written is emptied first, and saving over the log would then empty the
// live log and say it had saved. A source that cannot be opened leaves an existing destination as
// it was.
inline bool CopyWithoutLinks(const std::filesystem::path& source,
                             const std::filesystem::path& destination) {
  std::error_code unknown;
  // a directory opens as a stream under libstdc++ and only fails at the first read
  if (!std::filesystem::is_regular_file(source, unknown)) return false;
  std::ifstream in(source, std::ios::binary);
  if (!in) return false;
  if (std::filesystem::equivalent(source, destination, unknown)) return false;
  std::ofstream out(destination, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  std::string line;
  while (std::getline(in, line)) {
    out << authlink::RedactLogText(line);
    if (!in.eof()) out << '\n';  // the last line of a log may end without one
  }
  out.flush();
  return !in.bad() && out.good();
}

}  // namespace urnw::logexport
