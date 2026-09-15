// How much memory this daemon may actually use, measured once.
//
// The memory tier (TunnelPolicy.hpp) is chosen from this number: a host
// measuring more than 8 GiB takes the large device target and process budget,
// anything else -- INCLUDING a host whose memory cannot be determined -- takes
// the base pair. An unknown host is not a large host.
//
// The measurement is cached for the life of the process, and that is the point
// rather than an optimisation: the process budget is set at startup
// (daemon/main.cpp) and the device target when a tunnel is created
// (daemon/TunnelHost.cpp), and those two must come from the SAME tier. A second
// read could land on a changed cgroup limit and pair a large target with a
// small budget, which is the one arrangement both constraints exist to forbid.
//
// The reads live here; everything that decides anything is pure and lives in
// TunnelPolicy.hpp, where the unit tests reach it.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

namespace urnw {

// Usable host memory in bytes, or 0 when it cannot be determined: the smaller
// of /proc/meminfo MemTotal and any cgroup memory limit (v2 memory.max, then
// v1 memory.limit_in_bytes). Measured on the first call, cached after; safe to
// call from any thread.
std::int64_t HostMemoryByteCountCached();

}  // namespace urnw
