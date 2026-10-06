// The session bus side of the handover (InstanceHandover.hpp): a launch's
// claim on the bus name, its handover call and its waits, and the instance's
// handover object and exiting name. GUI only: it needs gio, and the
// GApplication whose startup notification ids it carries.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <gio/gio.h>

#include "InstanceHandover.hpp"

namespace urnw::instance {

// This process's launch: the paths as uris, as GApplication makes them, and
// the startup notification id GtkApplication would send a running instance.
Launch LaunchFor(GApplication* application, const Arguments& arguments);

// Runs the launch on the session bus (RunLaunch) before GApplication
// registers. Main thread, before the main loop: the handover object it
// registers answers on the default main context, which nothing iterates until
// this instance has started, so a launch that reaches it early waits there.
Outcome LaunchOnSessionBus(const Launch& launch);

// The window and the tray exist: held and later launches are served, each
// between GApplication's before_emit and after_emit with the launch's startup
// notification id, as GApplication serves an activation.
void OpenLaunches(GApplication* application, Gate::Serve serve);

// This instance begins to exit (Quit, a failed start, the end of the main
// loop): it owns kExitingName until its process ends, then refuses every
// launch. Only the first call acts.
void BeginExiting();

}  // namespace urnw::instance
