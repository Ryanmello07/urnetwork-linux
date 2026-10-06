// How a launch of the URnetwork GUI reaches the instance that already runs in
// the desktop session, and how that instance answers it.
//
// One instance runs per session: GApplication makes a launch the primary when
// it owns the session bus name com.bringyour.network. Every other launch used
// to become a GApplication remote, which sends Activate or Open without
// waiting for an answer, and which then never exited (main.cpp held the
// application before run(), so the remote's loop never ran out). A launch that
// met an instance while it quit was lost on top of that: the tray's Quit stops
// the tunnel on the main loop, and the instance then exits without reading
// what arrived in the meantime.
//
// The launch (RunLaunch), before GApplication registers:
//   * it claims the bus name itself. With the name, it first waits for an
//     instance that is still exiting (one owns kExitingName from the moment
//     it begins to exit until its process ends), then starts as a first
//     launch.
//   * when another instance holds the name, it hands the launch over with
//     Handover, on that instance's own connection, and waits for the answer.
//     Served: the launch exits. Refused: that instance is exiting, so the
//     launch waits for its connection to close and claims the name again. An
//     instance that is gone is claimed again at once, and one from before the
//     handover gets GApplication's Activate or Open.
//   * each wait is bounded (kHandoverBudgetMillis, kExitingBudgetMillis), and
//     so is the number of claims (kLaunchRounds).
// The instance (Gate) holds the launches that reach it while it starts,
// serves them once its window and tray exist, and refuses every launch from
// the moment it begins to exit, after it owns kExitingName.
//
// Autostart (owner decision, 2026-10-05: autostart on system start launches
// only the tray icon). The autostart entry runs the launcher with
// --autostart, and the launcher hands the GUI URNETWORK_AUTOSTART=1 instead,
// because a GUI that predates the option refuses to start on an unknown one.
// The GUI takes the option too. Such a launch shows only the tray; every
// other launch, and every later launch handed to the instance, shows the
// window.
//
// Pure, header-only, C++17 and free of glib: tests/InstanceHandoverTest.cpp
// runs it against a fake bus, and SingleInstance.cpp binds the session bus.
// Not safe for concurrent use: all of it runs on the main thread.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace urnw::instance {

// The application id, which GApplication owns on the session bus.
inline constexpr const char* kBusName = "com.bringyour.network";
// Owned by an instance from the moment it begins to exit until its process
// ends, on the connection that owns kBusName, so a launch can tell an exiting
// holder from a busy one while its main loop is stopped.
inline constexpr const char* kExitingName = "com.bringyour.network.Exiting";
// The handover object, next to GApplication's own.
inline constexpr const char* kObjectPath = "/com/bringyour/network/Instance";
inline constexpr const char* kInterface = "com.bringyour.network.Instance";
// GApplication's object, for an instance from before the handover.
inline constexpr const char* kApplicationPath = "/com/bringyour/network";

inline constexpr const char* kAutostartOption = "--autostart";
// Set by the launcher for --autostart; read once and unset, so nothing the
// GUI starts inherits it (the updater's relaunch is a launch of its own).
inline constexpr const char* kAutostartEnvironment = "URNETWORK_AUTOSTART";

// How long a launch waits for the instance to answer a handover. A running
// instance answers as soon as its main loop runs; one that has not answered
// by then still holds the launch and serves it later.
inline constexpr int kHandoverBudgetMillis = 10 * 1000;
// How long a launch waits for an exiting instance to end. Quit stops the
// tunnel over the daemon's control socket, whose receive timeout is 30 s and
// which retries once (ControlClient.cpp).
inline constexpr int kExitingBudgetMillis = 60 * 1000;
// How many times a launch claims the name.
inline constexpr int kLaunchRounds = 4;

enum class LaunchKind {
  // A plain launch: the window shows.
  Open,
  // The autostart entry: only the tray.
  Autostart,
  // urnetwork:// links (wallet callbacks, onboarding): routed, and the window
  // shows.
  Link,
};

// Whether serving a launch of this kind shows the window.
constexpr bool ShowsWindow(LaunchKind kind) {
  return kind != LaunchKind::Autostart;
}

// The Handover argument.
constexpr const char* ToWire(LaunchKind kind) {
  switch (kind) {
    case LaunchKind::Open: return "open";
    case LaunchKind::Autostart: return "autostart";
    case LaunchKind::Link: return "link";
  }
  return "open";
}

inline std::optional<LaunchKind> LaunchKindFromWire(std::string_view wire) {
  for (const LaunchKind kind : {LaunchKind::Open, LaunchKind::Autostart, LaunchKind::Link}) {
    if (wire == ToWire(kind)) return kind;
  }
  return std::nullopt;
}

// The instance's answer to a handover.
enum class Answer {
  // Shown, or routed (an autostart has nothing to show).
  Served,
  // The instance is exiting.
  Refused,
};

constexpr const char* ToWire(Answer answer) {
  switch (answer) {
    case Answer::Served: return "served";
    case Answer::Refused: return "refused";
  }
  return "refused";
}

inline std::optional<Answer> AnswerFromWire(std::string_view wire) {
  for (const Answer answer : {Answer::Served, Answer::Refused}) {
    if (wire == ToWire(answer)) return answer;
  }
  return std::nullopt;
}

struct Launch {
  LaunchKind kind = LaunchKind::Open;
  // Link: the arguments as GApplication turns them into uris.
  std::vector<std::string> uris;
  // The launch's startup notification id, which lets the instance raise its
  // window for it; empty when the launch carries none.
  std::string activationToken;
};

// The launcher's environment flag.
inline bool IsAutostartFlag(const char* value) {
  return value != nullptr && std::string_view(value) == "1";
}

// The command line as GApplication reads it.
struct Arguments {
  LaunchKind kind = LaunchKind::Open;
  // The positional arguments, which GApplication opens as files or uris.
  std::vector<std::string> paths;
  // False when the command line has an option only GApplication answers
  // (--help, "--", an option it refuses): the launch is then GApplication's,
  // exactly as before the handover.
  bool handover = true;
};

// args are argv without argv[0]; autostartFlag is the launcher's flag.
inline Arguments ParseArguments(const std::vector<std::string>& args, bool autostartFlag) {
  Arguments arguments;
  bool autostart = autostartFlag;
  for (const std::string& arg : args) {
    if (arg == kAutostartOption) {
      autostart = true;
    } else if (arg.size() > 1 && arg[0] == '-') {
      arguments.handover = false;
    } else {
      arguments.paths.push_back(arg);
    }
  }
  if (!arguments.paths.empty()) {
    arguments.kind = LaunchKind::Link;
  } else if (autostart) {
    arguments.kind = LaunchKind::Autostart;
  }
  return arguments;
}

// What each activation of the instance stands for. GApplication emits the
// process's own launch first, before the main loop runs (Activate, or Open
// with its arguments); every later activation is a launch from before the
// handover, and shows the window.
class Activations {
 public:
  explicit Activations(LaunchKind own) : own_(own) {}

  LaunchKind Next() {
    if (ownServed_) return LaunchKind::Open;
    ownServed_ = true;
    return own_;
  }

 private:
  LaunchKind own_;
  bool ownServed_ = false;
};

// The launches handed to this instance, by its state: held while it starts,
// served once its window and tray exist, refused once it begins to exit.
class Gate {
 public:
  enum class State { Starting, Open, Closed };
  using Serve = std::function<void(const Launch&)>;
  using Reply = std::function<void(Answer)>;

  // A launch from Handover. reply runs once: after serve when served, at
  // Open or Close for a held launch, at once otherwise.
  void Take(Launch launch, Reply reply) {
    switch (state_) {
      case State::Starting:
        pending_.push_back(Pending{std::move(launch), std::move(reply)});
        return;
      case State::Open:
        serve_(launch);
        reply(Answer::Served);
        return;
      case State::Closed:
        reply(Answer::Refused);
        return;
    }
  }

  // The window and the tray exist: the held launches are served in the order
  // they came, and every later one at once. Ignored once closed.
  void Open(Serve serve) {
    if (state_ != State::Starting) return;
    state_ = State::Open;
    serve_ = std::move(serve);
    // a serve that begins the exit leaves the rest to Close
    while (state_ == State::Open && !pending_.empty()) {
      Pending next = std::move(pending_.front());
      pending_.pop_front();
      serve_(next.launch);
      next.reply(Answer::Served);
    }
  }

  // The instance begins to exit. raiseExiting runs first, so a launch that is
  // refused finds kExitingName owned; then every held launch is refused, and
  // so is every later one. Only the first call acts.
  void Close(const std::function<void()>& raiseExiting) {
    if (state_ == State::Closed) return;
    state_ = State::Closed;
    if (raiseExiting) raiseExiting();
    std::deque<Pending> refused;
    refused.swap(pending_);
    for (Pending& pending : refused) pending.reply(Answer::Refused);
  }

  State state() const { return state_; }

 private:
  struct Pending {
    Launch launch;
    Reply reply;
  };

  State state_ = State::Starting;
  Serve serve_;
  std::deque<Pending> pending_;
};

enum class Claim {
  // This launch owns kBusName now (or already did).
  Primary,
  // Another connection owns it.
  Held,
  // There is no session bus: GApplication runs without uniqueness then too.
  NoBus,
  Failed,
};

enum class Handed {
  Served,
  Refused,
  // The holder's connection is gone (it ended, or gave the name up).
  Gone,
  // The holder has no handover object: it predates the handover.
  Unsupported,
  TimedOut,
  Failed,
};

enum class Wait { Gone, TimedOut, Failed };

// The session bus as a launch sees it. Every call is bounded by its budget.
struct Bus {
  // Claims kBusName without queueing for it.
  std::function<Claim()> claim;
  // The unique connection name that owns a name, "" for none, nullopt when
  // the bus could not say.
  std::function<std::optional<std::string>(const std::string& name)> owner;
  // Handover on the holder's connection.
  std::function<Handed(const std::string& holder, const Launch& launch, int budgetMillis)> handover;
  // GApplication's Activate (Open for links) on the holder's connection.
  std::function<Handed(const std::string& holder, const Launch& launch, int budgetMillis)>
      handoverLegacy;
  // Until no connection owns the name.
  std::function<Wait(const std::string& name, int budgetMillis)> awaitGone;
};

enum class Outcome {
  // This launch owns the name, or there is no bus: it starts as the instance.
  Start,
  // The bus failed: GApplication registers and decides, as before.
  Fallback,
  // The instance took the launch, or holds it to serve: the launch exits.
  HandedOver,
  // An exiting instance did not end within kExitingBudgetMillis.
  StillClosing,
  // kLaunchRounds claims did not settle.
  GaveUp,
};

// For logs.
constexpr const char* ToString(Outcome outcome) {
  switch (outcome) {
    case Outcome::Start: return "start";
    case Outcome::Fallback: return "left to GApplication";
    case Outcome::HandedOver: return "handed over";
    case Outcome::StillClosing: return "the running instance is still closing";
    case Outcome::GaveUp: return "the running instance kept changing";
  }
  return "unknown";
}

inline Outcome RunLaunch(const Bus& bus, const Launch& launch) {
  // An outcome ends the launch; nullopt claims the name again.
  using Step = std::optional<Outcome>;
  const auto awaitExit = [&bus](const std::string& exiting, Outcome onFailure) -> Step {
    switch (bus.awaitGone(exiting, kExitingBudgetMillis)) {
      case Wait::Gone: return std::nullopt;
      case Wait::TimedOut: return Outcome::StillClosing;
      case Wait::Failed: return onFailure;
    }
    return onFailure;
  };
  const auto holderExiting = [&bus](const std::string& holder) {
    const std::optional<std::string> exiting = bus.owner(kExitingName);
    return exiting && *exiting == holder;
  };
  // A handover the holder did not answer in time. A running holder serves it
  // once its main loop runs again; one that is exiting never will, and one
  // whose connection is gone did not get it.
  const auto afterTimeout = [&](const std::string& holder) -> Step {
    if (holderExiting(holder)) return awaitExit(holder, Outcome::Fallback);
    const std::optional<std::string> connected = bus.owner(holder);
    if (connected && connected->empty()) return std::nullopt;
    return Outcome::HandedOver;
  };
  // An instance from before the handover is activated the way GApplication
  // activated it. An autostart has nothing to show there.
  const auto handOverLegacy = [&](const std::string& holder) -> Step {
    if (launch.kind == LaunchKind::Autostart) return Outcome::HandedOver;
    switch (bus.handoverLegacy(holder, launch, kHandoverBudgetMillis)) {
      case Handed::Served:
        return Outcome::HandedOver;
      case Handed::TimedOut:
        return afterTimeout(holder);
      case Handed::Gone:
        return std::nullopt;
      case Handed::Refused:
      case Handed::Unsupported:
      case Handed::Failed:
        return Outcome::Fallback;
    }
    return Outcome::Fallback;
  };
  const auto handOver = [&](const std::string& holder) -> Step {
    if (holderExiting(holder)) return awaitExit(holder, Outcome::Fallback);
    switch (bus.handover(holder, launch, kHandoverBudgetMillis)) {
      case Handed::Served:
        return Outcome::HandedOver;
      case Handed::Refused:
        return awaitExit(holder, Outcome::Fallback);
      case Handed::Gone:
        return std::nullopt;
      case Handed::Unsupported:
        return handOverLegacy(holder);
      case Handed::TimedOut:
        return afterTimeout(holder);
      case Handed::Failed:
        return Outcome::Fallback;
    }
    return Outcome::Fallback;
  };
  for (int round = 0; round < kLaunchRounds; ++round) {
    Step step;
    switch (bus.claim()) {
      case Claim::NoBus:
        return Outcome::Start;
      case Claim::Failed:
        return Outcome::Fallback;
      case Claim::Primary: {
        // the name is this launch's, and an instance that gave it up can still
        // be ending; the next claim looks again for one next in line
        const std::optional<std::string> exiting = bus.owner(kExitingName);
        if (!exiting || exiting->empty()) return Outcome::Start;
        step = awaitExit(*exiting, Outcome::Start);
        break;
      }
      case Claim::Held: {
        const std::optional<std::string> holder = bus.owner(kBusName);
        if (!holder) return Outcome::Fallback;
        // an empty holder ended between the claim and the lookup
        if (!holder->empty()) step = handOver(*holder);
        break;
      }
    }
    if (step) return *step;
  }
  return Outcome::GaveUp;
}

}  // namespace urnw::instance
