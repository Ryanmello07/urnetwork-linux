// SPDX-License-Identifier: MPL-2.0
#include "daemon/HostMemory.hpp"

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#include "TunnelPolicy.hpp"

namespace urnw {
namespace {

// Small pseudo-files (/proc, /sys). A missing or unreadable file is an empty
// string, which every parser here reads as "unknown".
std::string ReadSmallFile(const char* path) {
  std::ifstream in(path);
  if (!in) return {};
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::int64_t MeasureHostMemoryByteCount() {
  const std::int64_t physical = ParseMeminfoTotalByteCount(ReadSmallFile("/proc/meminfo"));
  std::int64_t limit = ParseCgroupMemoryLimitByteCount(ReadSmallFile("/sys/fs/cgroup/memory.max"));
  if (limit <= 0) {
    limit = ParseCgroupMemoryLimitByteCount(
        ReadSmallFile("/sys/fs/cgroup/memory/memory.limit_in_bytes"));
  }
  return EffectiveHostMemoryByteCount(physical, limit);
}

}  // namespace

std::int64_t HostMemoryByteCountCached() {
  static const std::int64_t byteCount = MeasureHostMemoryByteCount();
  return byteCount;
}

}  // namespace urnw
