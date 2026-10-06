// Signing out of URnetwork (SignOut.hpp, owner decisions 2026-10-05): a
// sign-out sends Quit's stop_tunnel, then logout, after which the daemon keeps
// neither the device identity nor the credential its sdk stored for the
// account, so the next network starts on a new identity; one the daemon could
// not be told stays owed in a marker that outlives the app, is delivered before
// anything else once the daemon can be reached, and holds every start until
// then; another user's session, its identity and its credential are never
// touched; and the next sign-in's reconcile starts what its settings say, as a
// fresh launch's does.
//
// The daemon is a fake with urnetworkd's semantics where a sign-out meets
// them: stop_tunnel ends this user's session and retires the provider-only
// device; logout does too, and deletes the identity and the stored credential,
// but is refused beside another user's live session and while a bring-up owns
// the session, and a daemon that predates it answers `unknown verb`; a start
// makes the identity once and stores its account's credential; set_provide
// with a mode that does not provide retires the device; a start is refused
// beside a session; a restart ends what ran and starts nothing by itself; a
// status for another user's session is redacted. The app is modeled as
// SdkHost runs it, and those lines are pinned in SdkHost by
// SignOutWiringTest.cpp: the reconcile delivers an owed sign-out first, a
// signed-out reconcile provides nothing, nothing starts while a sign-out is
// owed, and a daemon's `unknown verb` to logout counts as done. Nothing here
// waits on a clock.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ProvideLifecycle.hpp"
#include "SignOut.hpp"

namespace provide = urnw::provide;
namespace signout = urnw::signout;
using signout::Delivery;
using signout::Request;

namespace {

// The daemon, as far as a sign-out meets it.
class FakeDaemon {
 public:
  // The control socket answers and the hello succeeds.
  bool reachable = true;
  // polkit denies this user's control-tunnel action (an inactive session, a
  // dismissed prompt), which stop_tunnel and logout both ask for.
  bool refusesStop = false;
  // A bring-up owns the session, so a logout is refused (start_in_progress).
  bool busy = false;
  // urnetworkd predates the logout verb.
  bool predatesLogout = false;
  // `status` does not answer.
  bool silent = false;
  // Another user's session runs: their tunnel, their provider.
  bool otherUsersSession = false;
  // Every request that reached the daemon, in order.
  std::vector<std::string> log;
  // Called before each sign-out request is served.
  std::function<void(Request)> onRequest;
  // What runs for this user, and for whom: gone with the process.
  std::string tunnelAccount;
  std::string providerAccount;
  // On disk: the device identity the daemon keeps (one per daemon, "" when
  // none) and the client credential its sdk stored in the space.
  std::string identity;
  std::string storedCredential;
  int identitiesMade = 0;

  bool StopTunnel() {
    log.push_back("stop_tunnel");
    if (refusesStop) return false;
    tunnelAccount.clear();
    providerAccount.clear();
    return true;
  }
  // How the daemon answers logout.
  enum class LogoutAnswer { Done, UnknownVerb, OtherUsersSession, Refused };
  LogoutAnswer Logout() {
    log.push_back("logout");
    if (predatesLogout) return LogoutAnswer::UnknownVerb;
    if (otherUsersSession) return LogoutAnswer::OtherUsersSession;
    if (refusesStop || busy) return LogoutAnswer::Refused;
    tunnelAccount.clear();
    providerAccount.clear();
    identity.clear();
    storedCredential.clear();
    return LogoutAnswer::Done;
  }
  // set_provide with a mode that does not provide (the reconcile's Stop step).
  void StopProvider() {
    log.push_back("set_provide never");
    providerAccount.clear();
  }
  void StartProvider(const std::string& account) {
    log.push_back("start_provider " + account);
    if (!tunnelAccount.empty() || otherUsersSession) return;
    BuildDevice(account);
    providerAccount = account;
  }
  void StartTunnel(const std::string& account) {
    log.push_back("start_tunnel " + account);
    providerAccount.clear();
    BuildDevice(account);
    tunnelAccount = account;
  }
  // TunnelHost::NewDeviceLocked: the stored identity, or a new one when there
  // is none; the device stores its account's credential when it starts.
  void BuildDevice(const std::string& account) {
    if (identity.empty()) identity = "identity-" + std::to_string(++identitiesMade);
    storedCredential = account;
  }
  // The process ends, by a crash, an update or a reboot, and starts again.
  void Restart() {
    tunnelAccount.clear();
    providerAccount.clear();
  }
  // What `status` says to this user's reconcile (ctl::ProviderFactsFrom).
  provide::DaemonProviderFacts Facts() const {
    provide::DaemonProviderFacts facts;
    facts.answered = true;
    facts.redacted = otherUsersSession;
    facts.tunnelSession = !tunnelAccount.empty();
    facts.killSwitchArmed = false;
    facts.providerRunning = !providerAccount.empty();
    facts.ownerConnected = true;
    return facts;
  }
  bool Runs() const { return !tunnelAccount.empty() || !providerAccount.empty(); }
  std::vector<std::string> Since(std::size_t at) const {
    return std::vector<std::string>(log.begin() + static_cast<std::ptrdiff_t>(at), log.end());
  }
};

// The app, reduced to what a sign-out meets, as SdkHost runs it.
class App {
 public:
  // `markerFile` is the marker on disk, which outlives the app; `account` the
  // credential stored when it starts ("" signed out).
  App(FakeDaemon& daemon, bool& markerFile, std::string account, std::string provideMode)
      : daemon_(daemon),
        obligation_(MarkerOn(markerFile)),
        account_(std::move(account)),
        provideMode_(std::move(provideMode)) {
    obligation_.Load();  // Initialize
  }

  // RegisterNetworkClient's commit: the credential is stored and the window
  // shows Home; the health poll's next reconcile follows.
  void SignIn(std::string account, std::string provideMode) {
    account_ = std::move(account);
    provideMode_ = std::move(provideMode);
    Reconcile();
  }

  // Logout: signed out, the local credentials logged out, the obligation begun.
  Delivery SignOut() {
    account_.clear();
    provideMode_ = "never";
    return obligation_.Begin(Bind());
  }

  // StartTunnelLocked: the owed sign-out first, then a start only while
  // signed in, reachable and with nothing owed.
  void Connect() {
    obligation_.Settle(Bind());
    if (account_.empty() || !daemon_.reachable || obligation_.Owed()) return;
    daemon_.StartTunnel(account_);
  }

  // ReconcileProviderLocked, as the health poll runs it with no session.
  void Reconcile() {
    obligation_.Settle(Bind());
    if (!daemon_.reachable) return;
    const std::string mode = account_.empty() ? "never" : provideMode_;
    switch (provide::DisconnectedProviderStep(mode, daemon_.Facts())) {
      case provide::DisconnectedStep::None:
      case provide::DisconnectedStep::Apply:
        return;
      case provide::DisconnectedStep::Stop:
        daemon_.StopProvider();
        return;
      case provide::DisconnectedStep::Start:
        if (obligation_.Owed()) return;
        daemon_.StartProvider(account_);
        return;
    }
  }

  bool Owed() const { return obligation_.Owed(); }

 private:
  static signout::Marker MarkerOn(bool& markerFile) {
    signout::Marker marker;
    marker.read = [&markerFile] { return markerFile; };
    marker.write = [&markerFile](bool owed) { markerFile = owed; };
    return marker;
  }

  signout::Daemon Bind() {
    signout::Daemon daemon;
    daemon.reach = [this] { return daemon_.reachable; };
    daemon.otherUsersSession = [this]() -> std::optional<bool> {
      if (daemon_.silent) return std::nullopt;
      return daemon_.otherUsersSession;
    };
    daemon.send = [this](Request request) {
      if (daemon_.onRequest) daemon_.onRequest(request);
      switch (request) {
        case Request::StopTunnel: return daemon_.StopTunnel();
        case Request::Logout:
          switch (daemon_.Logout()) {
            case FakeDaemon::LogoutAnswer::Done:
            // ControlClient::LogoutOutcome::Unsupported, which SdkHost counts
            // as done
            case FakeDaemon::LogoutAnswer::UnknownVerb:
            // auth_not_tunnel_owner, which SdkHost counts as done too
            case FakeDaemon::LogoutAnswer::OtherUsersSession:
              return true;
            case FakeDaemon::LogoutAnswer::Refused:
              return false;
          }
          return false;
      }
      return false;
    };
    return daemon;
  }

  FakeDaemon& daemon_;
  signout::Obligation obligation_;
  std::string account_;
  std::string provideMode_;
};

std::string Join(const std::vector<std::string>& items) {
  std::string out;
  for (const std::string& item : items) out += (out.empty() ? "" : ", ") + item;
  return "[" + out + "]";
}

bool AnyStartFor(const std::vector<std::string>& log, const std::string& account) {
  for (const std::string& line : log) {
    if (line == "start_provider " + account || line == "start_tunnel " + account) return true;
  }
  return false;
}

const std::vector<std::string> kSignOutRequests{"stop_tunnel", "logout"};

}  // namespace

// Quit's request: stop_tunnel, which ends the session and the provider-only
// device, connected or only providing.
UR_TEST(SignOut_StopsAsQuit) {
  for (const bool connected : {true, false}) {
    FakeDaemon daemon;
    bool marker = false;
    App app(daemon, marker, "", "never");
    app.SignIn("a", "always");
    if (connected) app.Connect();
    UR_EXPECT_TRUE(daemon.Runs());
    const std::size_t at = daemon.log.size();
    const Delivery delivery = app.SignOut();
    UR_EXPECT_TRUE_MSG("delivered to a daemon that can be reached", delivery == Delivery::Delivered);
    UR_EXPECT_TRUE_MSG("Quit's stop_tunnel, then logout, got " + Join(daemon.Since(at)),
                       daemon.Since(at) == kSignOutRequests);
    UR_EXPECT_TRUE_MSG("nothing of a's runs", !daemon.Runs());
    UR_EXPECT_TRUE_MSG("nothing is owed once delivered", !marker && !app.Owed());
  }
}

// A daemon that missed the sign-out is told before anything else when it can
// be reached again, and nothing of the account starts after it.
UR_TEST(SignOut_AMissedDaemonIsToldWhenItComesBack) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  daemon.reachable = false;
  const std::size_t signedOut = daemon.log.size();
  UR_EXPECT_TRUE_MSG("unreachable: nothing could be sent",
                     app.SignOut() == Delivery::Unreachable);
  UR_EXPECT_TRUE_MSG("unreachable: the sign-out is owed, in the marker", marker && app.Owed());
  UR_EXPECT_TRUE_MSG("unreachable: (the fake) the daemon still provides for a",
                     daemon.providerAccount == "a");
  daemon.reachable = true;
  const std::size_t at = daemon.log.size();
  app.Reconcile();  // the health poll
  UR_EXPECT_TRUE_MSG("comes back: the first request delivers the sign-out, got " +
                         Join(daemon.Since(at)),
                     daemon.Since(at) == kSignOutRequests);
  UR_EXPECT_TRUE_MSG("comes back: a's provider is stopped", !daemon.Runs());
  UR_EXPECT_TRUE_MSG("comes back: nothing of a's starts", !AnyStartFor(daemon.Since(signedOut), "a"));
  UR_EXPECT_TRUE_MSG("comes back: nothing is owed", !marker && !app.Owed());
}

// A restart, then a relaunch signed out: the marker carries the obligation,
// and the first reconcile delivers it.
UR_TEST(SignOut_ARestartAndARelaunchDoNotResume) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "network");
  app.Connect();
  const std::size_t signedOut = daemon.log.size();
  daemon.reachable = false;
  app.SignOut();
  daemon.Restart();
  UR_EXPECT_TRUE_MSG("restart: the daemon starts nothing by itself", !daemon.Runs());
  App relaunched(daemon, marker, "", "never");
  UR_EXPECT_TRUE_MSG("relaunch: the owed sign-out survives the app", relaunched.Owed());
  relaunched.Reconcile();
  daemon.reachable = true;
  const std::size_t at = daemon.log.size();
  relaunched.Reconcile();
  UR_EXPECT_TRUE_MSG("relaunch: the first reachable reconcile delivers it, got " +
                         Join(daemon.Since(at)),
                     daemon.Since(at) == kSignOutRequests);
  UR_EXPECT_TRUE_MSG("restart and relaunch: nothing of a's runs or starts after the sign-out",
                     !daemon.Runs() && !AnyStartFor(daemon.Since(signedOut), "a"));
  UR_EXPECT_TRUE_MSG("relaunch: nothing is owed once delivered", !marker);
}

// b signs in before the daemon can be told: nothing of b's starts until a's
// sign-out is delivered, and then b's reconcile and b's connect.
UR_TEST(SignOut_OwedHoldsTheNextSignIn) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  daemon.reachable = false;
  app.SignOut();
  daemon.Restart();
  App rebooted(daemon, marker, "", "never");
  rebooted.SignIn("b", "always");
  rebooted.Connect();  // the post-login connect
  UR_EXPECT_TRUE_MSG("reboot: b starts nothing while a's sign-out is owed",
                     rebooted.Owed() && !AnyStartFor(daemon.log, "b"));
  daemon.reachable = true;
  const std::size_t at = daemon.log.size();
  rebooted.Reconcile();
  const std::vector<std::string> want{"stop_tunnel", "logout", "start_provider b"};
  UR_EXPECT_TRUE_MSG("a's sign-out is delivered before b's provider starts, got " +
                         Join(daemon.Since(at)),
                     daemon.Since(at) == want);
  UR_EXPECT_TRUE_MSG("b provides", daemon.providerAccount == "b");
}

// polkit refused the stop: the sign-out stays owed and holds b's starts until
// the daemon does it.
UR_TEST(SignOut_ARefusalStaysOwed) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  daemon.refusesStop = true;
  UR_EXPECT_TRUE_MSG("refused: the daemon did not do it", app.SignOut() == Delivery::Refused);
  UR_EXPECT_TRUE_MSG("refused: the sign-out stays owed", marker && app.Owed());
  app.SignIn("b", "always");
  app.Connect();
  UR_EXPECT_TRUE_MSG("refused: b does not start beside a's provider",
                     !AnyStartFor(daemon.log, "b") && daemon.providerAccount == "a");
  daemon.refusesStop = false;
  const std::size_t at = daemon.log.size();
  app.Reconcile();
  const std::vector<std::string> want{"stop_tunnel", "logout", "start_provider b"};
  UR_EXPECT_TRUE_MSG("allowed: the sign-out first, then b, got " + Join(daemon.Since(at)),
                     daemon.Since(at) == want);
}

// A daemon that does not answer `status` cannot say whose session it runs:
// nothing is sent, and the sign-out stays owed.
UR_TEST(SignOut_NoStatusStaysOwed) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  daemon.silent = true;
  const std::size_t at = daemon.log.size();
  UR_EXPECT_TRUE_MSG("no status: refused", app.SignOut() == Delivery::Refused);
  UR_EXPECT_TRUE_MSG("no status: nothing sent and still owed", daemon.log.size() == at && marker);
}

// Another user's session is theirs: the delivery leaves it alone (stopping it
// would ask for an administrator) and has nothing of this user's to stop.
UR_TEST(SignOut_LeavesAnotherUsersSession) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "a", "never");
  daemon.otherUsersSession = true;
  daemon.tunnelAccount = "someone else";
  daemon.identity = "identity of someone else";
  daemon.storedCredential = "someone else";
  const std::size_t at = daemon.log.size();
  UR_EXPECT_TRUE_MSG("another user's session: delivered", app.SignOut() == Delivery::Delivered);
  UR_EXPECT_TRUE_MSG("another user's session: nothing sent", daemon.log.size() == at);
  UR_EXPECT_TRUE_MSG("another user's session: it still runs",
                     daemon.tunnelAccount == "someone else");
  UR_EXPECT_TRUE_MSG("another user's session: its identity and credential are kept",
                     daemon.identity == "identity of someone else" &&
                         daemon.storedCredential == "someone else");
  UR_EXPECT_TRUE_MSG("another user's session: nothing owed", !marker);
}

// Each network starts fresh: after a's sign-out the daemon keeps neither a's
// identity nor a's stored credential, and b, signing in next, runs on a new
// identity, connected or only providing.
UR_TEST(SignOut_TheNextNetworkStartsOnANewIdentity) {
  for (const bool connected : {true, false}) {
    FakeDaemon daemon;
    bool marker = false;
    App app(daemon, marker, "", "never");
    app.SignIn("a", "always");
    if (connected) app.Connect();
    const std::string identityOfA = daemon.identity;
    UR_EXPECT_TRUE_MSG("a runs on an identity", !identityOfA.empty() &&
                                                    daemon.storedCredential == "a");
    UR_EXPECT_TRUE_MSG("delivered", app.SignOut() == Delivery::Delivered);
    UR_EXPECT_TRUE_MSG("signed out: the daemon keeps no identity and no credential of a's",
                       daemon.identity.empty() && daemon.storedCredential.empty());
    app.SignIn("b", "always");
    if (connected) app.Connect();
    UR_EXPECT_TRUE_MSG("b runs", daemon.Runs() && daemon.storedCredential == "b");
    UR_EXPECT_TRUE_MSG("b runs on a new identity, not a's: " + daemon.identity,
                       !daemon.identity.empty() && daemon.identity != identityOfA);
  }
}

// Another user's session that starts between this user's status read and its
// logout is refused by the daemon, not cleared, and that refusal delivers the
// sign-out as a redacted status would have: nothing of this user's is left to
// clear, and their session, identity and credential stand.
UR_TEST(SignOut_ALogoutRacingAnotherUsersStartClearsNothingOfIt) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  daemon.onRequest = [&daemon](Request request) {
    if (request != Request::Logout || daemon.otherUsersSession) return;
    // another user connects: their device, on the daemon's identity
    daemon.otherUsersSession = true;
    daemon.tunnelAccount = "someone else";
    daemon.BuildDevice("someone else");
  };
  const std::size_t at = daemon.log.size();
  UR_EXPECT_TRUE_MSG("delivered beside the new session", app.SignOut() == Delivery::Delivered);
  UR_EXPECT_TRUE_MSG("stop_tunnel then the refused logout, got " + Join(daemon.Since(at)),
                     daemon.Since(at) == kSignOutRequests);
  const std::string theirIdentity = daemon.identity;
  UR_EXPECT_TRUE_MSG("their session, identity and credential stand",
                     daemon.tunnelAccount == "someone else" && !theirIdentity.empty() &&
                         daemon.storedCredential == "someone else");
  UR_EXPECT_TRUE_MSG("nothing owed", !marker && !app.Owed());
  const std::size_t after = daemon.log.size();
  app.Reconcile();
  UR_EXPECT_TRUE_MSG("nothing more is sent beside it, got " + Join(daemon.Since(after)),
                     daemon.Since(after).empty());
  UR_EXPECT_TRUE_MSG("their identity and credential are still theirs",
                     daemon.identity == theirIdentity &&
                         daemon.storedCredential == "someone else" &&
                         daemon.tunnelAccount == "someone else");
}

// A bring-up that owns the session refuses the logout: the sign-out stays
// owed, b starts nothing, and once the daemon can do it b starts on a new
// identity.
UR_TEST(SignOut_ABusyDaemonKeepsTheLogoutOwed) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  const std::string identityOfA = daemon.identity;
  daemon.busy = true;
  UR_EXPECT_TRUE_MSG("busy: refused", app.SignOut() == Delivery::Refused);
  UR_EXPECT_TRUE_MSG("busy: owed", marker && app.Owed());
  app.SignIn("b", "always");
  app.Connect();
  UR_EXPECT_TRUE_MSG("busy: b starts nothing", !AnyStartFor(daemon.log, "b"));
  daemon.busy = false;
  const std::size_t at = daemon.log.size();
  app.Reconcile();
  const std::vector<std::string> want{"stop_tunnel", "logout", "start_provider b"};
  UR_EXPECT_TRUE_MSG("the logout, then b, got " + Join(daemon.Since(at)), daemon.Since(at) == want);
  UR_EXPECT_TRUE_MSG("b provides on a new identity",
                     daemon.providerAccount == "b" && daemon.identity != identityOfA);
}

// A daemon that predates logout answers `unknown verb`, which counts as done:
// it has nothing to clear the identity with, and keeping the sign-out owed to
// it would hold every start.
UR_TEST(SignOut_ADaemonWithoutLogoutStillDelivers) {
  FakeDaemon daemon;
  daemon.predatesLogout = true;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  const std::size_t at = daemon.log.size();
  UR_EXPECT_TRUE_MSG("an older daemon: delivered", app.SignOut() == Delivery::Delivered);
  UR_EXPECT_TRUE_MSG("an older daemon: both requests sent, got " + Join(daemon.Since(at)),
                     daemon.Since(at) == kSignOutRequests);
  UR_EXPECT_TRUE_MSG("an older daemon: nothing owed, nothing runs", !marker && !daemon.Runs());
  app.SignIn("b", "always");
  UR_EXPECT_TRUE_MSG("an older daemon: b provides", daemon.providerAccount == "b");
}

// The next sign-in's reconcile starts what a fresh launch's does, for every
// provide mode, and no tunnel without a connect.
UR_TEST(SignOut_TheNextSignInReconcilesAsAFreshLaunch) {
  for (const char* mode : {"never", "always", "network", "auto"}) {
    FakeDaemon daemon;
    bool marker = false;
    App app(daemon, marker, "", "never");
    app.SignIn("a", "always");
    app.Connect();
    app.SignOut();
    const std::size_t at = daemon.log.size();
    app.SignIn("b", mode);
    const std::vector<std::string> afterSignOut = daemon.Since(at);
    FakeDaemon fresh;
    bool freshMarker = false;
    App launched(fresh, freshMarker, "b", mode);
    launched.Reconcile();
    UR_EXPECT_TRUE_MSG(std::string(mode) + ": what a fresh launch starts, got " +
                           Join(afterSignOut) + ", fresh " + Join(fresh.log),
                       afterSignOut == fresh.log);
    const bool provides = provide::ProviderRuns(provide::ControlModeFrom(mode), false);
    UR_EXPECT_TRUE_MSG(std::string(mode) + ": the provider starts as the mode says",
                       AnyStartFor(afterSignOut, "b") == provides);
    for (const std::string& line : afterSignOut) {
      UR_EXPECT_TRUE_MSG(std::string(mode) + ": no tunnel without a connect",
                         line.rfind("start_tunnel", 0) != 0);
    }
  }
}

// The marker is written before the request: an app ended mid-delivery (a
// polkit prompt left open) still owes it.
UR_TEST(SignOut_MarksBeforeTheRequest) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "", "never");
  app.SignIn("a", "always");
  bool markedAtFirst = false;
  bool first = true;
  daemon.onRequest = [&](Request) {
    if (first) markedAtFirst = marker;
    first = false;
  };
  app.SignOut();
  UR_EXPECT_TRUE_MSG("the marker is written before the first request", !first && markedAtFirst);
}

// With nothing owed a reconcile or a connect sends no sign-out request: a
// stop there would end a signed-in user's own tunnel.
UR_TEST(SignOut_NothingOwedSendsNothing) {
  FakeDaemon daemon;
  bool marker = false;
  App app(daemon, marker, "a", "always");
  app.Reconcile();
  app.Connect();
  for (const std::string& line : daemon.log) {
    UR_EXPECT_TRUE_MSG("nothing owed: no stop_tunnel or logout, got " + line,
                       line != "stop_tunnel" && line != "logout");
  }
  UR_EXPECT_TRUE_MSG("nothing owed: a's tunnel runs", daemon.tunnelAccount == "a");
}

UR_TEST(SignOut_Names) {
  UR_EXPECT_TRUE(std::string(signout::ToString(Request::StopTunnel)) == "stop_tunnel");
  UR_EXPECT_TRUE(std::string(signout::ToString(Request::Logout)) == "logout");
  for (const Delivery delivery : {Delivery::Delivered, Delivery::Unreachable, Delivery::Refused}) {
    UR_EXPECT_TRUE(std::string(signout::ToString(delivery)) != "unknown");
  }
}
