// The owed Reset extenders (ExtenderReset.hpp): urnetworkd refuses a
// reset_extenders while a tunnel bring-up owns its session, and that bring-up
// imported the space as it was before the reset, so the GUI sends the same
// request again, once, when a status shows the bring-up settled. Every other
// answer owes nothing, and a newer press supersedes an older owed reset.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <optional>
#include <string>

#include "ExtenderReset.hpp"

namespace ctl = urnw::ctl;

namespace {

ctl::ResetExtendersRequest SampleOwedReset(const std::string& resetId) {
  ctl::ResetExtendersRequest req;
  req.host_name = "network.example";
  req.env_name = "main";
  req.extender_reset_id = resetId;
  return req;
}

ctl::StatusReply StatusIn(ctl::TunnelState state) {
  ctl::StatusReply status;
  status.tunnel_state = state;
  return status;
}

}  // namespace

// A start_in_progress refusal owes the request; nothing is sent while the
// bring-up runs, and the same request goes once it settled, exactly once.
UR_TEST(ExtenderReset_ABusyRefusalIsSentAgainOnceTheBringUpSettled) {
  urnw::OwedExtenderReset owed;
  const ctl::ResetExtendersRequest req = SampleOwedReset("f00dfeed-0000-4000-8000-000000000001");
  owed.NoteAnswer(req, /*taken=*/false, ctl::kCodeStartInProgress);
  UR_EXPECT_TRUE(owed.Owed());

  UR_EXPECT_FALSE(owed.TakeIfSettled(StatusIn(ctl::TunnelState::Starting)).has_value());
  UR_EXPECT_TRUE(owed.Owed());

  const std::optional<ctl::ResetExtendersRequest> again =
      owed.TakeIfSettled(StatusIn(ctl::TunnelState::Up));
  UR_EXPECT_TRUE(again.has_value());
  UR_EXPECT_TRUE(again && again->host_name == req.host_name);
  UR_EXPECT_TRUE(again && again->env_name == req.env_name);
  UR_EXPECT_TRUE(again && again->extender_reset_id == req.extender_reset_id);

  UR_EXPECT_FALSE(owed.Owed());
  UR_EXPECT_FALSE(owed.TakeIfSettled(StatusIn(ctl::TunnelState::Up)).has_value());
}

// Nobody pressed anything for the request sent again, so it asks for no dialog,
// while the press's own request did.
UR_TEST(ExtenderReset_TheRequestSentAgainAsksForNoDialog) {
  urnw::OwedExtenderReset owed;
  const ctl::ResetExtendersRequest press = SampleOwedReset("f00dfeed-0000-4000-8000-000000000008");
  UR_EXPECT_TRUE(press.interactive);
  owed.NoteAnswer(press, /*taken=*/false, ctl::kCodeStartInProgress);
  const std::optional<ctl::ResetExtendersRequest> again =
      owed.TakeIfSettled(StatusIn(ctl::TunnelState::Up));
  UR_EXPECT_TRUE(again.has_value());
  UR_EXPECT_TRUE(again && !again->interactive);
  UR_EXPECT_TRUE(again && !ctl::ResetExtendersAllowsInteraction(nlohmann::json(*again)));
}

// Any state but starting is a settled bring-up, another user's redacted status
// included (it keeps tunnel_state).
UR_TEST(ExtenderReset_EveryStateButStartingIsSettled) {
  const ctl::TunnelState settled[] = {ctl::TunnelState::Stopped, ctl::TunnelState::Up,
                                      ctl::TunnelState::Stopping, ctl::TunnelState::Error};
  for (const ctl::TunnelState state : settled) {
    urnw::OwedExtenderReset owed;
    owed.NoteAnswer(SampleOwedReset("f00dfeed-0000-4000-8000-000000000002"), false,
                    ctl::kCodeStartInProgress);
    UR_EXPECT_TRUE_MSG(ctl::ToString(state), owed.TakeIfSettled(StatusIn(state)).has_value());
  }
  urnw::OwedExtenderReset owed;
  owed.NoteAnswer(SampleOwedReset("f00dfeed-0000-4000-8000-000000000003"), false,
                  ctl::kCodeStartInProgress);
  ctl::StatusReply full = StatusIn(ctl::TunnelState::Up);
  full.provider_running = true;
  UR_EXPECT_TRUE(owed.TakeIfSettled(ctl::RedactStatusForForeignUid(full)).has_value());
}

// Taken, refused for another reason, or never delivered: nothing is owed, and
// such an answer to a newer press also clears an older owed reset, whose id the
// newer one supersedes.
UR_TEST(ExtenderReset_OtherAnswersOweNothing) {
  struct Answer {
    bool taken;
    const char* code;
  };
  const Answer answers[] = {
      {true, ""},                         // the daemon applied it, or held no space
      {false, ""},                        // unreachable, or a daemon without the verb
      {false, ctl::kCodeAuthDenied},      // polkit refused it
      {false, ctl::kCodeAuthDismissed},   // the user closed the dialog
      {false, ctl::kCodeTunnelOwnedByOtherClient},
      {true, ctl::kCodeStartInProgress},  // a code on a taken answer is no refusal
  };
  for (const Answer& answer : answers) {
    urnw::OwedExtenderReset owed;
    owed.NoteAnswer(SampleOwedReset("f00dfeed-0000-4000-8000-000000000004"), false,
                    ctl::kCodeStartInProgress);
    owed.NoteAnswer(SampleOwedReset("f00dfeed-0000-4000-8000-000000000005"), answer.taken,
                    answer.code);
    UR_EXPECT_TRUE_MSG(answer.code, !owed.Owed());
    UR_EXPECT_TRUE_MSG(answer.code,
                       !owed.TakeIfSettled(StatusIn(ctl::TunnelState::Stopped)).has_value());
  }
}

// A newer press refused the same way replaces the owed one: the newer id is the
// one sent again.
UR_TEST(ExtenderReset_ANewerBusyRefusalReplacesTheOwedOne) {
  urnw::OwedExtenderReset owed;
  owed.NoteAnswer(SampleOwedReset("f00dfeed-0000-4000-8000-000000000006"), false,
                  ctl::kCodeStartInProgress);
  owed.NoteAnswer(SampleOwedReset("f00dfeed-0000-4000-8000-000000000007"), false,
                  ctl::kCodeStartInProgress);
  const auto again = owed.TakeIfSettled(StatusIn(ctl::TunnelState::Up));
  UR_EXPECT_TRUE(again && again->extender_reset_id == "f00dfeed-0000-4000-8000-000000000007");
  UR_EXPECT_FALSE(urnw::OwedExtenderReset().Owed());
  UR_EXPECT_FALSE(
      urnw::OwedExtenderReset().TakeIfSettled(StatusIn(ctl::TunnelState::Up)).has_value());
}
