// The launch and the instance of InstanceHandover.hpp: a relaunch during Quit
// starts the app once the quitting instance has ended, a relaunch of a running
// instance is handed over and the launch exits, and autostart shows only the
// tray (owner decision, 2026-10-05). The bus is a fake whose answers are
// scripted call by call and which logs every call, so each ordering is forced
// by the script, never by timing.
//
// SPDX-License-Identifier: MPL-2.0
#include "InstanceHandover.hpp"

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "TestHarness.hpp"

namespace {

using urnw::instance::Activations;
using urnw::instance::Answer;
using urnw::instance::Arguments;
using urnw::instance::Bus;
using urnw::instance::Claim;
using urnw::instance::Gate;
using urnw::instance::Handed;
using urnw::instance::Launch;
using urnw::instance::LaunchKind;
using urnw::instance::Outcome;
using urnw::instance::Wait;

constexpr const char* kHolder = ":1.10";
constexpr const char* kEarlier = ":1.7";

// The next scripted answer; the last one repeats.
template <typename T>
T NextAnswer(std::deque<T>& answers, const T& unscripted) {
  if (answers.empty()) return unscripted;
  T answer = answers.front();
  if (answers.size() > 1) answers.pop_front();
  return answer;
}

// A session bus scripted per call. An unscripted call is logged and fails.
struct FakeBus {
  std::deque<Claim> claims;
  std::map<std::string, std::deque<std::optional<std::string>>> owners;
  std::deque<Handed> handovers;
  std::deque<Handed> legacyHandovers;
  std::deque<Wait> waits;
  std::vector<std::string> calls;
  std::vector<Launch> handedLaunches;

  Bus Bind() {
    Bus bus;
    bus.claim = [this] {
      calls.push_back("claim");
      return NextAnswer(claims, Claim::Failed);
    };
    bus.owner = [this](const std::string& name) -> std::optional<std::string> {
      calls.push_back("owner " + name);
      auto answers = owners.find(name);
      if (answers == owners.end()) return std::nullopt;
      return NextAnswer(answers->second, std::optional<std::string>());
    };
    bus.handover = [this](const std::string& holder, const Launch& launch, int budgetMillis) {
      calls.push_back("handover " + holder + " " + urnw::instance::ToWire(launch.kind) + " " +
                      std::to_string(budgetMillis));
      handedLaunches.push_back(launch);
      return NextAnswer(handovers, Handed::Failed);
    };
    bus.handoverLegacy = [this](const std::string& holder, const Launch& launch, int budgetMillis) {
      calls.push_back("legacy " + holder + " " + urnw::instance::ToWire(launch.kind) + " " +
                      std::to_string(budgetMillis));
      handedLaunches.push_back(launch);
      return NextAnswer(legacyHandovers, Handed::Failed);
    };
    bus.awaitGone = [this](const std::string& name, int budgetMillis) {
      calls.push_back("await " + name + " " + std::to_string(budgetMillis));
      return NextAnswer(waits, Wait::Failed);
    };
    return bus;
  }
};

bool SameCalls(const std::vector<std::string>& expected, const std::vector<std::string>& actual) {
  return expected == actual;
}

std::string Joined(const std::vector<std::string>& calls) {
  std::string out;
  for (const std::string& call : calls) out += "[" + call + "] ";
  return out;
}

#define EXPECT_CALLS(expected, bus)                                                       \
  UR_EXPECT_TRUE_MSG("calls were " + Joined((bus).calls), SameCalls((expected), (bus).calls))

#define EXPECT_OUTCOME(expected, actual)                                              \
  UR_EXPECT_TRUE_MSG(std::string("outcome was ") + urnw::instance::ToString(actual), \
                     (actual) == (expected))

const std::string kBusName = urnw::instance::kBusName;
const std::string kExitingName = urnw::instance::kExitingName;
const std::string kHandoverBudget = std::to_string(urnw::instance::kHandoverBudgetMillis);
const std::string kExitingBudget = std::to_string(urnw::instance::kExitingBudgetMillis);

Launch LaunchOf(LaunchKind kind) {
  Launch launch;
  launch.kind = kind;
  return launch;
}

// What a gate did, in order.
struct GateLog {
  std::vector<std::string> events;

  Gate::Serve Serve() {
    return [this](const Launch& launch) {
      std::string event = std::string("serve ") + urnw::instance::ToWire(launch.kind);
      for (const std::string& uri : launch.uris) event += " " + uri;
      if (!launch.activationToken.empty()) event += " token " + launch.activationToken;
      if (urnw::instance::ShowsWindow(launch.kind)) event += " window";
      events.push_back(event);
    };
  }

  Gate::Reply Reply(const std::string& who) {
    return [this, who](Answer answer) {
      events.push_back("reply " + who + " " + urnw::instance::ToWire(answer));
    };
  }
};

}  // namespace

// Owner rule 1: a first user launch claims the free name, finds no instance
// still exiting, and starts; its own activation is a plain launch, which shows
// the window.
UR_TEST(InstanceHandover_AFirstUserLaunchOpensTheWindow) {
  const Arguments arguments = urnw::instance::ParseArguments({}, /*autostartFlag=*/false);
  UR_EXPECT_TRUE(arguments.kind == LaunchKind::Open);
  UR_EXPECT_TRUE(arguments.handover);
  UR_EXPECT_TRUE(arguments.paths.empty());

  FakeBus bus;
  bus.claims = {Claim::Primary};
  bus.owners[kExitingName] = {std::string()};
  const Outcome outcome = urnw::instance::RunLaunch(bus.Bind(), LaunchOf(arguments.kind));
  EXPECT_OUTCOME(Outcome::Start, outcome);
  EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kExitingName}), bus);

  Activations activations(arguments.kind);
  UR_EXPECT_TRUE(urnw::instance::ShowsWindow(activations.Next()));
}

// Owner rule 2: the autostart entry's launch, by the option or by the
// launcher's flag, shows only the tray: its own activation shows no window,
// and a running instance it reaches shows none either. A launch after it
// shows the window.
UR_TEST(InstanceHandover_AutostartOpensOnlyTheTray) {
  UR_EXPECT_TRUE(urnw::instance::ParseArguments({"--autostart"}, false).kind ==
                 LaunchKind::Autostart);
  UR_EXPECT_TRUE(urnw::instance::ParseArguments({"--autostart"}, false).handover);
  UR_EXPECT_TRUE(urnw::instance::ParseArguments({}, /*autostartFlag=*/true).kind ==
                 LaunchKind::Autostart);
  UR_EXPECT_TRUE(urnw::instance::IsAutostartFlag("1"));
  for (const char* value : {"0", "", "true", "yes"}) {
    UR_EXPECT_TRUE_MSG(value, !urnw::instance::IsAutostartFlag(value));
  }
  UR_EXPECT_FALSE(urnw::instance::IsAutostartFlag(nullptr));

  // the instance it starts
  Activations activations(LaunchKind::Autostart);
  UR_EXPECT_FALSE(urnw::instance::ShowsWindow(activations.Next()));
  UR_EXPECT_TRUE(urnw::instance::ShowsWindow(activations.Next()));

  // the instance it reaches
  FakeBus bus;
  bus.claims = {Claim::Held};
  bus.owners[kBusName] = {std::string(kHolder)};
  bus.owners[kExitingName] = {std::string()};
  bus.handovers = {Handed::Served};
  EXPECT_OUTCOME(Outcome::HandedOver,
                 urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Autostart)));
  EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                         std::string("handover ") + kHolder + " autostart " +
                                             kHandoverBudget}),
               bus);
  GateLog log;
  Gate gate;
  gate.Open(log.Serve());
  gate.Take(LaunchOf(LaunchKind::Autostart), log.Reply("autostart"));
  UR_EXPECT_TRUE_MSG(Joined(log.events),
                     (log.events == std::vector<std::string>{"serve autostart",
                                                             "reply autostart served"}));
}

// Owner rule 3: a relaunch during Quit. The quitting instance owns the exiting
// name while its main loop stops for the tunnel, so the launch never hands
// itself to it: it waits for that instance's connection to close, claims the
// name, and starts, and its own activation shows the window. The same holds
// when the handover raced the start of Quit and the instance refused it.
UR_TEST(InstanceHandover_ARelaunchDuringQuitOpensTheWindowWhenTheNewInstanceStarts) {
  {
    FakeBus bus;
    bus.claims = {Claim::Held, Claim::Primary};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string(kHolder), std::string()};
    bus.waits = {Wait::Gone};
    const Outcome outcome = urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open));
    EXPECT_OUTCOME(Outcome::Start, outcome);
    EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                           std::string("await ") + kHolder + " " + kExitingBudget,
                                           "claim", "owner " + kExitingName}),
                 bus);
    UR_EXPECT_TRUE(bus.handedLaunches.empty());
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Held, Claim::Primary};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.handovers = {Handed::Refused};
    bus.waits = {Wait::Gone};
    const Outcome outcome = urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open));
    EXPECT_OUTCOME(Outcome::Start, outcome);
    EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                           std::string("handover ") + kHolder + " open " +
                                               kHandoverBudget,
                                           std::string("await ") + kHolder + " " + kExitingBudget,
                                           "claim", "owner " + kExitingName}),
                 bus);
  }
  Activations activations(LaunchKind::Open);
  UR_EXPECT_TRUE(urnw::instance::ShowsWindow(activations.Next()));
}

// Owner rule 4: a relaunch of a running instance is handed over and answered,
// and the launch exits; the instance shows its window for it, raised with the
// launch's startup notification id.
UR_TEST(InstanceHandover_ARelaunchOfAHealthyInstanceShowsTheWindow) {
  FakeBus bus;
  bus.claims = {Claim::Held};
  bus.owners[kBusName] = {std::string(kHolder)};
  bus.owners[kExitingName] = {std::string()};
  bus.handovers = {Handed::Served};
  Launch launch = LaunchOf(LaunchKind::Open);
  launch.activationToken = "token-test-1";
  EXPECT_OUTCOME(Outcome::HandedOver, urnw::instance::RunLaunch(bus.Bind(), launch));
  EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                         std::string("handover ") + kHolder + " open " +
                                             kHandoverBudget}),
               bus);
  UR_EXPECT_TRUE(bus.handedLaunches.size() == 1 &&
                 bus.handedLaunches[0].activationToken == "token-test-1");

  GateLog log;
  Gate gate;
  gate.Open(log.Serve());
  gate.Take(launch, log.Reply("relaunch"));
  UR_EXPECT_TRUE_MSG(Joined(log.events),
                     (log.events == std::vector<std::string>{"serve open token token-test-1 window",
                                                             "reply relaunch served"}));
}

// A holder that ends while the handover waits (no reply, or its name gone) is
// claimed again at once, without a wait.
UR_TEST(InstanceHandover_AHolderThatEndsMidHandoverIsClaimedAgainAtOnce) {
  FakeBus bus;
  bus.claims = {Claim::Held, Claim::Primary};
  bus.owners[kBusName] = {std::string(kHolder)};
  bus.owners[kExitingName] = {std::string()};
  bus.handovers = {Handed::Gone};
  EXPECT_OUTCOME(Outcome::Start, urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                         std::string("handover ") + kHolder + " open " +
                                             kHandoverBudget,
                                         "claim", "owner " + kExitingName}),
               bus);
  // so is one that ended between the claim and the lookup of its name
  FakeBus ended;
  ended.claims = {Claim::Held, Claim::Primary};
  ended.owners[kBusName] = {std::string()};
  ended.owners[kExitingName] = {std::string()};
  EXPECT_OUTCOME(Outcome::Start,
                 urnw::instance::RunLaunch(ended.Bind(), LaunchOf(LaunchKind::Open)));
  EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "claim",
                                         "owner " + kExitingName}),
               ended);
}

// A handover the holder does not answer in time: an exiting holder is waited
// out and the name claimed again; a running one holds the launch and serves it
// once its main loop runs again, so the launch exits; and one whose connection
// is gone never got it, so the name is claimed again (a bus that does not
// answer a call to a peer that left with no reply ends it this way).
UR_TEST(InstanceHandover_AHandoverThatTimesOutIsLeftOnlyWithARunningHolder) {
  {
    FakeBus bus;
    bus.claims = {Claim::Held, Claim::Primary};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string(), std::string(kHolder), std::string()};
    bus.handovers = {Handed::TimedOut};
    bus.waits = {Wait::Gone};
    EXPECT_OUTCOME(Outcome::Start,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
    EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                           std::string("handover ") + kHolder + " open " +
                                               kHandoverBudget,
                                           "owner " + kExitingName,
                                           std::string("await ") + kHolder + " " + kExitingBudget,
                                           "claim", "owner " + kExitingName}),
                 bus);
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Held};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.owners[kHolder] = {std::string(kHolder)};
    bus.handovers = {Handed::TimedOut};
    EXPECT_OUTCOME(Outcome::HandedOver,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
    EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                           std::string("handover ") + kHolder + " open " +
                                               kHandoverBudget,
                                           "owner " + kExitingName,
                                           std::string("owner ") + kHolder}),
                 bus);
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Held, Claim::Primary};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.owners[kHolder] = {std::string()};
    bus.handovers = {Handed::TimedOut};
    EXPECT_OUTCOME(Outcome::Start,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
    EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                           std::string("handover ") + kHolder + " open " +
                                               kHandoverBudget,
                                           "owner " + kExitingName,
                                           std::string("owner ") + kHolder, "claim",
                                           "owner " + kExitingName}),
                 bus);
  }
}

// The name can be free while an instance still ends (GApplication gives it up
// at the end of its run, before the process has torn down): the launch that
// claims it waits for that instance first, then starts.
UR_TEST(InstanceHandover_ANameClaimedWhileAnInstanceStillExitsWaitsForIt) {
  FakeBus bus;
  bus.claims = {Claim::Primary};
  bus.owners[kExitingName] = {std::string(kEarlier), std::string()};
  bus.waits = {Wait::Gone};
  EXPECT_OUTCOME(Outcome::Start, urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kExitingName,
                                         std::string("await ") + kEarlier + " " + kExitingBudget,
                                         "claim", "owner " + kExitingName}),
               bus);
}

// An exiting instance that does not end within the budget is reported as
// still closing: the launch never starts beside it, and never reports that it
// was handed over.
UR_TEST(InstanceHandover_AnExitingInstanceThatDoesNotEndInTimeIsStillClosing) {
  {
    FakeBus bus;
    bus.claims = {Claim::Held};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string(kHolder)};
    bus.waits = {Wait::TimedOut};
    EXPECT_OUTCOME(Outcome::StillClosing,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Primary};
    bus.owners[kExitingName] = {std::string(kEarlier)};
    bus.waits = {Wait::TimedOut};
    EXPECT_OUTCOME(Outcome::StillClosing,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Held};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.handovers = {Handed::Refused};
    bus.waits = {Wait::TimedOut};
    EXPECT_OUTCOME(Outcome::StillClosing,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
}

// An instance from before the handover has no handover object: it gets
// GApplication's activation, as it always did, and an autostart leaves it
// alone.
UR_TEST(InstanceHandover_AHolderFromBeforeTheHandoverIsActivatedAsBefore) {
  {
    FakeBus bus;
    bus.claims = {Claim::Held};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.handovers = {Handed::Unsupported};
    bus.legacyHandovers = {Handed::Served};
    EXPECT_OUTCOME(Outcome::HandedOver,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
    EXPECT_CALLS((std::vector<std::string>{"claim", "owner " + kBusName, "owner " + kExitingName,
                                           std::string("handover ") + kHolder + " open " +
                                               kHandoverBudget,
                                           std::string("legacy ") + kHolder + " open " +
                                               kHandoverBudget}),
                 bus);
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Held};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.handovers = {Handed::Unsupported};
    EXPECT_OUTCOME(Outcome::HandedOver,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Autostart)));
    UR_EXPECT_TRUE_MSG(Joined(bus.calls), bus.calls.back().rfind("handover ", 0) == 0);
  }
  {
    // it ended during the activation: claimed again
    FakeBus bus;
    bus.claims = {Claim::Held, Claim::Primary};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.handovers = {Handed::Unsupported};
    bus.legacyHandovers = {Handed::Gone};
    EXPECT_OUTCOME(Outcome::Start,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Link)));
  }
  {
    // an activation it did not answer in time stays with it while it runs,
    // and is claimed again once its connection is gone
    for (const bool connected : {true, false}) {
      FakeBus bus;
      bus.claims = {Claim::Held, Claim::Primary};
      bus.owners[kBusName] = {std::string(kHolder)};
      bus.owners[kExitingName] = {std::string()};
      bus.owners[kHolder] = {connected ? std::string(kHolder) : std::string()};
      bus.handovers = {Handed::Unsupported};
      bus.legacyHandovers = {Handed::TimedOut};
      const Outcome outcome = urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open));
      EXPECT_OUTCOME(connected ? Outcome::HandedOver : Outcome::Start, outcome);
    }
  }
}

// A name that changes hands on every round does not keep the launch forever.
UR_TEST(InstanceHandover_TheClaimsAreBounded) {
  FakeBus bus;
  bus.claims = {Claim::Held};
  bus.owners[kBusName] = {std::string(kHolder)};
  bus.owners[kExitingName] = {std::string()};
  bus.handovers = {Handed::Gone};
  EXPECT_OUTCOME(Outcome::GaveUp, urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  int claims = 0;
  for (const std::string& call : bus.calls) claims += call == "claim" ? 1 : 0;
  UR_EXPECT_EQ(urnw::instance::kLaunchRounds, claims);
}

// A bus that cannot answer leaves the launch to GApplication, as it was
// before the handover; no bus at all starts the app, as GApplication does.
UR_TEST(InstanceHandover_ABusThatFailsLeavesTheLaunchToGApplication) {
  {
    FakeBus bus;
    bus.claims = {Claim::NoBus};
    EXPECT_OUTCOME(Outcome::Start,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Failed};
    EXPECT_OUTCOME(Outcome::Fallback,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
  {
    // the holder's name could not be looked up
    FakeBus bus;
    bus.claims = {Claim::Held};
    EXPECT_OUTCOME(Outcome::Fallback,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
  {
    FakeBus bus;
    bus.claims = {Claim::Held};
    bus.owners[kBusName] = {std::string(kHolder)};
    bus.owners[kExitingName] = {std::string()};
    bus.handovers = {Handed::Failed};
    EXPECT_OUTCOME(Outcome::Fallback,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
  {
    // the name is this launch's even when the exiting name cannot be read
    FakeBus bus;
    bus.claims = {Claim::Primary};
    EXPECT_OUTCOME(Outcome::Start,
                   urnw::instance::RunLaunch(bus.Bind(), LaunchOf(LaunchKind::Open)));
  }
}

// A link is handed over with its uris, and the instance routes it and shows
// the window, as a link opened at a cold start does.
UR_TEST(InstanceHandover_ALinkIsHandedOverAndRoutedWithTheWindow) {
  const Arguments arguments =
      urnw::instance::ParseArguments({"urnetwork://callback.example/wallet"}, false);
  UR_EXPECT_TRUE(arguments.kind == LaunchKind::Link);
  UR_EXPECT_TRUE(arguments.paths == std::vector<std::string>{"urnetwork://callback.example/wallet"});
  Launch launch = LaunchOf(arguments.kind);
  launch.uris = arguments.paths;

  FakeBus bus;
  bus.claims = {Claim::Held};
  bus.owners[kBusName] = {std::string(kHolder)};
  bus.owners[kExitingName] = {std::string()};
  bus.handovers = {Handed::Served};
  EXPECT_OUTCOME(Outcome::HandedOver, urnw::instance::RunLaunch(bus.Bind(), launch));
  UR_EXPECT_TRUE(bus.handedLaunches.size() == 1 && bus.handedLaunches[0].uris == launch.uris);

  GateLog log;
  Gate gate;
  gate.Open(log.Serve());
  gate.Take(launch, log.Reply("link"));
  UR_EXPECT_TRUE_MSG(Joined(log.events),
                     (log.events ==
                      std::vector<std::string>{"serve link urnetwork://callback.example/wallet window",
                                               "reply link served"}));
}

// The command line as GApplication reads it: positional arguments are links,
// and a link outranks the autostart flag; any option but --autostart (and
// "--") leaves the launch to GApplication, which answers or refuses it.
UR_TEST(InstanceHandover_ParseArgumentsReadsTheCommandLineAsGApplicationDoes) {
  struct Case {
    std::vector<std::string> args;
    bool flag;
    LaunchKind kind;
    bool handover;
    size_t paths;
  };
  const std::vector<Case> cases = {
      {{}, false, LaunchKind::Open, true, 0},
      {{"--autostart"}, false, LaunchKind::Autostart, true, 0},
      {{}, true, LaunchKind::Autostart, true, 0},
      {{"urnetwork://callback.example/a"}, false, LaunchKind::Link, true, 1},
      {{"urnetwork://callback.example/a", "urnetwork://callback.example/b"}, false, LaunchKind::Link, true, 2},
      {{"--autostart", "urnetwork://callback.example/a"}, false, LaunchKind::Link, true, 1},
      {{"urnetwork://callback.example/a"}, true, LaunchKind::Link, true, 1},
      {{"-"}, false, LaunchKind::Link, true, 1},
      {{"--help"}, false, LaunchKind::Open, false, 0},
      {{"-h"}, false, LaunchKind::Open, false, 0},
      {{"--"}, false, LaunchKind::Open, false, 0},
      {{"--gapplication-service"}, false, LaunchKind::Open, false, 0},
      {{"--autostart=1"}, false, LaunchKind::Open, false, 0},
  };
  for (size_t at = 0; at < cases.size(); ++at) {
    const Case& c = cases[at];
    const Arguments arguments = urnw::instance::ParseArguments(c.args, c.flag);
    const std::string which = "case " + std::to_string(at);
    UR_EXPECT_TRUE_MSG(which, arguments.kind == c.kind);
    UR_EXPECT_TRUE_MSG(which, arguments.handover == c.handover);
    UR_EXPECT_TRUE_MSG(which, arguments.paths.size() == c.paths);
  }
}

// Launches that reach the instance while it starts are held, not refused and
// not served into a window that does not exist; once the window and the tray
// exist they are served in the order they came, each answered after it is
// served, and later ones are served at once.
UR_TEST(InstanceHandover_LaunchesThatArriveWhileStartingAreServedWhenTheInstanceOpens) {
  GateLog log;
  Gate gate;
  UR_EXPECT_TRUE(gate.state() == Gate::State::Starting);
  Launch link = LaunchOf(LaunchKind::Link);
  link.uris = {"urnetwork://callback.example/first"};
  gate.Take(link, log.Reply("first"));
  gate.Take(LaunchOf(LaunchKind::Open), log.Reply("second"));
  UR_EXPECT_TRUE_MSG(Joined(log.events), log.events.empty());
  gate.Open(log.Serve());
  UR_EXPECT_TRUE(gate.state() == Gate::State::Open);
  gate.Take(LaunchOf(LaunchKind::Open), log.Reply("third"));
  UR_EXPECT_TRUE_MSG(Joined(log.events),
                     (log.events == std::vector<std::string>{
                                        "serve link urnetwork://callback.example/first window",
                                        "reply first served", "serve open window",
                                        "reply second served", "serve open window",
                                        "reply third served"}));
}

// Beginning to exit owns the exiting name first, so a launch that is refused
// already finds the instance exiting; then the held launches are refused, and
// every later launch at once, never served. Only the first close acts, and a
// closed instance does not open.
UR_TEST(InstanceHandover_AnExitingInstanceOwnsTheExitingNameBeforeItRefusesAnything) {
  GateLog log;
  Gate gate;
  gate.Take(LaunchOf(LaunchKind::Open), log.Reply("held"));
  gate.Close([&log] { log.events.push_back("raise exiting"); });
  gate.Close([&log] { log.events.push_back("raise exiting again"); });
  gate.Take(LaunchOf(LaunchKind::Open), log.Reply("later"));
  gate.Open(log.Serve());
  gate.Take(LaunchOf(LaunchKind::Link), log.Reply("after open"));
  UR_EXPECT_TRUE(gate.state() == Gate::State::Closed);
  UR_EXPECT_TRUE_MSG(Joined(log.events),
                     (log.events == std::vector<std::string>{"raise exiting", "reply held refused",
                                                             "reply later refused",
                                                             "reply after open refused"}));

  // an open instance that begins to exit
  GateLog running;
  Gate open;
  open.Open(running.Serve());
  open.Take(LaunchOf(LaunchKind::Open), running.Reply("before"));
  open.Close([&running] { running.events.push_back("raise exiting"); });
  open.Take(LaunchOf(LaunchKind::Open), running.Reply("during quit"));
  UR_EXPECT_TRUE_MSG(Joined(running.events),
                     (running.events ==
                      std::vector<std::string>{"serve open window", "reply before served",
                                               "raise exiting", "reply during quit refused"}));
}

// An instance that fails to start begins to exit with launches still held:
// they are refused, so their launches claim the name again.
UR_TEST(InstanceHandover_AnInstanceThatFailsToStartRefusesWhatItHeld) {
  GateLog log;
  Gate gate;
  gate.Take(LaunchOf(LaunchKind::Open), log.Reply("first"));
  gate.Take(LaunchOf(LaunchKind::Autostart), log.Reply("second"));
  gate.Close(nullptr);
  UR_EXPECT_TRUE_MSG(Joined(log.events),
                     (log.events == std::vector<std::string>{"reply first refused",
                                                             "reply second refused"}));
}

// A served launch that begins the exit (its serve closes the gate) is still
// answered as served; the launches held behind it are refused.
UR_TEST(InstanceHandover_AnExitThatBeginsWhileServingRefusesTheRest) {
  GateLog log;
  Gate gate;
  gate.Take(LaunchOf(LaunchKind::Open), log.Reply("first"));
  gate.Take(LaunchOf(LaunchKind::Open), log.Reply("second"));
  const Gate::Serve serve = log.Serve();
  gate.Open([&gate, &log, &serve](const Launch& launch) {
    serve(launch);
    gate.Close([&log] { log.events.push_back("raise exiting"); });
  });
  UR_EXPECT_TRUE_MSG(Joined(log.events),
                     (log.events == std::vector<std::string>{"serve open window", "raise exiting",
                                                             "reply second refused",
                                                             "reply first served"}));
}

// The handover's wire forms are the ones the instance parses.
UR_TEST(InstanceHandover_TheWireFormsRoundTrip) {
  for (const LaunchKind kind : {LaunchKind::Open, LaunchKind::Autostart, LaunchKind::Link}) {
    const auto parsed = urnw::instance::LaunchKindFromWire(urnw::instance::ToWire(kind));
    UR_EXPECT_TRUE(parsed && *parsed == kind);
  }
  for (const Answer answer : {Answer::Served, Answer::Refused}) {
    const auto parsed = urnw::instance::AnswerFromWire(urnw::instance::ToWire(answer));
    UR_EXPECT_TRUE(parsed && *parsed == answer);
  }
  UR_EXPECT_FALSE(urnw::instance::LaunchKindFromWire("show").has_value());
  UR_EXPECT_FALSE(urnw::instance::AnswerFromWire("").has_value());
}
