// The daemon's flushes of the SDK's glog buffers (urnet::flushGlog), all on one
// thread of its own.
//
// glog writes its files from a buffer and flushes it every 30 s, so a death lost
// up to 30 s of the SDK's log, the part most likely to name the cause, and the
// log ring's tail of the files lagged as far. The flusher closes that gap where
// the SDK writes: once a second while a device of this daemon runs, and soon
// after the end of every bring-up and every stop. With no device the SDK writes
// next to nothing and glog's own flush is enough; flushing every second anyway
// would fsync every log file every second for the life of the service.
//
// A thread, never the caller's: glog's flush fsyncs, a loaded disk can hold
// that for seconds, and the main loop answers `status`. It is detached and its
// state is never freed, so the process can end around it.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urnw::glogflush {

// Starts the thread. Later calls do nothing.
void Start();

// Whether to flush once a second. TunnelHost's reaper says so while a device of
// its runs, and a bring-up from its start.
void SetActive(bool active);

// One flush soon, whether or not active. Never waits.
void Request();

}  // namespace urnw::glogflush
