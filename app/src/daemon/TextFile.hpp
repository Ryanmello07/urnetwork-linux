// The whole of a small file the daemon reads: a /proc or /sys entry, or one of
// its own state files. Empty when the file cannot be opened or read.
//
// A read can fail after the open succeeded. sysfs answers EINVAL for an
// attribute the driver does not report (/sys/class/net/<if>/speed of most
// Wi-Fi interfaces) and for carrier or speed of an interface that is
// administratively down. A directory opens too, and its first read fails with
// EISDIR. libstdc++'s filebuf throws std::ios_base::failure from underflow
// when read(2) fails, and istreambuf_iterator calls the buffer directly,
// outside any input function, so nothing catches the throw and turns it into
// badbit. Uncaught on the main loop it ended the daemon: TunnelHost::Reap
// reads the default route's speed on every tick a device is live, and a Wi-Fi
// laptop that provides restarted every 5 s. The daemon's /proc, /sys and
// state-file reads go through ReadAll; the key material is the one exception
// (TunnelHost's ReadFileBytes), where an unreadable file must not look like a
// missing one.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <exception>
#include <fstream>
#include <iterator>
#include <string>

namespace urnw::textfile {

inline std::string ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  try {
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } catch (const std::exception&) {
    return {};
  }
}

}  // namespace urnw::textfile
