// The in-app service manager — the Linux twin of
// windows:app/src/App/ServiceSetup.{h,cpp} plus the release half of
// windows:app/src/App/UpdateChecker.{h,cpp}, fused because on Linux they are
// one story: the thing that is missing, stopped or out of date IS the thing
// GitHub Releases publishes.
//
// WHY THIS EXISTS. The Flatpak and the AppImage ship the GUI ONLY: a sandbox
// has no /dev/net/tun, no CAP_NET_ADMIN and no way to install a system unit
// (packaging/flatpak/network.ur.urnetwork.yml says why at length). The daemon
// therefore arrives separately, and the app is where the user finds out it is
// missing. Windows solved the same shape with a first-run banner over one
// elevated, idempotent verb; this is that, with GitHub Releases as the source
// of truth for "is the installed service up to date".
//
// ── THE THREE RULES THIS FILE IS BUILT AROUND ───────────────────────────────
//
// 1. NEVER CLAIM MORE THAN WAS OBSERVED. Every state names what it was derived
//    from (Observation::derivation) and how sure that is (Confidence). A
//    denied control socket does NOT become "not installed" — it is a group
//    membership problem with a different fix, and the app already renders it.
//    A wrong "update your service" banner teaches the user to ignore the
//    banner that will one day be right.
//
// 2. DOWNLOAD AND VERIFY UNPRIVILEGED. The asset is fetched as the app user
//    into $XDG_CACHE_HOME, its sha256 checked against the digest GitHub
//    stamped on that same asset object in the same releases JSON the check
//    parsed, and only an ALREADY-VERIFIED path is handed to the elevated step.
//    Nothing is ever piped into root. The elevated step is dumb: run the
//    installer that shipped inside the verified tarball.
//
// 3. EVERY FAILURE IS A DISTINCT, ACTIONABLE STATE. There is no spinner that
//    never settles: every network read has a timeout, every child process a
//    deadline, and every terminal state a sentence a user can act on. A
//    dismissed polkit dialog is NOT a failure — it is Phase::Cancelled, and it
//    is silent.
//
// ── WHAT EACH STATE IS DERIVED FROM, PER ENVIRONMENT ────────────────────────
// The environment is detected, never guessed: /.flatpak-info exists iff we are
// inside a Flatpak sandbox (Env()).
//
// NATIVE (AppImage, .deb, tarball install):
//   * control socket  — stat() of ControlClient::SocketPath(), plus the
//                       session state the window's ControlClient already holds
//                       (hello ok / EACCES / unreachable).
//   * systemd         — `systemctl show urnetworkd.service -p LoadState
//                       -p ActiveState -p UnitFileState`, read-only and
//                       unprivileged. LoadState=not-found is the ONLY positive
//                       evidence of absence this app can obtain, and it is why
//                       NotInstalled is Observed here and Inferred in a
//                       sandbox.
//   * version         — the hello handshake's daemon_version.
//
// FLATPAK (systemctl is NOT in the runtime, and /usr is the runtime's, not the
// host's — no .installed-version marker is visible):
//   * control socket  — the same stat()+session evidence, over the
//                       --filesystem=/run/urnetwork:ro bind mount.
//   * systemd         — only via `flatpak-spawn --host systemctl …`, which
//                       needs --talk-name=org.freedesktop.Flatpak. When that
//                       is present the sandbox gets the SAME systemd truth as
//                       a native run. When it is not, systemd is simply not
//                       consulted (systemdConsulted=false) and:
//                       hello ok            ⇒ Running   (Observed)
//                       EACCES              ⇒ Unknown + permissionDenied
//                                             (Observed: the socket is there
//                                             and we may not use it — a
//                                             usermod problem, not a setup
//                                             problem; the card stays hidden)
//                       socket unreachable  ⇒ NotInstalled (INFERRED). It may
//                                             really be installed-but-stopped:
//                                             systemd removes the unit's
//                                             RuntimeDirectory on stop, so the
//                                             socket AND its directory vanish
//                                             together and the two cases are
//                                             indistinguishable from inside
//                                             the sandbox. Inferring costs
//                                             nothing because the one offered
//                                             action is idempotent — the same
//                                             installer sets up, repairs,
//                                             starts and upgrades (see
//                                             packaging/tarball/install.sh) —
//                                             exactly as Windows' three
//                                             wordings drive one install verb.
//   * version         — the hello handshake's daemon_version, same as native.
//
// VersionMismatch is claimed from EITHER of two observations, never from a
// guess: (a) the control handshake reported protocol/SDK skew, which is the
// daemon telling us itself; or (b) the daemon's version AND the channel's
// release version both parse as release codes and the release is NEWER. A
// daemon whose version does not parse (a dev build, "0.0.0") is never nagged.
//
// ── WHAT THIS FILE CANNOT DO ALONE ──────────────────────────────────────────
// TODO(build): add src/ServiceSetup.cpp and src/ServiceNotice.cpp to the
//   `urnetwork-gui` sources list in app/meson.build. No new dependency: gio,
//   glib and nlohmann_json are already in gui_deps.
// TODO(flatpak-manifest): packaging/flatpak/network.ur.urnetwork.yml needs
//   `--talk-name=org.freedesktop.Flatpak` and `--socket=session-bus` for
//   RunElevatedVerb's `flatpak-spawn --host` to work at all. Flathub's linter
//   FLAGS that permission and an exception needs a justification PR, so the
//   option must carry its justification in a comment beside it. Until it
//   lands, a sandboxed build reports Failure::SpawnUnavailable and shows the
//   paste-able command instead — which is the designed fallback, not a bug.
// TODO(ci): the daemon release job must publish
//   `urnetwork-daemon-<version>-<arch>.install.tar.gz` (the name
//   packaging/make-install-tarball.sh already writes) as a PRERELEASE tagged
//   `v<version>`, with NO SHA256SUMS asset — verification rides the per-asset
//   sha256 digest GitHub computes at upload and serves in the same releases
//   JSON FetchRelease parses.
// TODO(settings): the channel rows (a tag field, the beta line, the greyed-out
//   upstream option) call LoadChannel/SaveChannel/ChannelAvailable/
//   ChannelUnavailableReason. Nothing else here needs a Settings page.
// TODO(store): every `svc_*` id in ServiceNotice.cpp is absent from
//   localizations/keys/*.yaml, the same way the Windows twin's ids are marked
//   Adv(). The English written there is the source text those keys must carry.
//
// ── THREADING ───────────────────────────────────────────────────────────────
// One worker at a time. Every socket read, every g_spawn and every sha256 runs
// on it; the handler is invoked on the GTK main loop through urnw::PostToMain
// and is EPOCH-GUARDED — Cancel(), a new action and Shutdown() each bump the
// epoch, so a post from an abandoned run is dropped instead of overwriting the
// snapshot of the run that replaced it. DaemonFacts are pulled through a
// provider callback the window installs, so this class never owns a second
// ControlClient and never opens a second control connection.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace urnw {

class ServiceSetup {
 public:
  // ---- what the machine looks like -----------------------------------------

  // The five states the owner asked for. Deliberately NOT one per failure:
  // this enum answers "what is wrong with the service", and the install/update
  // machinery answers "what went wrong while fixing it" (Failure, below).
  enum class State {
    Unknown,          // no evidence, or evidence that is not ours to act on
    NotInstalled,     // nothing serves the control socket and nothing is registered
    Stopped,          // registered with systemd, not active
    Running,          // hello completed; the daemon is serving
    VersionMismatch,  // running, but older than what the channel publishes
  };

  // How sure the state is. Inferred exists so the UI can be honest about the
  // sandbox case above without pretending it is not actionable.
  enum class Confidence { Observed, Inferred };

  enum class Environment { Native, Flatpak };

  // What the app ALREADY knows from the one ControlClient the window owns.
  // Deliberately a plain POD rather than ControlClient's enums: this header
  // stays free of ControlProtocol.hpp, and the window's adapter is one switch
  // (see the wiring notes).
  struct DaemonFacts {
    bool valid = false;              // false: the caller had nothing to say yet
    bool helloOk = false;            // EnsureSession() == DaemonSessionState::Ok
    bool permissionDenied = false;   // DaemonUnreachableReason::PermissionDenied
    bool staleSandboxMount = false;  // DaemonUnreachableReason::StaleSandboxMount
    // DaemonTooOld / ClientTooOld / SdkMismatch — the daemon answered and the
    // two halves disagree. This is the daemon telling us it is the wrong
    // version, which is stronger evidence than any release comparison.
    bool protocolSkew = false;
    std::string daemonVersion;  // hello's daemon_version; "" when unknown
    std::string socketPath;     // ControlClient::SocketPath()
  };

  // The read-only systemd query. All three fields are empty when systemd was
  // not consulted at all (see consulted).
  struct SystemdFacts {
    bool consulted = false;    // a systemctl actually ran and answered
    bool viaHostSpawn = false; // it ran through flatpak-spawn --host
    std::string loadState;     // "loaded" | "not-found" | "masked" | …
    std::string activeState;   // "active" | "inactive" | "failed" | "activating" | …
    std::string unitFileState; // "enabled" | "disabled" | "masked" | …
  };

  struct Observation {
    State state = State::Unknown;
    Confidence confidence = Confidence::Observed;
    Environment environment = Environment::Native;
    // Release-grammar strings, kept as evidence for the mismatch wording. Data,
    // never translated.
    std::string installedVersion;  // hello's daemon_version
    std::string targetVersion;     // the channel's newest (or pinned) release
    // One English sentence naming what this was read from — the log line, the
    // developer screen and the card's tooltip all use it. Never a claim beyond
    // the evidence.
    std::string derivation;
    // The socket is there and we are not allowed to use it: the urnetwork
    // group. NOT a setup problem, so the card stays hidden and the app's
    // existing remediation keeps the floor.
    bool permissionDenied = false;
    bool socketPresent = false;   // stat() of the socket path succeeded
    bool systemdConsulted = false;
  };

  // ---- the release channel (Settings) --------------------------------------

  enum class Channel {
    BetaLatest,    // newest PRERELEASE on the fork's beta line — the default
    PinnedTag,     // an exact tag the user typed
    UpstreamMain,  // newest full release from the upstream repo
  };

  struct ChannelConfig {
    Channel channel = Channel::BetaLatest;
    std::string repo;  // "" ⇒ the channel's default repo
    std::string tag;   // PinnedTag only; with or without the leading 'v'
  };

  // "Ryanmello07/urnetwork-linux" — the fork whose CI publishes the daemon
  // assets this app installs.
  static const char* DefaultBetaRepo();
  // "urnetwork/urnetwork-linux" — upstream. Has no build yet, which is why
  // ChannelAvailable(UpstreamMain) is false and Settings greys the option out
  // rather than hiding it (a hidden option cannot explain itself).
  static const char* UpstreamRepo();
  // False for UpstreamMain today. Settings MUST consult this instead of
  // hard-coding the grey-out, so the day upstream publishes, one constant
  // flips and the option lights up.
  static bool ChannelAvailable(Channel);
  // Why it is unavailable, for the row's disabled subtitle. "" when available.
  static std::string ChannelUnavailableReason(Channel);
  static const char* ChannelRepo(const ChannelConfig&);

  // $XDG_CONFIG_HOME/urnetwork/app_prefs.json, beside advanced_mode:
  //   "service_channel"     "beta" | "tag" | "upstream"
  //   "service_channel_repo" string, optional override
  //   "service_channel_tag" string, PinnedTag only
  static ChannelConfig LoadChannel();
  static void SaveChannel(const ChannelConfig&);

  // What one release offers us. Everything the apply needs is captured at
  // CHECK time — url and digest read out of the SAME asset object in one
  // visit — so a repo that changes mid-flight cannot pair this hash with a
  // different download.
  struct Release {
    std::string tag;       // as minted, with the 'v'
    std::string version;   // v-less
    std::uint64_t code = 0;  // the beta counter; 0 ⇒ not release grammar
    bool prerelease = false;
    std::string assetName;   // urnetwork-daemon-<version>-<arch>.install.tar.gz
    std::string assetUrl;    // browser_download_url
    std::string digestHex;   // lowercase sha256 hex; empty ⇒ unverifiable
    std::int64_t assetSize = 0;
    bool valid() const { return !assetUrl.empty() && digestHex.size() == 64; }
  };

  // ---- what the one button is doing ----------------------------------------

  enum class Phase {
    Idle,       // nothing in flight
    Working,    // `stage` says how far it has got
    Cancelled,  // the polkit dialog was dismissed, or the user pressed Cancel.
                // A NORMAL outcome: rendered as one calm line, never an error.
    Failed,     // `failure` says exactly where and what to do about it
    Succeeded,  // the installer exited 0 and the daemon answered again
  };

  enum class Stage {
    Idle,
    CheckingRelease,  // GET /repos/<repo>/releases…
    Downloading,      // the asset → $XDG_CACHE_HOME (progress is meaningful)
    Verifying,        // sha256 vs the release JSON's digest
    Extracting,       // tar xz into a fresh 0700 staging dir
    Elevating,        // ONE polkit prompt; the installer runs as root
    Confirming,       // waiting for the control socket to answer again
  };

  // One enumerator per thing that can go wrong, because one sentence per thing
  // that can go wrong is the whole point.
  enum class Failure {
    None,
    UnsupportedArch,     // uname is neither x86_64 nor aarch64
    NoNetwork,           // DNS/connect/TLS never got a socket
    TlsUnavailable,      // glib-networking missing: https cannot be spoken
    ReleaseUnavailable,  // HTTP error, unparseable JSON, or no release at all
    TagNotFound,         // PinnedTag: that tag has no release
    RateLimited,         // GitHub said 403/429 — retry later, unauthenticated
    AssetMissing,        // the release carries no asset for this architecture
    DigestMissing,       // the asset carries no usable sha256: unverifiable
    DownloadFailed,      // the transfer did not finish
    ChecksumMismatch,    // it finished and the bytes were wrong: discarded
    CacheUnwritable,     // $XDG_CACHE_HOME could not be created or written
    TarMissing,          // no tar to unpack with (and no host spawn to borrow)
    ExtractFailed,       // tar refused, or the archive is not the shipped shape
    PolkitUnavailable,   // pkexec is not installed on the host
    SpawnUnavailable,    // sandbox: flatpak-spawn absent or the portal refused
    ElevatedFailed,      // the installer ran and exited non-zero
    ElevatedTimeout,     // it never finished inside the budget
    DaemonDidNotAppear,  // it exited 0 and the socket still does not answer
                         // (almost always the urnetwork group + re-login)
  };

  struct Snapshot {
    Observation observation{};
    bool busy = false;  // an action is in flight; the button is disabled
    Phase phase = Phase::Idle;
    Stage stage = Stage::Idle;
    Failure failure = Failure::None;
    // Never user-facing prose on its own — the log line, and the parenthetical
    // the card appends after the translated sentence (curl-style detail).
    std::string failureDetail;
    // 0..1 while Downloading, -1 when the length is unknown or another stage
    // is running. A determinate bar or an indeterminate one, never a lie.
    double progress = -1.0;
    // The release the button would install ("" until a check succeeded).
    std::string offeredVersion;
    std::string offeredTag;
    // The verified, extracted installer, once it exists. This is the path the
    // elevated step is handed, and the path the paste-able fallback names.
    std::string stagedInstaller;
    // THE NO-PERMISSION FALLBACK. Always populated, always safe to show: the
    // exact one-line command that does what the button would have done, for a
    // host with no polkit agent, a Flathub build without
    // --talk-name=org.freedesktop.Flatpak, or a user who would rather see it.
    // Verified-and-staged ⇒ `pkexec '<staged>/install.sh'`; not yet staged ⇒ a
    // download-verify-install line that checks the SAME digest this app would.
    std::string fallbackCommand;
    // Bumped on every published snapshot, so a view can cheaply tell "new" from
    // "same thing again".
    std::uint64_t revision = 0;
  };

  // ---- the process-wide instance -------------------------------------------
  // One per app: the Connect card, the Settings rows and the developer screen
  // are three views of ONE state machine, and two of them racing two polkit
  // prompts is exactly the bug the busy flag exists to prevent.
  static ServiceSetup& Instance();

  ServiceSetup();
  ~ServiceSetup();
  ServiceSetup(const ServiceSetup&) = delete;
  ServiceSetup& operator=(const ServiceSetup&) = delete;

  // Called on the WORKER thread, so it may block: hand it the window's
  // ControlClient (which is documented thread-safe). Without a provider the
  // classifier falls back to filesystem evidence alone and says so.
  using FactsProvider = std::function<DaemonFacts()>;
  void SetDaemonFactsProvider(FactsProvider provider);

  // Store only — never invoked from inside AddHandler. Bind, then replay
  // Current() yourself: a card can be built long after the first probe ran.
  // Invoked on the GTK main loop, epoch-guarded.
  //
  // A LIST, not a single writer: the Connect card, the Settings channel row
  // and the developer screen are three views of ONE state machine, and making
  // them fight over one slot would mean whichever bound last silently blanked
  // the others. Every handler gets a token and MUST remove it before whatever
  // it captures dies.
  using Handler = std::function<void(const Snapshot&)>;
  std::uint64_t AddHandler(Handler handler);
  void RemoveHandler(std::uint64_t token);

  Snapshot Current() const;

  // Re-classify now. `withReleaseCheck` also refreshes the channel's release
  // (throttled to kReleaseCheckThrottleMs so window-activation refreshes cannot
  // hammer the API). Returns immediately; the work is on the worker.
  void RefreshNow(bool withReleaseCheck = true);

  // THE ONE BUTTON. Check → download → verify → extract → ONE polkit prompt →
  // confirm. Idempotent by construction: the installer inside the tarball sets
  // up, repairs, starts and upgrades, so Set up / Start / Update are three
  // wordings of this one action, exactly as on Windows. Ignored while busy.
  void BeginInstall();

  // Abort whatever is in flight. Bumps the epoch, cancels the in-flight socket
  // read and SIGTERMs a running child (including an open polkit dialog), so the
  // UI settles within ~100 ms rather than at the next timeout.
  void Cancel();

  // App teardown: cancel, then join the worker with a budget. Safe to call
  // twice. The destructor calls it.
  void Shutdown();

  // ---- pure / blocking pieces, usable from any thread and from tests -------

  static Environment Env();  // /.flatpak-info
  // Inside a sandbox: is `flatpak-spawn` on PATH? (Its PRESENCE is not proof
  // the portal will allow the call — that is only learned by trying, which is
  // why SpawnUnavailable is a run-time failure and not a precondition.)
  static bool HostSpawnAvailable();
  // "amd64" | "arm64" | "" (unsupported).
  static std::string Arch();

  // The one classification. PURE over its inputs — no I/O — so the derivation
  // sentence is testable and the same function serves every environment.
  // `target` may be null (no release check has succeeded yet).
  static Observation Classify(const DaemonFacts& daemon, const SystemdFacts& systemd,
                              bool socketPresent, Environment env, const Release* target);

  // Read-only systemctl. Returns consulted=false when systemd cannot be asked
  // at all (no systemctl, no /run/systemd/system, or the sandbox has no host
  // spawn) — which is a fact about the ENVIRONMENT, never about the service.
  static SystemdFacts ProbeSystemd(int timeoutMs);

  // GET the releases JSON and pick per the channel. Fills `out` on success;
  // on failure sets `failure` and `detail` and returns false. Blocking.
  static bool FetchRelease(const ChannelConfig& config, Release* out, Failure* failure,
                           std::string* detail, const std::atomic<bool>* cancel);

  // The beta counter out of a tag: "v2026.8.16-104123450-beta" → 104123450.
  // 0 when the tag is not release grammar (a dev build, a hand-made tag). The
  // Windows twin is Common/VersionGrammar.h ParseReleaseCode — same grammar,
  // because the same CI mints both.
  static std::uint64_t ParseReleaseCode(const std::string& tag);
  // The lowercase sha256 hex out of a GitHub asset `digest` field, or "" unless
  // the value is exactly `sha256:<64 hex>`. Strict: empty means "this release
  // cannot be verified, skip it", never best-effort.
  static std::string DigestHexFromAssetDigest(const std::string& digest);
  // urnetwork-daemon-<version>-<arch>.install.tar.gz — the name
  // packaging/make-install-tarball.sh writes. ASSET NAMES ARE A CONTRACT.
  static std::string AssetNameFor(const std::string& version, const std::string& arch);

  // What one elevated run did. `cancelled` is the polkit dialog being
  // dismissed (pkexec exits 126, or 127 when it could not even start the
  // program) — a NORMAL outcome, reported silently.
  struct ElevatedResult {
    bool spawned = false;
    bool exited = false;
    bool cancelled = false;
    bool timedOut = false;
    int exitCode = 1;
    std::string stderrTail;  // last few KiB, for the log only
  };
  // Sandbox: `flatpak-spawn --host pkexec <installer>`. Native: `pkexec
  // <installer>`. Detected via Env(), never guessed. `installer` MUST already
  // be a verified, extracted path — this function does not check anything, it
  // only elevates, which is exactly why the verification happens before it.
  static ElevatedResult RunElevatedVerb(const std::string& installer, int timeoutMs,
                                        const std::atomic<bool>* cancel);

  // The paste-able one-liner for a host that cannot (or will not) elevate from
  // the app. Given a staged installer it is `pkexec '<path>'`; given only a
  // release it is a download-verify-install line carrying the same digest.
  static std::string FallbackCommandFor(const std::string& stagedInstaller,
                                        const Release& release);

  // Where downloads and staging live: $XDG_CACHE_HOME/urnetwork/service.
  // NEVER /tmp — a Flatpak's /tmp is private to the sandbox and the host side
  // of the elevated step could not see the file at all.
  static std::string CacheDir();

  // Throttle for release checks driven by window activation.
  static constexpr int kReleaseCheckThrottleMs = 5 * 60 * 1000;
  // The polkit dialog is a human waiting; the install itself stops a unit,
  // writes files and starts it again.
  static constexpr int kElevatedTimeoutMs = 15 * 60 * 1000;
  static constexpr int kSystemdTimeoutMs = 4000;
  static constexpr int kConfirmBudgetMs = 20000;

 private:
  // ONE long-lived worker with a request queue, on the Windows UpdateChecker's
  // WorkerLoop shape — NOT a thread per action. A thread per action forces the
  // GTK thread to join the previous one on the next click, and joining a
  // worker that is mid-download blocks the UI for as long as the socket
  // timeout. Queueing costs nothing and cannot block the main loop at all.
  void Request(bool install, bool withReleaseCheck);
  void WorkerLoop();
  void RunProbe(std::uint64_t epoch, bool withReleaseCheck);
  void RunInstall(std::uint64_t epoch);
  // Copy under the lock, mutate, publish outside it — the handler is never
  // invoked with mutex_ held, and never on this thread.
  void Mutate(std::uint64_t epoch, const std::function<void(Snapshot&)>& fn);
  void Publish(std::uint64_t epoch);
  DaemonFacts PullFacts() const;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  Snapshot snapshot_;
  FactsProvider provider_;
  std::vector<std::pair<std::uint64_t, Handler>> handlers_;
  std::uint64_t nextHandlerToken_ = 1;
  Release offer_;
  std::int64_t lastReleaseCheckMs_ = 0;

  std::thread worker_;
  bool workerStarted_ = false;
  bool stop_ = false;
  bool probeRequested_ = false;
  bool probeWithRelease_ = false;
  bool installRequested_ = false;
  bool inFlight_ = false;       // an action is running on the worker right now
  bool installInFlight_ = false;  // ...and it is the elevated one
  std::atomic<bool> cancel_{false};
  std::atomic<bool> shutdown_{false};
  // THE EPOCH GUARD, deliberately a shared_ptr and not a member atomic: a
  // PostToMain closure from an abandoned run must be able to decide whether to
  // run WITHOUT dereferencing `this`, which may already be gone at static
  // teardown. The closure captures a copy of the cell, never the object.
  std::shared_ptr<std::atomic<std::uint64_t>> epochCell_ =
      std::make_shared<std::atomic<std::uint64_t>>(1);
};

// English, untranslated, for logs and the developer screen. The USER-facing
// wording lives in ServiceNotice (the store keys svc_*), because that is the
// surface that gets translated.
const char* ToString(ServiceSetup::State state);
const char* ToString(ServiceSetup::Phase phase);
const char* ToString(ServiceSetup::Stage stage);
const char* ToString(ServiceSetup::Failure failure);

}  // namespace urnw
