// How an SdkHost read answers on the GTK main loop (SdkHost::ReadOnMain).
//
// The connect commands, the device work behind them and the user's edits run
// on SdkHost's command queue, and each takes mutex_ across its rpc to the
// daemon, for up to the 60 s rpc timeout. A main-loop read that took mutex_
// then waited for the command, with the window stopped. So on the main loop
// a read never waits on mutex_: while commands are outstanding (one may hold
// mutex_, and a queued edit may not have applied) or while mutex_ is taken,
// it answers the last value it gave, which an edit queued from the main loop
// updates at once to what it writes (SdkHost::KeepOnMain). Otherwise it reads
// under mutex_ and keeps the value. Off the main loop (a command, a listener
// thread, a worker) it waits for mutex_ as it always did.
//
// Pure (tests/MainReadTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace urnw::main_read {

enum class Source {
  Locked,    // read under mutex_, waiting for it, and keep the value
  Snapshot,  // answer the last value read (or kept), and wait on nothing
};

// `lockFree`: a try_lock of mutex_ succeeded (asked only on the main loop
// with no command outstanding).
constexpr Source SourceFor(bool onMainLoop, bool commandsBusy, bool lockFree) {
  if (!onMainLoop) return Source::Locked;
  if (commandsBusy || !lockFree) return Source::Snapshot;
  return Source::Locked;
}


// Main-loop-only mirrors belong to one sign-in/network-space generation. A
// busy worker must never make a new account read the previous account's data.
class Snapshots {
 public:
  template <typename T>
  T Get(const char* key, std::uint64_t epoch) {
    if (!Select(epoch)) return T{};
    const auto found = values_.find(key);
    if (found != values_.end()) {
      if (const T* value = std::any_cast<T>(&found->second)) return *value;
    }
    return T{};
  }

  template <typename T>
  void Keep(const char* key, std::uint64_t epoch, T value) {
    if (!Select(epoch)) return;
    values_[key] = std::move(value);
  }

 private:
  bool Select(std::uint64_t epoch) {
    if (epoch < epoch_) return false;
    if (epoch_ != epoch) {
      values_.clear();
      epoch_ = epoch;
    }
    return true;
  }
  std::uint64_t epoch_ = 0;
  std::unordered_map<std::string, std::any> values_;
};

}  // namespace urnw::main_read
