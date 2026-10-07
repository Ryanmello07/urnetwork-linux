// The app_prefs.json file itself, away from where it lives (AppPrefs.hpp finds
// it), so tests/AppPrefsTest.cpp runs it on a temporary directory without glib.
//
// A set is a read-modify-write of the whole object, so keys never clobber each
// other. Sets come from more than one thread (the update checker records its
// checks from its worker, everything else from the main loop), so one
// process-wide lock holds each read-modify-write: two at once could otherwise
// both read the old object and the second write would drop the first's key.
// The object goes to a temporary file beside the target, is synced, and is
// renamed over it, so a reader, or a crash, sees the old object or the new one,
// never a truncated file that reads as {} and resets every preference.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace urnw::prefs {

// Far beyond any real preferences file: a bigger one is not ours to parse.
inline constexpr std::uintmax_t kMaxFileBytes = 1u << 20;

// The object at `path`: {} for a missing, unreadable, oversized or malformed
// file, or one that holds anything but an object.
inline nlohmann::json ReadAllAt(const std::string& path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > kMaxFileBytes) return nlohmann::json::object();
  try {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return nlohmann::json::object();
    nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
    if (parsed.is_object()) return parsed;
  } catch (...) {
  }
  return nlohmann::json::object();
}

// `key`'s value as a T, or `fallback` when it is absent or of another type.
template <typename T>
inline T ValueOr(const nlohmann::json& all, const char* key, T fallback) {
  if (auto it = all.find(key); it != all.end()) {
    try {
      return it->get<T>();
    } catch (...) {
    }
  }
  return fallback;
}

inline std::mutex& WriteLock() {
  static std::mutex lock;
  return lock;
}

// Replaces `path` with `text` through a synced temporary file renamed over it.
// 0, or the errno of the step that failed, with `path` untouched.
inline int ReplaceFile(const std::string& path, const std::string& text) {
  std::string temporary = path + ".XXXXXX";
  const int fd = ::mkstemp(temporary.data());  // mode 0600, as the 0700 directory
  if (fd < 0) return errno;
  int failure = 0;
  for (size_t written = 0; failure == 0 && written < text.size();) {
    const ssize_t n = ::write(fd, text.data() + written, text.size() - written);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      failure = n < 0 ? errno : EIO;
    } else {
      written += static_cast<size_t>(n);
    }
  }
  if (failure == 0 && ::fsync(fd) != 0) failure = errno;
  if (::close(fd) != 0 && failure == 0) failure = errno;
  if (failure == 0 && ::rename(temporary.c_str(), path.c_str()) != 0) failure = errno;
  if (failure != 0) ::unlink(temporary.c_str());
  return failure;
}

// Sets `key` in the object at `path`. 0, or the errno of the write that failed.
template <typename T>
inline int SetAt(const std::string& path, const char* key, const T& value) {
  std::scoped_lock lock(WriteLock());
  nlohmann::json all = ReadAllAt(path);
  all[key] = value;
  return ReplaceFile(path, all.dump(2) + "\n");
}

}  // namespace urnw::prefs
