// A reset of the extenders (connect EXTENDER.md E7) that urnetworkd refused
// because a tunnel bring-up owned its session (ctl::kCodeStartInProgress). That
// bring-up imported the network space as it was before the reset, so the
// session it brings up would dial from the old directory until the space is
// imported again. The GUI sends the same request again, once, when a status it
// reads shows the bring-up settled. An id the space applied already changes
// nothing, so the second delivery is harmless. A GUI that quits first drops it,
// and the space's next import applies the id it carries.
//
// Header-only and free of GTK and the SDK so the unit tests need no vendored
// headers (tests/ExtenderResetTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "ControlProtocol.hpp"

namespace urnw {

// The reset owed to urnetworkd: noted from the daemon's answer to a press, and
// taken once the bring-up that refused it settled. Main loop only.
class OwedExtenderReset {
 public:
  // The daemon's answer to the reset_extenders a press sent. A start_in_progress
  // refusal leaves the request owed. Any other answer leaves nothing owed: one
  // the daemon took, refused for another reason or never received is not sent
  // again, and a press supersedes an older owed reset.
  void NoteAnswer(const ctl::ResetExtendersRequest& request, bool taken,
                  const std::string& code) {
    if (!taken && code == ctl::kCodeStartInProgress) {
      owed_ = request;
    } else {
      owed_.reset();
    }
  }

  // The owed request to send again: taken once `status` shows the daemon's
  // session no longer starting, after which nothing is owed.
  std::optional<ctl::ResetExtendersRequest> TakeIfSettled(const ctl::StatusReply& status) {
    if (!owed_ || status.tunnel_state == ctl::TunnelState::Starting) return std::nullopt;
    std::optional<ctl::ResetExtendersRequest> request = std::move(owed_);
    owed_.reset();
    return request;
  }

  bool Owed() const { return owed_.has_value(); }

 private:
  std::optional<ctl::ResetExtendersRequest> owed_;
};

}  // namespace urnw
