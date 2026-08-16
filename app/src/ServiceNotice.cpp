// ServiceNotice — see ServiceNotice.hpp. One card, one action, one writer.
// SPDX-License-Identifier: MPL-2.0
#include "ServiceNotice.hpp"

#include <algorithm>

#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

// The failure sentences. ONE per Failure enumerator, and every one of them
// says what to do next — that is the whole reason the enum has this many
// members instead of a single "something went wrong".
//
// TODO(store): none of the `svc_*` key ids below exist in
// localizations/keys/*.yaml yet (the Windows twin marks the same ids Adv() for
// the same reason). Until they do, T_() falls through to the English written
// here, which IS the source text those keys must carry — byte for byte.
Glib::ustring FailureLine(ServiceSetup::Failure failure) {
  using F = ServiceSetup::Failure;
  switch (failure) {
    case F::None:
      return {};
    case F::UnsupportedArch:
      return T_("svc_fail_arch",
                "URnetwork publishes service builds for 64-bit Intel and ARM only.");
    case F::NoNetwork:
      return T_("svc_fail_network",
                "Couldn't reach GitHub to find the release. Check the connection and "
                "click again.");
    case F::TlsUnavailable:
      return T_("svc_fail_tls",
                "This system can't make an HTTPS request, so the release can't be "
                "fetched. Install glib-networking, or use the command below.");
    case F::ReleaseUnavailable:
      return T_("svc_fail_release",
                "The selected release channel didn't return a usable release. Pick "
                "another channel in Settings.");
    case F::TagNotFound:
      return T_("svc_fail_tag",
                "That release tag doesn't exist. Pick another one in Settings.");
    case F::RateLimited:
      return T_("svc_fail_rate",
                "GitHub rate-limited this check. Try again in a few minutes.");
    case F::AssetMissing:
      return T_("svc_fail_asset",
                "That release has no service build for this computer's processor.");
    case F::DigestMissing:
      return T_("svc_fail_digest",
                "That release's download carries no checksum, so it can't be "
                "verified. Nothing was installed.");
    case F::DownloadFailed:
      return T_("svc_fail_download",
                "The download didn't finish. Click again to retry it.");
    case F::ChecksumMismatch:
      return T_("svc_fail_checksum",
                "The download didn't match the release's checksum, so it was "
                "discarded. Click again to retry it.");
    case F::CacheUnwritable:
      return T_("svc_fail_cache",
                "The app's cache folder isn't writable, so the download can't be "
                "staged.");
    case F::TarMissing:
      return T_("svc_fail_tar",
                "There's no tar here to unpack the download with. Use the command "
                "below in a terminal.");
    case F::ExtractFailed:
      return T_("svc_fail_extract",
                "The download couldn't be unpacked. Nothing was installed.");
    case F::PolkitUnavailable:
      return T_("svc_fail_polkit",
                "pkexec isn't installed, so the app can't ask for permission. Use "
                "the command below in a terminal.");
    case F::SpawnUnavailable:
      return T_("svc_fail_spawn",
                "This sandboxed build isn't permitted to run a command on the host. "
                "Use the command below in a terminal.");
    case F::ElevatedFailed:
      return T_("svc_fail_elevated",
                "The installer didn't finish — the service is unchanged.");
    case F::ElevatedTimeout:
      return T_("svc_fail_timeout",
                "The installer was still running when the time ran out and was "
                "stopped. Run the command below in a terminal to see what it says.");
    case F::DaemonDidNotAppear:
      return T_("svc_fail_group",
                "The service was installed, but this session still can't reach it: "
                "you were added to the 'urnetwork' group, and that only applies to a "
                "new login. Log out and back in.");
  }
  return {};
}

Glib::ustring StageLine(ServiceSetup::Stage stage) {
  using S = ServiceSetup::Stage;
  switch (stage) {
    case S::CheckingRelease:
      return T_("svc_stage_checking", "Checking for the current release…");
    case S::Downloading:
      return T_("svc_stage_downloading", "Downloading the service…");
    case S::Verifying:
      return T_("svc_stage_verifying", "Verifying the download…");
    case S::Extracting:
      return T_("svc_stage_extracting", "Unpacking…");
    case S::Elevating:
      // The one shipped string that fits this exact moment (its comment scopes
      // it to the browser extension; its value is precisely this).
      return T_("site_ext_setting_up", "Setting up…");
    case S::Confirming:
      return T_("svc_stage_confirming", "Waiting for the service to answer…");
    case S::Idle:
      break;
  }
  return T_("site_ext_setting_up", "Setting up…");
}

// The failures that mean "the app cannot do this for you in this environment".
// For these the fallback command is not an extra — it IS the remedy, so it
// opens itself rather than hiding behind a disclosure the user must find.
bool FailureNeedsTerminal(ServiceSetup::Failure failure) {
  using F = ServiceSetup::Failure;
  return failure == F::PolkitUnavailable || failure == F::SpawnUnavailable ||
         failure == F::TarMissing || failure == F::TlsUnavailable ||
         failure == F::ElevatedTimeout;
}

constexpr std::size_t kMaxDetailChars = 220;

}  // namespace

ServiceNotice::ServiceNotice() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
  EnsureDrawerCss();
  BuildUi();
  set_visible(false);
}

ServiceNotice::~ServiceNotice() {
  Unsubscribe();
  if (pulse_.connected()) pulse_.disconnect();
}

void ServiceNotice::BuildUi() {
  card_ = MakeCard(8);
  card_->set_hexpand(true);
  append(*card_);

  // ---- header: a warning mark + the title -----------------------------------
  auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  auto* dot = Gtk::make_managed<Gtk::Label>();
  dot->set_markup("<span foreground=\"" + HexForMarkup(kUrCoral) + "\">●</span>");
  dot->set_valign(Gtk::Align::CENTER);
  // Decorative: the title already says what this is. A screen reader that also
  // announced "black circle" would be reading the styling out loud.
  kit::MarkDecorative(*dot);
  header->append(*dot);

  title_ = Gtk::make_managed<Gtk::Label>();
  title_->set_xalign(0);
  title_->set_hexpand(true);
  title_->set_wrap(true);
  title_->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  title_->add_css_class("ur-row-title");
  header->append(*title_);
  card_->append(*header);

  // ---- the message: the pitch, or whatever replaced it ---------------------
  message_ = Gtk::make_managed<Gtk::Label>();
  message_->set_xalign(0);
  message_->set_wrap(true);
  message_->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  message_->add_css_class("ur-caption");
  card_->append(*message_);

  // ---- the detail line: DATA (versions, exit codes, hashes), never a
  // translated sentence, so it is composed rather than concatenated into one.
  status_ = Gtk::make_managed<Gtk::Label>();
  status_->set_xalign(0);
  status_->set_wrap(true);
  status_->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  status_->add_css_class("ur-caption");
  card_->append(*status_);

  progress_ = Gtk::make_managed<Gtk::ProgressBar>();
  progress_->set_visible(false);
  progress_->set_pulse_step(0.12);
  card_->append(*progress_);

  // ---- actions --------------------------------------------------------------
  auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  actions->set_margin_top(4);

  action_ = Gtk::make_managed<Gtk::Button>();
  action_->add_css_class("ur-pane-primary");
  WireButtonPressFeedback(*action_);
  action_->signal_clicked().connect(sigc::mem_fun(*this, &ServiceNotice::OnAction));
  actions->append(*action_);

  cancel_ = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  cancel_->add_css_class("ur-pane-secondary");
  cancel_->set_visible(false);
  WireButtonPressFeedback(*cancel_);
  cancel_->signal_clicked().connect(sigc::mem_fun(*this, &ServiceNotice::OnCancel));
  actions->append(*cancel_);

  auto* spacer = Gtk::make_managed<Gtk::Box>();
  spacer->set_hexpand(true);
  actions->append(*spacer);

  commandToggle_ = Gtk::make_managed<Gtk::Button>();
  commandToggle_->add_css_class("ur-quiet-link");
  commandToggle_->set_valign(Gtk::Align::CENTER);
  commandToggle_->signal_clicked().connect(sigc::mem_fun(*this, &ServiceNotice::ToggleCommand));
  actions->append(*commandToggle_);
  card_->append(*actions);

  // ---- the no-permission fallback ------------------------------------------
  // Always available, never in the way. Selectable AND copyable: a user on a
  // machine with no polkit agent has to be able to get this into a terminal
  // without retyping a sha256.
  commandRevealer_ = Gtk::make_managed<Gtk::Revealer>();
  commandRevealer_->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
  auto* commandBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  commandBox->set_margin_top(4);
  commandText_ = Gtk::make_managed<Gtk::Label>();
  commandText_->set_xalign(0);
  commandText_->set_hexpand(true);
  commandText_->set_wrap(true);
  commandText_->set_wrap_mode(Pango::WrapMode::CHAR);
  commandText_->set_selectable(true);
  commandText_->add_css_class("monospace");
  commandText_->add_css_class("ur-caption");
  commandBox->append(*commandText_);
  commandCopy_ = Gtk::make_managed<Gtk::Button>(T_("copy", "Copy"));
  commandCopy_->add_css_class("ur-pane-secondary");
  commandCopy_->set_valign(Gtk::Align::START);
  WireButtonPressFeedback(*commandCopy_);
  commandCopy_->signal_clicked().connect(sigc::mem_fun(*this, &ServiceNotice::CopyCommand));
  commandBox->append(*commandCopy_);
  commandRevealer_->set_child(*commandBox);
  card_->append(*commandRevealer_);

  commandToggle_->set_label(T_("svc_show_command", "Use a terminal instead"));
  kit::SetAccessibleLabel(*commandCopy_, T_("svc_copy_command", "Copy the install command"));
}

// ---------------------------------------------------------------------------

ServiceNotice* ServiceNotice::Attach(Gtk::Box& host, bool prepend) {
  auto* notice = Gtk::make_managed<ServiceNotice>();
  if (prepend) {
    host.prepend(*notice);
  } else {
    host.append(*notice);
  }
  ServiceSetup& setup = ServiceSetup::Instance();
  notice->Subscribe(setup);
  // Replay first, THEN refresh: a card built minutes after the first probe
  // must never spend a frame blank, and a card built before the first probe
  // must not wait for a window-activation to fill itself in.
  notice->Render(setup.Current());
  setup.RefreshNow(true);
  return notice;
}

void ServiceNotice::Subscribe(ServiceSetup& setup) {
  Unsubscribe();
  setup_ = &setup;
  std::weak_ptr<int> alive = alive_;
  ServiceNotice* self = this;
  token_ = setup.AddHandler([alive, self](const ServiceSetup::Snapshot& snapshot) {
    // Runs on the GTK main loop. The weak token is what makes a post that was
    // already queued when this widget died a no-op instead of a use-after-free.
    if (alive.expired()) return;
    self->Render(snapshot);
  });
}

void ServiceNotice::Unsubscribe() {
  if (setup_ != nullptr && token_ != 0) setup_->RemoveHandler(token_);
  setup_ = nullptr;
  token_ = 0;
}

bool ServiceNotice::ShouldShow(const ServiceSetup::Snapshot& snapshot) {
  using State = ServiceSetup::State;
  using Phase = ServiceSetup::Phase;
  // An action in flight is always worth showing — including the one the user
  // started from Settings, which is how they find out it is running.
  if (snapshot.busy) return true;
  // A failure STAYS until something changes it: a click that silently did
  // nothing is the worst outcome available here.
  if (snapshot.phase == Phase::Failed) return true;
  switch (snapshot.observation.state) {
    case State::NotInstalled:
    case State::Stopped:
    case State::VersionMismatch:
      return true;
    case State::Running:
    case State::Unknown:
      // Healthy needs no banner. Unknown needs no banner either — that is the
      // permission-denied and stale-mount case, whose fix is not this card's
      // action, and a wrong banner teaches the user to ignore the right one.
      return false;
  }
  return false;
}

void ServiceNotice::Render(const ServiceSetup::Snapshot& snapshot) {
  using State = ServiceSetup::State;
  using Phase = ServiceSetup::Phase;

  const bool show = ShouldShow(snapshot);
  set_visible(show);
  if (!show) {
    SetPulsing(false);
    return;
  }

  // The title is committed to the state that made this card appear and does
  // not follow the daemon through a restart (see wording_).
  const State state = snapshot.observation.state;
  if (state == State::NotInstalled || state == State::Stopped ||
      state == State::VersionMismatch) {
    wording_ = state;
  }

  Glib::ustring title;
  Glib::ustring action;
  Glib::ustring message;
  switch (wording_) {
    case State::Stopped:
      title = T_("svc_start_title", "Start the VPN service");
      action = T_("svc_start_action", "Start");
      message = T_("svc_start_message", "The service is installed but not running.");
      break;
    case State::VersionMismatch:
      title = T_("svc_update_title", "Update the VPN service");
      action = T_("update", "Update");
      message = T_("svc_update_message",
                   "The installed service is a different version than this app.");
      break;
    default:
      title = T_("svc_setup_title", "Set up the VPN service");
      action = T_("svc_setup_action", "Set up");
      message = T_("svc_setup_message",
                   "URnetwork uses a system service to carry traffic. One click — your "
                   "system will ask for permission.");
      break;
  }

  // The version pair is DATA (release-grammar strings, never translated), so
  // it goes on the detail line rather than being concatenated into a sentence.
  Glib::ustring detail;
  if (wording_ == State::VersionMismatch && !snapshot.observation.installedVersion.empty() &&
      !snapshot.observation.targetVersion.empty()) {
    detail = snapshot.observation.installedVersion + " → " +
             snapshot.observation.targetVersion;
  } else if (!snapshot.offeredVersion.empty() && wording_ != State::VersionMismatch &&
             snapshot.phase != Phase::Failed) {
    detail = snapshot.offeredVersion;
  }

  // The in-flight / aftermath line REPLACES the pitch. The title keeps saying
  // what the card is for while the message says what is happening to it.
  bool isError = false;
  if (snapshot.busy) {
    message = StageLine(snapshot.stage);
    detail.clear();
  } else if (snapshot.phase == Phase::Cancelled) {
    // A dismissed polkit dialog is a NORMAL outcome, not an error: calm
    // wording, no error colour, nothing in the log.
    message = T_("svc_pkexec_declined",
                 "The permission prompt was closed. Click again whenever you're ready.");
    detail.clear();
  } else if (snapshot.phase == Phase::Failed) {
    message = FailureLine(snapshot.failure);
    isError = true;
    detail.clear();
    if (!snapshot.failureDetail.empty() &&
        snapshot.failureDetail.size() <= kMaxDetailChars) {
      detail = snapshot.failureDetail;
    }
  }

  kit::SetTextOrCollapse(*title_, title);
  kit::SetTextOrCollapse(*message_, message);
  kit::SetTextOrCollapse(*status_, detail);
  status_->remove_css_class("ur-error-text");
  status_->remove_css_class("ur-caption");
  status_->add_css_class(isError ? "ur-error-text" : "ur-caption");

  action_->set_label(action);
  action_->set_sensitive(!snapshot.busy);
  kit::SetAccessibleLabel(*action_, action + ": " + title);
  cancel_->set_visible(snapshot.busy);

  if (snapshot.busy && snapshot.progress >= 0.0) {
    SetPulsing(false);
    progress_->set_visible(true);
    progress_->set_fraction(std::min(1.0, std::max(0.0, snapshot.progress)));
  } else if (snapshot.busy) {
    SetPulsing(true);
  } else {
    SetPulsing(false);
  }

  command_ = snapshot.fallbackCommand;
  commandText_->set_text(command_);
  const bool force = snapshot.phase == Phase::Failed && FailureNeedsTerminal(snapshot.failure);
  if (force) commandOpen_ = true;
  commandRevealer_->set_reveal_child(commandOpen_);
  commandToggle_->set_label(commandOpen_ ? T_("svc_hide_command", "Hide the command")
                                         : T_("svc_show_command", "Use a terminal instead"));
  commandToggle_->set_visible(!command_.empty());
}

void ServiceNotice::OnAction() {
  if (setup_ != nullptr) {
    setup_->BeginInstall();
  } else {
    ServiceSetup::Instance().BeginInstall();
  }
}

void ServiceNotice::OnCancel() {
  if (setup_ != nullptr) {
    setup_->Cancel();
  } else {
    ServiceSetup::Instance().Cancel();
  }
}

void ServiceNotice::ToggleCommand() {
  commandOpen_ = !commandOpen_;
  commandRevealer_->set_reveal_child(commandOpen_);
  commandToggle_->set_label(commandOpen_ ? T_("svc_hide_command", "Hide the command")
                                         : T_("svc_show_command", "Use a terminal instead"));
}

void ServiceNotice::CopyCommand() {
  if (command_.empty()) return;
  get_clipboard()->set_text(command_);
  ShowToast(*this, T_("copied", "Copied"));
}

void ServiceNotice::SetPulsing(bool on) {
  if (!on) {
    if (pulse_.connected()) pulse_.disconnect();
    progress_->set_visible(false);
    return;
  }
  progress_->set_visible(true);
  if (pulse_.connected()) return;
  progress_->pulse();
  pulse_ = Glib::signal_timeout().connect(
      [this]() {
        progress_->pulse();
        return true;
      },
      120);
}

}  // namespace urnw
