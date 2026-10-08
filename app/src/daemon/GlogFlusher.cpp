// SPDX-License-Identifier: MPL-2.0
#include "daemon/GlogFlusher.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include <urnetwork_sdk.hpp>

#include "daemon/DaemonLog.hpp"

namespace urnw::glogflush {
namespace {

// How often the thread flushes while active.
constexpr std::chrono::seconds kActiveInterval{1};

struct State {
  std::mutex mutex;
  std::condition_variable wake;
  // Guarded by mutex.
  bool requested = false;
  std::atomic<bool> active{false};
  std::atomic<bool> started{false};
};

// Never destroyed: the detached thread may still read it while static
// destructors run at exit.
State& Shared() {
  static State* state = new State();
  return *state;
}

void Run() {
  State& state = Shared();
  for (;;) {
    bool requested = false;
    {
      std::unique_lock<std::mutex> lock(state.mutex);
      state.wake.wait_for(lock, kActiveInterval, [&] { return state.requested; });
      requested = state.requested;
      state.requested = false;
    }
    if (requested || state.active.load()) urnet::flushGlog();
  }
}

}  // namespace

void Start() {
  State& state = Shared();
  if (state.started.exchange(true)) return;
  try {
    std::thread(&Run).detach();
  } catch (const std::exception& e) {
    // Without it glog's own 30 s flush is all there is, as before this existed.
    DaemonLogf("[daemon] the glog flusher could not start: %s\n", e.what());
  }
}

void SetActive(bool active) { Shared().active.store(active); }

void Request() {
  State& state = Shared();
  {
    std::scoped_lock lock(state.mutex);
    state.requested = true;
  }
  state.wake.notify_all();
}

}  // namespace urnw::glogflush
