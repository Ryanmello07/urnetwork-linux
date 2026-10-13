// The checks that stand between a click and the browser approval: whether the
// approval page is live in this run and lists the contract version the network
// needs, and whether this process may use it at all. The rest of the conditions
// (the build flag, a network whose API a request can name, a signed-out app, the
// urnetwork:// handler) are MainWindow::ApprovalOfferedIn's, which asks these.
//
// GLib and libsoup, no GTK: the request runs on a worker thread that touches no
// widget, and the window asks for the answer when the person clicks. The
// judgment of the page's answer is ApprovalDescriptor.hpp.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

namespace urnw {

// This process runs as root, or set-uid. A GUI started with sudo or pkexec, or
// a set-uid one, opens the browser as root and is not the holder of
// com.bringyour.network on the person's session bus, so the browser's handover
// of the answer can never reach it: the approval is not offered there, and the
// paste flow stays.
bool ProcessIsElevated();

enum class ApprovalPage {
  NotChecked,  // nobody has asked yet
  Checking,    // the one request of this run is on its way
  Live,        // the page answered its descriptor in time, and lists this platform and a contract
               // version this app speaks
  Down,        // it did not, for any reason: the approval stays hidden for the rest of the run
};

// Starts the one check of this run, on a worker thread, if it has not started: a
// single GET of the descriptor (config::kApprovalDescriptorUrl) with a
// three-second budget and no redirect, whose body and content type
// ApprovalDescriptor.hpp judges. The answer, success and failure alike, is kept
// for the run; nothing asks again. Safe to call from any thread and as often as
// wanted. Never blocks. Logs closed words only.
void StartApprovalPageCheck();

// Where the check stands. NotChecked until StartApprovalPageCheck.
ApprovalPage ApprovalPageState();

// Whether the descriptor the page answered in this run lists `contract`
// (ApprovalNetwork.hpp: 1 for the official network, 2 for another one). False
// until the page has answered Live, and false for a page that lists other
// versions only, so a page that serves the official network alone is never sent
// a request that names another.
bool ApprovalPageLists(std::int64_t contract);

}  // namespace urnw
