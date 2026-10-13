// The operating system's random source, for the secrets the browser approval
// draws (UrIoApproval.hpp's Slot::Begin takes it as its Random). The contract
// (docs/app-approval-contract.md, section 2) asks for the state to come from the
// OS random source: on Linux that is getrandom(2), which blocks only until the
// kernel's pool is first seeded. A call can be interrupted by a signal or fill
// less than it was asked for, so this asks again until every byte is drawn.
// GLib's GRand (g_random_*, and g_uuid_string_random on top of it) is a Mersenne
// Twister seeded once, and is not that.
//
// Header-only, so tests/OsRandomTest.cpp draws from it on any host.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <sys/random.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>

namespace urnw::osrandom {

// The attempt's state comes from the OS random source (contract section 2): getrandom(2), never
// GLib's GRand. Fills `length` bytes at `out`, or answers false and the caller starts nothing.
inline bool RandomBytes(unsigned char* out, std::size_t length) {
#if defined(__linux__)
  std::size_t done = 0;
  while (done < length) {
    const ssize_t n = ::getrandom(out + done, length - done, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    done += static_cast<std::size_t>(n);
  }
  return true;
#else  // a developer's test build elsewhere (macOS): getentropy, 256 bytes a call
  for (std::size_t done = 0; done < length;) {
    const std::size_t n = std::min<std::size_t>(256, length - done);
    if (::getentropy(out + done, n) != 0) return false;
    done += n;
  }
  return true;
#endif
}

}  // namespace urnw::osrandom
