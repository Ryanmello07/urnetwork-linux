// The Sessions page's controller lifecycle (SessionsBinding.hpp; REVOKE-UI-FINAL.md
// §2, §6, §10 "Refresh and lifecycle" and "Races"), with a fake controller
// that records every call: start and visibility on attach, visibility and
// foreground forwarded as they change, Refresh and the revokes to the
// attached controller only, unsubscribe before close, and no post accepted
// from a released controller or from the previous account's.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "SessionsBinding.hpp"

namespace s = urnw::sessions;

namespace {

// Records each call as a word in a log the test keeps after the binding has
// dropped the controller.
class FakeController final : public s::Controller {
 public:
  explicit FakeController(std::shared_ptr<std::vector<std::string>> log)
      : log_(std::move(log)) {}
  void Start() override { log_->push_back("start"); }
  void SetVisible(bool visible) override { log_->push_back(visible ? "visible:1" : "visible:0"); }
  void SetForeground(bool foreground) override {
    log_->push_back(foreground ? "foreground:1" : "foreground:0");
  }
  void Refresh() override { log_->push_back("refresh"); }
  void RevokeSession(const std::string& sessionId) override {
    log_->push_back("revoke:" + sessionId);
  }
  void RevokeOtherSessions() override { log_->push_back("revoke-others"); }
  s::Snapshot Read() override {
    log_->push_back("read");
    return snapshot;
  }
  void Unsubscribe() override { log_->push_back("unsubscribe"); }
  void Close() override { log_->push_back("close"); }

  s::Snapshot snapshot;

 private:
  std::shared_ptr<std::vector<std::string>> log_;
};

using Log = std::shared_ptr<std::vector<std::string>>;

Log NewLog() { return std::make_shared<std::vector<std::string>>(); }

bool LogIs(const Log& log, const std::vector<std::string>& expected) {
  if (*log == expected) return true;
  std::string got;
  for (const std::string& entry : *log) got += entry + " ";
  UR_FAIL("log was: " + got);
  return false;
}

}  // namespace

UR_TEST(SessionsBinding_AttachStartsAndTellsWhetherTheScreenShows) {
  s::Binding binding;
  const Log log = NewLog();
  binding.SetVisible(true);  // the page mapped before the controller opened
  binding.Attach(std::make_unique<FakeController>(log), /*source=*/7);
  UR_EXPECT_TRUE(LogIs(log, {"start", "visible:1"}));
  UR_EXPECT_TRUE(binding.Attached());
  UR_EXPECT_EQ(7u, binding.Source());

  // attached off screen: started, told it is hidden (no polling)
  s::Binding hidden;
  const Log second = NewLog();
  hidden.Attach(std::make_unique<FakeController>(second), 7);
  UR_EXPECT_TRUE(LogIs(second, {"start", "visible:0"}));
}

UR_TEST(SessionsBinding_VisibilityAndForegroundAreForwardedAsTheyChange) {
  s::Binding binding;
  const Log log = NewLog();
  binding.Attach(std::make_unique<FakeController>(log), 1);
  log->clear();
  binding.SetVisible(true);   // mapped
  binding.SetVisible(true);   // no change: nothing
  binding.SetVisible(false);  // unmapped
  binding.SetForeground(true);
  binding.SetForeground(true);
  binding.SetForeground(false);  // hidden to the tray
  UR_EXPECT_TRUE(LogIs(log, {"visible:1", "visible:0", "foreground:1", "foreground:0"}));
  // without a controller they are remembered, not sent
  s::Binding detached;
  detached.SetVisible(true);
  detached.SetForeground(true);
  const Log late = NewLog();
  detached.Attach(std::make_unique<FakeController>(late), 1);
  UR_EXPECT_TRUE(LogIs(late, {"start", "visible:1"}));
}

UR_TEST(SessionsBinding_RefreshAndRevokesGoToTheAttachedController) {
  s::Binding binding;
  binding.Refresh();  // nothing attached: nothing to call
  binding.Revoke(s::Target{false, "a"});
  const Log log = NewLog();
  binding.Attach(std::make_unique<FakeController>(log), 1);
  log->clear();
  binding.Refresh();  // the header's refresh button and Try again
  binding.Revoke(s::Target{false, "01a1f3c2"});
  binding.Revoke(s::Target{true, ""});
  binding.Revoke(s::Target{false, ""});  // no session named: nothing
  UR_EXPECT_TRUE(LogIs(log, {"refresh", "revoke:01a1f3c2", "revoke-others"}));
}

// §6: unsubscribe, then Close(); and nothing lands after.
UR_TEST(SessionsBinding_ReleaseUnsubscribesThenClosesAndOrphansThePosts) {
  s::Binding binding;
  const Log log = NewLog();
  const uint64_t generation = binding.NextGeneration();
  binding.Attach(std::make_unique<FakeController>(log), 1);
  UR_EXPECT_TRUE(binding.Accepts(generation));
  log->clear();
  binding.Release();
  UR_EXPECT_TRUE(LogIs(log, {"unsubscribe", "close"}));
  UR_EXPECT_FALSE(binding.Attached());
  UR_EXPECT_EQ(0u, binding.Source());
  // a post that was already on its way is dropped, and there is nothing to read
  UR_EXPECT_FALSE(binding.Accepts(generation));
  UR_EXPECT_FALSE(binding.Read().has_value());
  // calls after the release reach nothing; a second release does nothing
  binding.Refresh();
  binding.SetVisible(true);
  binding.Release();
  UR_EXPECT_TRUE(LogIs(log, {"unsubscribe", "close"}));
}

UR_TEST(SessionsBinding_DestructionReleases) {
  const Log log = NewLog();
  {
    s::Binding binding;
    binding.Attach(std::make_unique<FakeController>(log), 1);
    log->clear();
  }
  UR_EXPECT_TRUE(LogIs(log, {"unsubscribe", "close"}));
}

// Switching accounts (or Apis) while a request is pending: the new
// controller's posts are accepted, the old one's never again, so the newer
// account's page survives an older answer.
UR_TEST(SessionsBinding_TheNewControllerRefusesTheOldOnesPosts) {
  s::Binding binding;
  const Log first = NewLog();
  const uint64_t oldGeneration = binding.NextGeneration();
  binding.Attach(std::make_unique<FakeController>(first), /*source=*/1);
  // a second Api: the generation is taken before the attach, which releases
  // the first controller itself
  const Log second = NewLog();
  const uint64_t newGeneration = binding.NextGeneration();
  binding.Attach(std::make_unique<FakeController>(second), /*source=*/2);
  UR_EXPECT_TRUE(LogIs(first, {"start", "visible:0", "unsubscribe", "close"}));
  UR_EXPECT_TRUE(LogIs(second, {"start", "visible:0"}));
  UR_EXPECT_FALSE(binding.Accepts(oldGeneration));
  UR_EXPECT_TRUE(binding.Accepts(newGeneration));
  UR_EXPECT_EQ(2u, binding.Source());
  // after a sign-out (release) and a sign-in on the same Api, still a new one
  binding.Release();
  const uint64_t afterSignIn = binding.NextGeneration();
  binding.Attach(std::make_unique<FakeController>(NewLog()), 2);
  UR_EXPECT_FALSE(binding.Accepts(newGeneration));
  UR_EXPECT_TRUE(binding.Accepts(afterSignIn));
}

// Posts can land out of order (a refresh and an action completing on two sdk
// threads): each one reads the controller's latest snapshot, so the last
// render is the newest state whichever post came last.
UR_TEST(SessionsBinding_EveryPostReadsTheLatestSnapshot) {
  s::Binding binding;
  const Log log = NewLog();
  auto controller = std::make_unique<FakeController>(log);
  FakeController* fake = controller.get();
  const uint64_t generation = binding.NextGeneration();
  binding.Attach(std::move(controller), 1);
  // the controller has moved on to "refreshed, two rows" by the time the
  // older post (the action's) is handled
  s::Session one;
  one.sessionId = "a";
  fake->snapshot.loaded = true;
  fake->snapshot.sessions = {one, one};
  UR_EXPECT_TRUE(binding.Accepts(generation));
  const std::optional<s::Snapshot> read = binding.Read();
  UR_EXPECT_TRUE(read.has_value() && read->loaded && read->sessions.size() == 2);
}
