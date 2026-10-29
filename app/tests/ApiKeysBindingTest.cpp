// The API keys page's requests (ApiKeysBinding.hpp), with a fake service that
// keeps every request's answer callback until the test answers it, and a
// manual clock for the give-ups: one list at a time, the new key handed over
// once and kept nowhere in the binding, the list asked again after every
// create, deletes one per key, a deleted key a late list cannot bring back,
// every request given up on after its timeout with the late answer dropped,
// and no answer landing after a release or from a previous service.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ApiKeysBinding.hpp"

namespace k = urnw::api_keys;

namespace {

// Every request the binding made, with the callback that answers it.
struct Calls {
  std::vector<std::function<void(k::ListAnswer)>> lists;
  std::vector<std::pair<std::string, std::function<void(k::CreateAnswer)>>> creates;
  std::vector<std::pair<std::string, std::function<void(k::DeleteAnswer)>>> deletes;
  bool destroyed = false;
};

class FakeService final : public k::Service {
 public:
  explicit FakeService(std::shared_ptr<Calls> calls) : calls_(std::move(calls)) {}
  ~FakeService() override { calls_->destroyed = true; }
  void List(std::function<void(k::ListAnswer)> done) override {
    calls_->lists.push_back(std::move(done));
  }
  void Create(const std::string& name, std::function<void(k::CreateAnswer)> done) override {
    calls_->creates.emplace_back(name, std::move(done));
  }
  void Delete(const std::string& keyId, std::function<void(k::DeleteAnswer)> done) override {
    calls_->deletes.emplace_back(keyId, std::move(done));
  }

 private:
  std::shared_ptr<Calls> calls_;
};

// The give-ups the binding armed, in order; the test fires them.
struct Clock {
  std::vector<std::pair<int, std::function<void()>>> armed;
  k::Schedule Schedule() {
    return [this](int delayMs, std::function<void()> fire) {
      armed.emplace_back(delayMs, std::move(fire));
    };
  }
};

// A binding attached to a fresh fake service, counting its renders.
struct Fixture {
  Clock clock;
  k::Binding binding{k::DefaultSettings(), clock.Schedule()};
  std::shared_ptr<Calls> calls = std::make_shared<Calls>();
  int renders = 0;

  Fixture() {
    binding.on_changed = [this] { ++renders; };
    binding.Attach(std::make_unique<FakeService>(calls), /*source=*/7);
  }
};

const std::string kIdA = "019a2b3c-0000-7000-8000-00000000000a";
const std::string kIdB = "019a2b3c-0000-7000-8000-00000000000b";
const std::string kIdC = "019a2b3c-0000-7000-8000-00000000000c";

k::Key KeyOf(const std::string& id, const std::string& name, int64_t createUnixSeconds) {
  k::Key key;
  key.id = id;
  key.name = name;
  key.createUnixSeconds = createUnixSeconds;
  return key;
}

k::ListAnswer Listed(std::vector<k::Key> keys) {
  k::ListAnswer answer;
  answer.ok = true;
  answer.keys = std::move(keys);
  return answer;
}

std::vector<std::string> IdsOf(const k::Snapshot& snapshot) {
  std::vector<std::string> ids;
  for (const k::Key& key : snapshot.keys) ids.push_back(key.id);
  return ids;
}

// The fixture with a loaded list of A (older) and B (newer).
void LoadTwo(Fixture& f) {
  f.binding.Refresh();
  f.calls->lists.back()(Listed({KeyOf(kIdA, "laptop", 100), KeyOf(kIdB, "home server", 200)}));
}

}  // namespace

UR_TEST(ApiKeysBinding_TheDefaultsAreTheAppsGiveUp) {
  const k::Settings settings = k::DefaultSettings();
  UR_EXPECT_EQ(20000, settings.requestTimeoutMs);
  UR_EXPECT_EQ(2000, settings.copiedMs);
  // non-positive never gives up: nothing is armed
  Clock clock;
  k::Binding binding{k::Settings{}, clock.Schedule()};
  auto calls = std::make_shared<Calls>();
  binding.Attach(std::make_unique<FakeService>(calls), 1);
  UR_EXPECT_TRUE(binding.Refresh());
  UR_EXPECT_EQ(0u, clock.armed.size());
}

UR_TEST(ApiKeysBinding_NothingIsSentWithoutAService) {
  Clock clock;
  k::Binding binding{k::DefaultSettings(), clock.Schedule()};
  int done = 0;
  UR_EXPECT_FALSE(binding.Attached());
  UR_EXPECT_FALSE(binding.Refresh());
  UR_EXPECT_FALSE(binding.Create("home server", [&](bool, std::string) { ++done; }));
  UR_EXPECT_FALSE(binding.Delete(kIdA));
  UR_EXPECT_EQ(0u, clock.armed.size());
  UR_EXPECT_EQ(0, done);
  UR_EXPECT_FALSE(binding.Read().loading || binding.Read().creating);
}

UR_TEST(ApiKeysBinding_TheListIsAskedOnceAtATimeAndSorted) {
  Fixture f;
  UR_EXPECT_TRUE(f.binding.Refresh());
  UR_EXPECT_TRUE(f.binding.Read().loading);
  UR_EXPECT_FALSE(f.binding.Refresh());  // one is out
  UR_EXPECT_EQ(1u, f.calls->lists.size());
  // the server lists in no order, and a key without an id cannot be shown
  f.calls->lists[0](Listed({KeyOf(kIdA, "laptop", 100), KeyOf("", "no id", 300),
                            KeyOf(kIdB, "home server", 200)}));
  const k::Snapshot& snapshot = f.binding.Read();
  UR_EXPECT_TRUE(snapshot.loaded && !snapshot.loading && !snapshot.loadFailed);
  UR_EXPECT_TRUE((IdsOf(snapshot) == std::vector<std::string>{kIdB, kIdA}));
  // the next refresh is sent
  UR_EXPECT_TRUE(f.binding.Refresh());
  UR_EXPECT_EQ(2u, f.calls->lists.size());
  UR_EXPECT_TRUE(f.renders >= 3);
}

UR_TEST(ApiKeysBinding_AFailedListKeepsTheLastOne) {
  Fixture f;
  // a first load that fails: nothing loaded
  f.binding.Refresh();
  f.calls->lists[0](k::ListAnswer{});
  UR_EXPECT_TRUE(!f.binding.Read().loaded && f.binding.Read().loadFailed);
  // loaded, then a refresh fails: the last list stays
  f.binding.Refresh();
  f.calls->lists[1](Listed({KeyOf(kIdA, "laptop", 100)}));
  UR_EXPECT_TRUE(f.binding.Read().loaded && !f.binding.Read().loadFailed);
  f.binding.Refresh();
  f.calls->lists[2](k::ListAnswer{});
  UR_EXPECT_TRUE(f.binding.Read().loadFailed);
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdA}));
}

// The sdk is known to drop callbacks: every request is given up on, finally.
UR_TEST(ApiKeysBinding_AListThatNeverAnswersIsGivenUpOn) {
  Fixture f;
  f.binding.Refresh();
  UR_EXPECT_EQ(1u, f.clock.armed.size());
  UR_EXPECT_EQ(20000, f.clock.armed[0].first);
  f.clock.armed[0].second();
  UR_EXPECT_TRUE(!f.binding.Read().loading && f.binding.Read().loadFailed);
  // the late answer changes nothing: the give-up is final
  f.calls->lists[0](Listed({KeyOf(kIdA, "laptop", 100)}));
  UR_EXPECT_TRUE(f.binding.Read().keys.empty() && f.binding.Read().loadFailed);
  // and a give-up for a request that did answer does nothing
  f.binding.Refresh();
  f.calls->lists[1](Listed({KeyOf(kIdA, "laptop", 100)}));
  f.clock.armed[1].second();
  UR_EXPECT_TRUE(!f.binding.Read().loadFailed && f.binding.Read().keys.size() == 1);
}

// The key the server returns goes to the create's caller once, and the
// binding keeps no copy of it; the list is asked again for the new key.
UR_TEST(ApiKeysBinding_TheNewKeyIsHandedOverOnceAndKeptNowhere) {
  Fixture f;
  LoadTwo(f);
  std::vector<std::pair<bool, std::string>> outcomes;
  UR_EXPECT_TRUE(f.binding.Create("  home server  ", [&](bool ok, std::string apiKey) {
    outcomes.emplace_back(ok, std::move(apiKey));
  }));
  UR_EXPECT_EQ(1u, f.calls->creates.size());
  UR_EXPECT_TRUE(f.calls->creates[0].first == "home server");  // the name NameToCreate sends
  UR_EXPECT_TRUE(f.binding.Read().creating);
  const size_t listsBefore = f.calls->lists.size();
  k::CreateAnswer answer;
  answer.ok = true;
  answer.apiKey = "urn_k3y-secret-0001";
  f.calls->creates[0].second(answer);
  UR_EXPECT_EQ(1u, outcomes.size());
  UR_EXPECT_TRUE(outcomes[0].first && outcomes[0].second == "urn_k3y-secret-0001");
  UR_EXPECT_FALSE(f.binding.Read().creating);
  // nothing in the snapshot carries the key
  for (const k::Key& key : f.binding.Read().keys) {
    UR_EXPECT_TRUE(key.id.find("urn_") == std::string::npos);
    UR_EXPECT_TRUE(key.name.find("urn_") == std::string::npos);
  }
  // the list again, which shows the new key
  UR_EXPECT_EQ(listsBefore + 1, f.calls->lists.size());
  // a second answer to the same request is dropped
  f.calls->creates[0].second(answer);
  UR_EXPECT_EQ(1u, outcomes.size());
}

UR_TEST(ApiKeysBinding_ARefusedCreateFailsAndTheListIsAskedAgain) {
  Fixture f;
  LoadTwo(f);
  std::vector<std::pair<bool, std::string>> outcomes;
  f.binding.Create("home server", [&](bool ok, std::string apiKey) {
    outcomes.emplace_back(ok, std::move(apiKey));
  });
  const size_t listsBefore = f.calls->lists.size();
  f.calls->creates[0].second(k::CreateAnswer{});  // the server's error, or a transport error
  UR_EXPECT_EQ(1u, outcomes.size());
  UR_EXPECT_TRUE(!outcomes[0].first && outcomes[0].second.empty());
  UR_EXPECT_EQ(listsBefore + 1, f.calls->lists.size());
  // an ok with no key is a failure, and hands nothing over
  f.calls->lists.back()(Listed({}));
  f.binding.Create("home server", [&](bool ok, std::string apiKey) {
    outcomes.emplace_back(ok, std::move(apiKey));
  });
  k::CreateAnswer empty;
  empty.ok = true;
  f.calls->creates[1].second(empty);
  UR_EXPECT_TRUE(outcomes.size() == 2 && !outcomes[1].first && outcomes[1].second.empty());
}

// A create given up on fails for good: the key a late answer brings is never
// handed over, and the list is asked again, which shows a key made anyway.
UR_TEST(ApiKeysBinding_ACreateThatNeverAnswersIsGivenUpOn) {
  Fixture f;
  LoadTwo(f);
  std::vector<std::pair<bool, std::string>> outcomes;
  f.binding.Create("home server", [&](bool ok, std::string apiKey) {
    outcomes.emplace_back(ok, std::move(apiKey));
  });
  const size_t listsBefore = f.calls->lists.size();
  f.clock.armed.back().second();
  UR_EXPECT_EQ(1u, outcomes.size());
  UR_EXPECT_FALSE(outcomes[0].first);
  UR_EXPECT_FALSE(f.binding.Read().creating);
  UR_EXPECT_EQ(listsBefore + 1, f.calls->lists.size());
  k::CreateAnswer late;
  late.ok = true;
  late.apiKey = "urn_k3y-secret-0002";
  f.calls->creates[0].second(late);
  UR_EXPECT_EQ(1u, outcomes.size());
}

UR_TEST(ApiKeysBinding_OneCreateAtATimeAndOnlyANameThatCanBeSent) {
  Fixture f;
  const auto ignore = [](bool, std::string) {};
  UR_EXPECT_FALSE(f.binding.Create("", ignore));
  UR_EXPECT_FALSE(f.binding.Create("   ", ignore));
  UR_EXPECT_FALSE(f.binding.Create(std::string(129, 'x'), ignore));
  UR_EXPECT_EQ(0u, f.calls->creates.size());
  UR_EXPECT_TRUE(f.binding.Create("home server", ignore));
  UR_EXPECT_FALSE(f.binding.Create("laptop", ignore));
  UR_EXPECT_EQ(1u, f.calls->creates.size());
}

// A list already on its way may have been read before the new key was made:
// the list is asked again once it answers.
UR_TEST(ApiKeysBinding_ACreateWhileAListIsOutListsAgainAfterIt) {
  Fixture f;
  f.binding.Refresh();
  f.binding.Create("home server", [](bool, std::string) {});
  k::CreateAnswer answer;
  answer.ok = true;
  answer.apiKey = "urn_k3y-secret-0003";
  f.calls->creates[0].second(answer);
  UR_EXPECT_EQ(1u, f.calls->lists.size());  // one is out: no second at once
  f.calls->lists[0](Listed({KeyOf(kIdA, "laptop", 100)}));
  UR_EXPECT_EQ(2u, f.calls->lists.size());  // ...and the next one after it
  f.calls->lists[1](Listed({KeyOf(kIdA, "laptop", 100), KeyOf(kIdC, "home server", 300)}));
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdC, kIdA}));
  UR_EXPECT_EQ(2u, f.calls->lists.size());
}

UR_TEST(ApiKeysBinding_ADeletedKeyLeavesTheList) {
  Fixture f;
  LoadTwo(f);
  UR_EXPECT_TRUE(f.binding.Delete(kIdA));
  UR_EXPECT_TRUE(k::DeleteViewOf(f.binding.Read(), kIdA) == k::ActionView::Busy);
  UR_EXPECT_FALSE(f.binding.Delete(kIdA));  // that key's delete is out
  UR_EXPECT_FALSE(f.binding.Delete(""));
  UR_EXPECT_TRUE(f.binding.Delete(kIdB));   // another key's may go too
  UR_EXPECT_EQ(2u, f.calls->deletes.size());
  UR_EXPECT_TRUE(f.calls->deletes[0].first == kIdA && f.calls->deletes[1].first == kIdB);
  k::DeleteAnswer ok;
  ok.ok = true;
  f.calls->deletes[0].second(ok);
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdB}));
  UR_EXPECT_TRUE(k::DeleteViewOf(f.binding.Read(), kIdA) == k::ActionView::Idle);
  // a refused delete keeps the key, with its failure
  f.calls->deletes[1].second(k::DeleteAnswer{});
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdB}));
  UR_EXPECT_TRUE(k::DeleteViewOf(f.binding.Read(), kIdB) == k::ActionView::Failed);
  // tried again: busy, and its failure goes
  UR_EXPECT_TRUE(f.binding.Delete(kIdB));
  UR_EXPECT_TRUE(k::DeleteViewOf(f.binding.Read(), kIdB) == k::ActionView::Busy);
  UR_EXPECT_TRUE(f.binding.Read().deleteFailedIds.empty());
}

UR_TEST(ApiKeysBinding_ADeleteThatNeverAnswersIsGivenUpOn) {
  Fixture f;
  LoadTwo(f);
  f.binding.Delete(kIdA);
  f.clock.armed.back().second();
  UR_EXPECT_TRUE(k::DeleteViewOf(f.binding.Read(), kIdA) == k::ActionView::Failed);
  // the late success is dropped: the next list says what happened
  k::DeleteAnswer ok;
  ok.ok = true;
  f.calls->deletes[0].second(ok);
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdB, kIdA}));
  // a failure belongs to a key still listed
  f.binding.Refresh();
  f.calls->lists.back()(Listed({KeyOf(kIdB, "home server", 200)}));
  UR_EXPECT_TRUE(f.binding.Read().deleteFailedIds.empty());
}

// A list sent before a delete went through may still carry the key: it does
// not come back.
UR_TEST(ApiKeysBinding_AListOnItsWayCannotBringADeletedKeyBack) {
  Fixture f;
  LoadTwo(f);
  f.binding.Refresh();
  f.binding.Delete(kIdA);
  k::DeleteAnswer ok;
  ok.ok = true;
  f.calls->deletes[0].second(ok);
  f.calls->lists.back()(Listed({KeyOf(kIdA, "laptop", 100), KeyOf(kIdB, "home server", 200)}));
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdB}));
  // a list sent after the delete is the server's word again
  f.binding.Refresh();
  f.calls->lists.back()(Listed({KeyOf(kIdA, "laptop", 100), KeyOf(kIdB, "home server", 200)}));
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdB, kIdA}));
}

// A sign-out, an Api replaced or the page gone: nothing that was on its way
// lands, the departed account's keys go, and the next account's answers do.
UR_TEST(ApiKeysBinding_ReleaseOrphansEveryAnswer) {
  Fixture f;
  LoadTwo(f);
  int done = 0;
  f.binding.Refresh();
  f.binding.Create("home server", [&](bool, std::string) { ++done; });
  f.binding.Delete(kIdA);
  f.binding.Release();
  UR_EXPECT_FALSE(f.binding.Attached());
  UR_EXPECT_EQ(0u, f.binding.Source());
  UR_EXPECT_TRUE(f.calls->destroyed);
  UR_EXPECT_TRUE(f.binding.Read().keys.empty() && !f.binding.Read().loaded);
  const int rendersAtRelease = f.renders;
  // every answer still on its way, and every give-up, does nothing
  f.calls->lists.back()(Listed({KeyOf(kIdC, "other account", 300)}));
  k::CreateAnswer answer;
  answer.ok = true;
  answer.apiKey = "urn_k3y-secret-0004";
  f.calls->creates[0].second(answer);
  k::DeleteAnswer ok;
  ok.ok = true;
  f.calls->deletes[0].second(ok);
  for (auto& [delayMs, fire] : f.clock.armed) {
    (void)delayMs;
    fire();
  }
  UR_EXPECT_EQ(0, done);
  UR_EXPECT_TRUE(f.binding.Read().keys.empty() && !f.binding.Read().loadFailed);
  UR_EXPECT_EQ(rendersAtRelease, f.renders);
  // the next account's service: its answers land, the old ones still do not
  auto next = std::make_shared<Calls>();
  f.binding.Attach(std::make_unique<FakeService>(next), /*source=*/8);
  UR_EXPECT_EQ(8u, f.binding.Source());
  f.binding.Refresh();
  f.calls->lists.back()(Listed({KeyOf(kIdC, "other account", 300)}));
  UR_EXPECT_TRUE(f.binding.Read().keys.empty());
  next->lists.back()(Listed({KeyOf(kIdB, "home server", 200)}));
  UR_EXPECT_TRUE((IdsOf(f.binding.Read()) == std::vector<std::string>{kIdB}));
}

// A different Api is a different service; attaching releases the one before.
UR_TEST(ApiKeysBinding_AttachReplacesTheServiceOfAnotherApi) {
  Fixture f;
  UR_EXPECT_TRUE(f.binding.Attached());
  UR_EXPECT_EQ(7u, f.binding.Source());
  LoadTwo(f);
  auto second = std::make_shared<Calls>();
  f.binding.Attach(std::make_unique<FakeService>(second), /*source=*/9);
  UR_EXPECT_TRUE(f.calls->destroyed);
  UR_EXPECT_FALSE(second->destroyed);
  UR_EXPECT_EQ(9u, f.binding.Source());
  UR_EXPECT_TRUE(f.binding.Read().keys.empty());
  // attaching nothing leaves nothing attached
  f.binding.Attach(nullptr, 10);
  UR_EXPECT_FALSE(f.binding.Attached());
  UR_EXPECT_TRUE(second->destroyed);
}
