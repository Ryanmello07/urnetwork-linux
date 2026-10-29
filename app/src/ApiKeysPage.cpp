// SPDX-License-Identifier: MPL-2.0
#include "ApiKeysPage.hpp"

#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <exception>
#include <optional>
#include <utility>

#include "BrandIcons.hpp"
#include "Formatters.hpp"
#include "I18n.hpp"
#include "KeyedReconcile.hpp"
#include "Ui.hpp"
#include "UrTheme.hpp"

namespace urnw {
namespace {

constexpr int kRowPadY = 10;         // a row's lines: padding 12,10
constexpr int kProsePadY = 10;       // the intro and the notice
constexpr int kStateTopPx = 48;      // the state line, down from the header
constexpr int kRefreshGlyphPx = 16;
constexpr int kSheetWidth = 360;     // the create sheet
// the sheet's lines wrap at these widths, in characters: a wrapped label
// otherwise asks for its whole text on one line and widens the sheet
constexpr int kSheetProseChars = 44;
constexpr int kSheetKeyChars = 36;   // the key, monospace: two lines of a 56-character key
constexpr int kConfirmWidth = 320;   // the delete confirmation (SessionsPage's)

// A line's text in a tone: a pango run beats the class colour (AccountPage's
// SetToned; the text is escaped, so a key's name stays plain text).
void SetToned(Gtk::Label& line, const Rgba& color, const Glib::ustring& text) {
  line.set_markup("<span foreground='" + HexForMarkup(color) + "'>" +
                  Glib::Markup::escape_text(text) + "</span>");
}

std::string Lookup(const api_keys::Text& text) { return T_(text.key, text.english); }

// A left-aligned line that wraps rather than cut a long name or id.
Gtk::Label* MakeLine(const char* cssClass) {
  auto* line = Gtk::make_managed<Gtk::Label>();
  line->add_css_class(cssClass);
  line->set_xalign(0);
  line->set_wrap(true);
  line->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  return line;
}

// A line's full date and time, for a screen reader (its description) and for
// the pointer (its tooltip). "" clears both.
void SetFullTime(Gtk::Label& line, const std::string& full) {
  gtk_accessible_update_property(GTK_ACCESSIBLE(line.gobj()), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                 full.c_str(), -1);
  if (full.empty()) {
    line.set_has_tooltip(false);
  } else {
    line.set_tooltip_text(full);
  }
}

// A prose row on the pane grid (12px inset and hairline from the kit), as tall
// as its column.
Gtk::Widget* MakeProseRow(Gtk::Box*& column) {
  auto* host = kit::MakePaneRow(0);
  column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  column->set_hexpand(true);
  column->set_margin_top(kProsePadY);
  column->set_margin_bottom(kProsePadY);
  if (auto* inner = dynamic_cast<Gtk::Box*>(host->get_first_child())) inner->append(*column);
  return host;
}

// A muted line of prose that wraps.
Gtk::Label* MakeNote(const Glib::ustring& text) {
  auto* line = Gtk::make_managed<Gtk::Label>(text);
  line->add_css_class("ur-caption");
  line->set_xalign(0);
  line->set_wrap(true);
  return line;
}

// ---- the sdk's answers, as plain data -------------------------------------------
// A result that arrived with neither a transport error nor the server's error
// is the answer; anything else failed. A failure is logged by what failed,
// never with a key.

api_keys::ListAnswer ListAnswerOf(const std::optional<urnet::ListApiKeysResult>& result,
                                  const std::optional<std::string>& err) {
  api_keys::ListAnswer answer;
  answer.ok = result.has_value() && !err.has_value() && !result->error.has_value();
  if (!answer.ok) {
    g_warning("api keys: listing the keys failed: %s",
              err ? err->c_str()
                  : result && result->error ? result->error->message.c_str() : "no result");
    return answer;
  }
  if (!result->api_keys) return answer;
  for (const urnet::PublicAccountApiKey& key : *result->api_keys) {
    api_keys::Key out;
    out.id = key.id.value_or(std::string());
    out.name = key.name;
    // the C ABI hands the server's time.Time over as its RFC 3339 text
    out.createUnixSeconds =
        api_keys::ParseRfc3339UnixSeconds(key.create_time.value_or(std::string())).value_or(0);
    answer.keys.push_back(std::move(out));
  }
  return answer;
}

// The one answer that carries a key: it moves into the answer, and the
// result's copy is wiped. The log says what failed and nothing the answer
// carried.
api_keys::CreateAnswer CreateAnswerOf(std::optional<urnet::CreateApiKeyResult>& result,
                                      const std::optional<std::string>& err) {
  api_keys::CreateAnswer answer;
  if (result && !err && !result->error && result->api_key) {
    answer.ok = true;
    answer.apiKey = std::move(*result->api_key);
  } else if (result && result->error) {
    g_warning("api keys: the server refused the create: %s", result->error->message.c_str());
  } else {
    g_warning("api keys: the create had no answer from the api");
  }
  if (result && result->api_key) api_keys::Wipe(*result->api_key);
  return answer;
}

api_keys::DeleteAnswer DeleteAnswerOf(const std::optional<urnet::DeleteApiKeyResult>& result,
                                      const std::optional<std::string>& err) {
  api_keys::DeleteAnswer answer;
  answer.ok = result.has_value() && !err.has_value() && !result->error.has_value();
  if (!answer.ok) {
    g_warning("api keys: deleting a key failed: %s",
              err ? err->c_str()
                  : result && result->error ? result->error->message.c_str() : "no result");
  }
  return answer;
}

// The account's Api behind api_keys::Service. Each answer arrives on an sdk
// thread, is copied into plain data there and posted to the main loop, where
// it is handed over only while the page is alive.
class SdkService final : public api_keys::Service {
 public:
  SdkService(SdkHost& host, std::shared_ptr<bool> alive) : host_(host), alive_(std::move(alive)) {}

  void List(std::function<void(api_keys::ListAnswer)> done) override {
    host_.api().listApiKeys([alive = alive_, done = std::move(done)](
                                std::optional<urnet::ListApiKeysResult> result,
                                std::optional<std::string> err) {
      api_keys::ListAnswer answer = ListAnswerOf(result, err);
      PostToMain([alive, done, answer = std::move(answer)]() mutable {
        if (*alive) done(std::move(answer));
      });
    });
  }

  void Create(const std::string& name, std::function<void(api_keys::CreateAnswer)> done) override {
    urnet::CreateApiKeyArgs args;
    args.name = name;
    host_.api().createApiKey(
        std::optional<urnet::CreateApiKeyArgs>(args),
        [alive = alive_, done = std::move(done)](std::optional<urnet::CreateApiKeyResult> result,
                                                 std::optional<std::string> err) {
          api_keys::CreateAnswer answer = CreateAnswerOf(result, err);
          PostToMain([alive, done, answer = std::move(answer)]() mutable {
            if (*alive) {
              done(std::move(answer));
            } else {
              api_keys::Wipe(answer.apiKey);
            }
          });
        });
  }

  void Delete(const std::string& keyId, std::function<void(api_keys::DeleteAnswer)> done) override {
    urnet::DeleteApiKeyArgs args;
    args.id = keyId;
    host_.api().deleteApiKey(
        std::optional<urnet::DeleteApiKeyArgs>(args),
        [alive = alive_, done = std::move(done)](std::optional<urnet::DeleteApiKeyResult> result,
                                                 std::optional<std::string> err) {
          const api_keys::DeleteAnswer answer = DeleteAnswerOf(result, err);
          PostToMain([alive, done, answer] {
            if (*alive) done(answer);
          });
        });
  }

 private:
  SdkHost& host_;
  std::shared_ptr<bool> alive_;
};

}  // namespace

// =============================================================================
// ApiKeysPage
// =============================================================================

ApiKeysPage::ApiKeysPage(SdkHost& host)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0),
      host_(host),
      binding_(settings_, [alive = alive_](int delayMs, std::function<void()> fire) {
        // a request's give-up: nothing once the page is gone
        Glib::signal_timeout().connect_once(
            [alive, fire = std::move(fire)] {
              if (*alive) fire();
            },
            delayMs);
      }) {
  EnsureBrandCss();   // the pane-shell vocabulary
  EnsureDrawerCss();  // .ur-caption and the sheet styles
  set_hexpand(true);
  set_vexpand(true);
  BuildPane();
  append(*pane_.root);
  binding_.on_changed = [this] { Render(); };
  Render();
}

ApiKeysPage::~ApiKeysPage() {
  *alive_ = false;  // every marshaled post and timer does nothing from here
  copiedTimer_.disconnect();
  // The binding and the key only: by now the window's teardown may have
  // disposed the page's widgets (EarningsPage, issue #13), so nothing here
  // touches them.
  binding_.on_changed = nullptr;
  binding_.Release();
  createFlow_.Close();
  sheet_ = CreateSheet{};  // its hide handler finds no label to clear
  createDialog_.reset();
  confirmDialog_.reset();
}

void ApiKeysPage::BuildPane() {
  pane_ = kit::MakePane(Lookup(api_keys::kTitleText));
  pane_.root->set_hexpand(true);
  kit::SetAccessibleLabel(*pane_.root, Lookup(api_keys::kTitleText));

  // The header: progress while a refresh runs, the refresh command, and the
  // way back to Account (SessionsPage's header).
  refreshSpinner_ = Gtk::make_managed<Gtk::Spinner>();
  refreshSpinner_->set_valign(Gtk::Align::CENTER);
  refreshSpinner_->set_visible(false);
  kit::SetAccessibleLabel(*refreshSpinner_, Lookup(api_keys::kLoadingText));
  pane_.header->append(*refreshSpinner_);

  refreshButton_ = Gtk::make_managed<Gtk::Button>();
  refreshButton_->add_css_class("ur-pane-action");
  refreshButton_->set_child(*Gtk::make_managed<GlyphIcon>(MdiGlyph::Refresh, kRefreshGlyphPx));
  refreshButton_->set_valign(Gtk::Align::CENTER);
  refreshButton_->set_tooltip_text(Lookup(api_keys::kRefreshText));
  kit::SetAccessibleLabel(*refreshButton_, Lookup(api_keys::kRefreshText));
  refreshButton_->signal_clicked().connect([this] { binding_.Refresh(); });
  pane_.header->append(*refreshButton_);

  auto* back = Gtk::make_managed<Gtk::Button>();
  back->add_css_class("flat");
  back->set_label(Glib::ustring("‹ ") + Lookup(api_keys::kAccountText));
  kit::SetAccessibleLabel(*back, Lookup(api_keys::kAccountText));
  back->signal_clicked().connect([this] {
    if (on_back) on_back();
  });
  pane_.header->append(*back);

  Gtk::Box* content = pane_.content;

  // what a key is for and what it can do, the documentation under it, and
  // the way to make one
  {
    Gtk::Box* column = nullptr;
    auto* row = MakeProseRow(column);
    column->append(*MakeNote(Lookup(api_keys::kIntroText)));
    // the pink accent link of a note, with the outward arrow ("Learn more ↗")
    auto* docs = Gtk::make_managed<Gtk::Label>();
    docs->set_markup("<a href=\"" + Glib::Markup::escape_text(api_keys::kDocsUrl) + "\">" +
                     Glib::Markup::escape_text(Lookup(api_keys::kDocsLinkText)) +
                     "\xC2\xA0\xE2\x86\x97</a>");
    docs->add_css_class("ur-row-note");
    docs->add_css_class("ur-learn-more");
    docs->set_xalign(0);
    docs->set_wrap(true);
    docs->signal_activate_link().connect(
        [this](const Glib::ustring&) -> bool {
          OpenDocs();
          return true;  // handled; GTK must not launch a second time
        },
        false);
    column->append(*docs);
    content->append(*row);
    // the pane's primary action, on the pane itself: its style brings the
    // 12px inset all round
    createButton_ = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kCreateText));
    createButton_->add_css_class("ur-pane-primary");
    createButton_->set_halign(Gtk::Align::START);
    createButton_->signal_clicked().connect([this] { OpenCreateSheet(); });
    content->append(*createButton_);
  }

  // a failed refresh keeps the list, under this non-blocking notice
  {
    Gtk::Box* column = nullptr;
    noticeRow_ = MakeProseRow(column);
    auto* notice = MakeNote({});
    SetToned(*notice, kUrDanger, Lookup(api_keys::kLoadFailedText));
    column->append(*notice);
    noticeRow_->set_visible(false);
    content->append(*noticeRow_);
  }

  // the state line of a screen with no rows: progress, empty, failed (with
  // Retry), signed out
  {
    stateBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    stateBox_->set_halign(Gtk::Align::CENTER);
    stateBox_->set_margin_top(kStateTopPx);
    stateBox_->set_margin_start(24);
    stateBox_->set_margin_end(24);
    stateSpinner_ = Gtk::make_managed<Gtk::Spinner>();
    stateSpinner_->set_visible(false);
    stateBox_->append(*stateSpinner_);
    stateText_ = Gtk::make_managed<Gtk::Label>();
    stateText_->add_css_class("ur-caption");
    stateText_->set_wrap(true);
    stateText_->set_justify(Gtk::Justification::CENTER);
    stateBox_->append(*stateText_);
    retryButton_ = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kRetryText));
    retryButton_->set_halign(Gtk::Align::CENTER);
    retryButton_->set_visible(false);
    retryButton_->signal_clicked().connect([this] { Retry(); });
    stateBox_->append(*retryButton_);
    content->append(*stateBox_);
  }

  rowsBox_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  content->append(*rowsBox_);
}

// ---- gates and words ---------------------------------------------------------

bool ApiKeysPage::CanCallApi() {
  // preview first (a preview build must never reach production authenticated),
  // then the SESSION, never apiReady() (true from sdk init, long before a login)
  return !previewMode_ && host_.IsLoggedIn();
}

api_keys::Words ApiKeysPage::MakeWords() const {
  api_keys::Words words;
  words.lookup = [](const api_keys::Text& text) { return Lookup(text); };
  words.format = [](const std::string& pattern, const std::vector<std::string>& args) {
    return FormatArgs(pattern.c_str(), args);
  };
  words.date = [](int64_t unixSeconds) { return LocalDate(unixSeconds); };
  words.dateTime = [](int64_t unixSeconds) { return LocalDateTime(unixSeconds); };
  return words;
}

void ApiKeysPage::Snack(const Glib::ustring& message, bool error) {
  if (on_snackbar) {
    on_snackbar(message, error);
    return;
  }
  g_warning("api keys: snackbar unbound; dropping message: %s", message.c_str());
}

// ---- lifecycle -----------------------------------------------------------------

void ApiKeysPage::SetPreviewMode(bool on) { previewMode_ = on; }

void ApiKeysPage::ShowPreviewState() {
  binding_.Release();
  Render();
}

void ApiKeysPage::Load() {
  if (!CanCallApi()) {
    // no session: no service and no request (a 401 must never arrive to be
    // read as an empty list)
    binding_.Release();
    Render();
    return;
  }
  EnsureService();
  // every visit lists the keys again; nothing while a list is out
  binding_.Refresh();
  Render();
}

void ApiKeysPage::ResetForSignOut() {
  binding_.Release();  // the departed account's requests: nothing they answer lands now
  deleteFlow_.Cancel();
  if (confirmDialog_ && confirmDialog_->get_visible()) confirmDialog_->set_visible(false);
  // the create sheet goes, and the key it shows with it, even with its
  // request out (its answer is dropped with the release above)
  CloseCreateSheet(true);
  Render();
}

// The service is attached to the account's Api once, and again only for a
// different Api (a server or extender change replaces it): the binding's
// source is the Api's handle, which no other Api ever has.
void ApiKeysPage::EnsureService() {
  const uint64_t source = host_.api().handle();
  if (binding_.Attached() && binding_.Source() == source) return;
  binding_.Attach(std::make_unique<SdkService>(host_, alive_), source);
}

void ApiKeysPage::Retry() {
  if (!binding_.Attached()) {
    Load();
    return;
  }
  binding_.Refresh();
}

void ApiKeysPage::OpenDocs() {
  GError* err = nullptr;
  if (!g_app_info_launch_default_for_uri(api_keys::kDocsUrl, nullptr, &err)) {
    g_warning("api keys: could not open %s: %s", api_keys::kDocsUrl, err ? err->message : "?");
    if (err) g_error_free(err);
    Snack(Lookup(api_keys::kSomethingWentWrongText), true);
  }
}

// ---- rendering --------------------------------------------------------------------

void ApiKeysPage::Render() {
  const bool signedIn = CanCallApi();
  const api_keys::Snapshot& snapshot = binding_.Read();
  const api_keys::Screen screen = api_keys::ScreenFor(snapshot, signedIn);

  refreshButton_->set_sensitive(signedIn && binding_.Attached());
  refreshSpinner_->set_visible(screen.refreshing);
  if (screen.refreshing) {
    refreshSpinner_->start();
  } else {
    refreshSpinner_->stop();
  }
  kit::SetBusy(*pane_.content, screen.refreshing || screen.body == api_keys::Body::Progress);
  createButton_->set_sensitive(screen.canCreate && binding_.Attached());

  const std::optional<api_keys::Text> line = api_keys::BodyText(screen.body);
  stateBox_->set_visible(line.has_value());
  const bool progress = screen.body == api_keys::Body::Progress;
  stateSpinner_->set_visible(progress);
  if (progress) {
    stateSpinner_->start();
  } else {
    stateSpinner_->stop();
  }
  const bool failed = screen.body == api_keys::Body::LoadFailed;
  retryButton_->set_visible(failed);
  if (line) SetToned(*stateText_, failed ? kUrDanger : kUrTextMuted, Lookup(*line));

  if (screen.body == api_keys::Body::Rows) {
    RenderRows(MakeWords());
  } else {
    for (const RowWidgets& row : rows_) rowsBox_->remove(*row.root);
    rows_.clear();
  }
  noticeRow_->set_visible(screen.refreshFailed);
}

// The rows, reconciled by key id (KeyedReconcile.hpp): a row still listed is
// rewritten in place, so its Delete keeps the keyboard focus and a screen
// reader its place across refreshes; a new key is inserted, a gone one
// removed.
void ApiKeysPage::RenderRows(const api_keys::Words& words) {
  const api_keys::Snapshot& snapshot = binding_.Read();
  std::vector<std::string> wanted;
  wanted.reserve(snapshot.keys.size());
  for (const api_keys::Key& key : snapshot.keys) wanted.push_back(key.id);
  std::vector<std::string> onScreen;
  onScreen.reserve(rows_.size());
  for (const RowWidgets& row : rows_) onScreen.push_back(row.keyId);

  const auto render = [&](RowWidgets& row, size_t wantedIndex) {
    const api_keys::Key& key = snapshot.keys[wantedIndex];
    UpdateRow(row, api_keys::RowFor(key, words), api_keys::DeleteViewOf(snapshot, key.id));
  };
  for (const reconcile::Step& step : reconcile::Plan(onScreen, wanted)) {
    switch (step.kind) {
      case reconcile::StepKind::Remove:
        rowsBox_->remove(*rows_[step.index].root);
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(step.index));
        break;
      case reconcile::StepKind::Update:
        render(rows_[step.index], step.wantedIndex);
        break;
      case reconcile::StepKind::Move: {
        RowWidgets row = std::move(rows_[step.from]);
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(step.from));
        render(row, step.wantedIndex);
        if (step.index == 0) {
          rowsBox_->reorder_child_at_start(*row.root);
        } else {
          rowsBox_->reorder_child_after(*row.root, *rows_[step.index - 1].root);
        }
        rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(step.index), std::move(row));
        break;
      }
      case reconcile::StepKind::Insert: {
        RowWidgets row = BuildRow(wanted[step.wantedIndex]);
        render(row, step.wantedIndex);
        if (step.index == 0) {
          rowsBox_->insert_child_at_start(*row.root);
        } else {
          rowsBox_->insert_child_after(*row.root, *rows_[step.index - 1].root);
        }
        rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(step.index), std::move(row));
        break;
      }
    }
  }
}

ApiKeysPage::RowWidgets ApiKeysPage::BuildRow(const std::string& keyId) {
  RowWidgets row;
  row.keyId = keyId;
  auto* host = kit::MakePaneRow(0);  // as tall as its lines
  auto* inner = dynamic_cast<Gtk::Box*>(host->get_first_child());
  inner->set_spacing(12);

  auto* lines = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
  lines->set_hexpand(true);
  lines->set_valign(Gtk::Align::CENTER);
  lines->set_margin_top(kRowPadY);
  lines->set_margin_bottom(kRowPadY);
  row.title = MakeLine("ur-row-title");
  lines->append(*row.title);
  row.created = MakeLine("ur-row-note");
  lines->append(*row.created);
  // the id is for using the key with the API: it can be selected and copied
  row.id = MakeLine("ur-row-note");
  row.id->set_selectable(true);
  lines->append(*row.id);
  row.status = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  row.spinner = Gtk::make_managed<Gtk::Spinner>();
  kit::SetAccessibleLabel(*row.spinner, Lookup(api_keys::kLoadingText));
  row.status->append(*row.spinner);
  row.statusText = MakeLine("ur-row-note");
  row.status->append(*row.statusText);
  row.status->set_visible(false);
  lines->append(*row.status);
  inner->append(*lines);

  // the trailing Delete of a pointer platform, flat and destructive; it only
  // opens the confirmation
  row.del = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kDeleteText));
  row.del->add_css_class("flat");
  row.del->add_css_class("destructive-action");
  row.del->set_valign(Gtk::Align::CENTER);
  row.del->signal_clicked().connect([this, keyId] { PressDelete(keyId); });
  inner->append(*row.del);

  row.root = host;
  return row;
}

void ApiKeysPage::UpdateRow(RowWidgets& row, const api_keys::Row& text,
                            api_keys::ActionView view) {
  // a key's name is plain text, never markup
  row.title->set_text(text.title);
  kit::SetTextOrCollapse(*row.created, text.created);
  SetFullTime(*row.created, text.createdFull);
  row.id->set_text(text.idLine);
  // "Delete. home server": the row's action by name, for a screen reader
  kit::SetAccessibleLabel(*row.del, text.deleteName);

  const bool busy = view == api_keys::ActionView::Busy;
  row.del->set_sensitive(!busy);
  const std::optional<api_keys::Text> status = api_keys::DeleteStatusText(view);
  row.status->set_visible(busy || status.has_value());
  row.spinner->set_visible(busy);
  if (busy) {
    row.spinner->start();
  } else {
    row.spinner->stop();
  }
  row.statusText->set_visible(status.has_value());
  if (status) SetToned(*row.statusText, kUrDanger, Lookup(*status));
  kit::SetBusy(*row.root, busy);
}

// ---- create -------------------------------------------------------------------------

void ApiKeysPage::OpenCreateSheet() {
  if (!CanCallApi() || !binding_.Attached() || createFlow_.IsOpen()) return;
  Gtk::Window* root = RootWindow();
  if (root == nullptr) {
    g_warning("api keys: no window root; the create sheet did not open");
    return;
  }
  if (!BeginSheet("create an api key")) return;
  createFlow_.Open();

  // A modal sheet (the auth code sheet's shape): the name, then the key.
  createDialog_ = std::make_unique<Gtk::Window>();
  createDialog_->set_transient_for(*root);
  createDialog_->set_modal(true);
  createDialog_->set_title(Lookup(api_keys::kCreateText));
  createDialog_->set_resizable(false);
  // hide-on-close, not destroy: a unique_ptr owns this window
  createDialog_->set_hide_on_close(true);
  createDialog_->add_css_class("ur-sheet");
  // Escape and the window manager close it, except while the request is out:
  // its key must have somewhere to show
  {
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) -> bool {
          if (keyval != GDK_KEY_Escape) return false;
          CloseCreateSheet(false);
          return true;
        },
        false);
    createDialog_->add_controller(keys);
    createDialog_->signal_close_request().connect(
        [this]() -> bool { return !createFlow_.CanClose(); }, false);
  }
  {
    auto alive = alive_;
    createDialog_->signal_hide().connect([this, alive] {
      // the key leaves the label at once; the rest after the signal
      if (sheet_.key != nullptr) sheet_.key->set_text({});
      PostToMain([this, alive] {
        if (!*alive) return;
        CreateSheetHidden();
      });
    });
  }

  sheet_ = CreateSheet{};
  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->set_margin(24);
  box->set_size_request(kSheetWidth, -1);
  sheet_.heading = Gtk::make_managed<Gtk::Label>();
  sheet_.heading->add_css_class("ur-step-heading");
  sheet_.heading->set_xalign(0);
  sheet_.heading->set_wrap(true);
  box->append(*sheet_.heading);

  // step 1: the key's name
  sheet_.nameStep = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
  {
    auto* label = Gtk::make_managed<Gtk::Label>(Lookup(api_keys::kNameLabelText));
    label->add_css_class("ur-input-label");
    label->set_xalign(0);
    sheet_.nameStep->append(*label);
    sheet_.name = Gtk::make_managed<Gtk::Entry>();
    sheet_.name->add_css_class("ur-input");
    sheet_.name->set_placeholder_text(Lookup(api_keys::kNamePlaceholderText));
    // the server stores at most this many characters, and GTK counts them so
    sheet_.name->set_max_length(static_cast<int>(api_keys::kMaxNameCharacters));
    kit::SetAccessibleLabel(*sheet_.name, Lookup(api_keys::kNameLabelText));
    sheet_.name->signal_changed().connect([this] { RenderCreateSheet(); });
    sheet_.name->signal_activate().connect([this] { SubmitCreate(); });
    sheet_.nameStep->append(*sheet_.name);
    sheet_.failure = MakeNote({});
    sheet_.failure->set_max_width_chars(kSheetProseChars);
    sheet_.failure->set_visible(false);
    sheet_.nameStep->append(*sheet_.failure);
    auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    actions->set_halign(Gtk::Align::END);
    sheet_.spinner = Gtk::make_managed<Gtk::Spinner>();
    sheet_.spinner->set_valign(Gtk::Align::CENTER);
    sheet_.spinner->set_visible(false);
    kit::SetAccessibleLabel(*sheet_.spinner, Lookup(api_keys::kLoadingText));
    actions->append(*sheet_.spinner);
    sheet_.cancel = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kCancelText));
    sheet_.cancel->signal_clicked().connect([this] { CloseCreateSheet(false); });
    actions->append(*sheet_.cancel);
    sheet_.create = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kCreateText));
    sheet_.create->add_css_class("ur-btn");
    sheet_.create->add_css_class("ur-btn-primary");
    sheet_.create->signal_clicked().connect([this] { SubmitCreate(); });
    actions->append(*sheet_.create);
    sheet_.nameStep->append(*actions);
  }
  box->append(*sheet_.nameStep);

  // step 2: the key, this once, with the warning and Copy key
  sheet_.keyStep = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  {
    auto* warning = MakeNote({});
    warning->set_max_width_chars(kSheetProseChars);
    SetToned(*warning, kUrAmber, Lookup(api_keys::kCreatedWarningText));
    sheet_.keyStep->append(*warning);
    // not selectable: a selection would also put the key on the primary
    // selection, past the sheet; Copy key is the one way out
    sheet_.key = Gtk::make_managed<Gtk::Label>();
    sheet_.key->add_css_class("ur-mono-13");
    sheet_.key->set_xalign(0);
    sheet_.key->set_wrap(true);
    sheet_.key->set_wrap_mode(Pango::WrapMode::CHAR);
    sheet_.key->set_max_width_chars(kSheetKeyChars);
    // a break inside the key gets no hyphen: one would read as part of the key
    {
      Pango::AttrList attrs;
      auto noHyphens = Pango::Attribute::create_attr_insert_hyphens(false);
      attrs.insert(noHyphens);
      sheet_.key->set_attributes(attrs);
    }
    kit::SetAccessibleLabel(*sheet_.key, Lookup(api_keys::kCreatedTitleText));
    sheet_.keyStep->append(*sheet_.key);
    auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    actions->set_halign(Gtk::Align::END);
    sheet_.copy = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kCopyText));
    sheet_.copy->signal_clicked().connect([this] { CopyCreatedKey(); });
    actions->append(*sheet_.copy);
    sheet_.done = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kDoneText));
    sheet_.done->add_css_class("ur-btn");
    sheet_.done->add_css_class("ur-btn-primary");
    sheet_.done->signal_clicked().connect([this] { CloseCreateSheet(false); });
    actions->append(*sheet_.done);
    sheet_.keyStep->append(*actions);
  }
  box->append(*sheet_.keyStep);

  createDialog_->set_child(*box);
  RenderCreateSheet();
  createDialog_->present();
  sheet_.name->grab_focus();
}

// Create pressed (or Enter in the name): the name goes once, and the sheet
// waits for the key.
void ApiKeysPage::SubmitCreate() {
  if (!createDialog_ || sheet_.name == nullptr) return;
  const std::optional<std::string> name = createFlow_.Submit(sheet_.name->get_text().raw());
  if (!name) return;
  const bool sent = binding_.Create(*name, [this](bool ok, std::string apiKey) {
    SettleCreate(ok, std::move(apiKey));
  });
  if (!sent) {
    // no service or a create already out: the name step again, with the failure
    createFlow_.Settle(false, std::string());
  }
  RenderCreateSheet();
}

// The binding's answer, on the main loop: the key into the sheet, or the
// failure under the name.
void ApiKeysPage::SettleCreate(bool ok, std::string apiKey) {
  createFlow_.Settle(ok, std::move(apiKey));
  RenderCreateSheet();
  if (createFlow_.Step() == api_keys::CreateStep::Created && sheet_.copy != nullptr) {
    // Enter copies: the default is the harmless control, never the close
    sheet_.copy->grab_focus();
  } else if (sheet_.name != nullptr) {
    sheet_.name->grab_focus();
  }
}

void ApiKeysPage::RenderCreateSheet() {
  if (!createDialog_ || sheet_.heading == nullptr) return;
  const api_keys::CreateStep step = createFlow_.Step();
  const bool created = step == api_keys::CreateStep::Created;
  const bool creating = step == api_keys::CreateStep::Creating;
  sheet_.heading->set_text(
      Lookup(created ? api_keys::kCreatedTitleText : api_keys::kCreateText));
  createDialog_->set_title(sheet_.heading->get_text());
  sheet_.nameStep->set_visible(!created);
  sheet_.keyStep->set_visible(created);

  sheet_.name->set_sensitive(!creating);
  sheet_.cancel->set_sensitive(!creating);
  sheet_.create->set_sensitive(createFlow_.CanSubmit(sheet_.name->get_text().raw()));
  sheet_.spinner->set_visible(creating);
  if (creating) {
    sheet_.spinner->start();
  } else {
    sheet_.spinner->stop();
  }
  kit::SetBusy(*sheet_.nameStep, creating);
  sheet_.failure->set_visible(createFlow_.Failed());
  if (createFlow_.Failed()) {
    SetToned(*sheet_.failure, kUrDanger, Lookup(api_keys::kCreateFailedText));
  }

  // the key, only while the sheet shows it
  sheet_.key->set_text(created ? createFlow_.ShownKey() : std::string());
  sheet_.copy->set_label(Lookup(createFlow_.CopyLabel()));
  // Enter: Create on the name step, Copy key on the key's (never Done, which
  // would close the one view of the key)
  sheet_.create->set_receives_default(!created);
  sheet_.copy->set_receives_default(created);
  createDialog_->set_default_widget(created ? static_cast<Gtk::Widget&>(*sheet_.copy)
                                            : static_cast<Gtk::Widget&>(*sheet_.create));
}

// Copy key: the whole key on the clipboard, and Copied in its place for a
// moment.
void ApiKeysPage::CopyCreatedKey() {
  std::optional<std::string> key = createFlow_.Copy();
  if (!key) return;
  get_clipboard()->set_text(*key);
  api_keys::Wipe(*key);
  RenderCreateSheet();
  copiedTimer_.disconnect();
  if (settings_.copiedMs <= 0) return;
  auto alive = alive_;
  copiedTimer_ = Glib::signal_timeout().connect(
      [this, alive]() -> bool {
        if (!*alive) return false;
        createFlow_.EndCopied();
        RenderCreateSheet();
        return false;  // one shot
      },
      settings_.copiedMs);
}

// Done, Cancel and Escape (`force` false): the sheet hides, unless its request
// is out, so the key it returns has somewhere to show. A sign-out (`force`)
// closes it anyway; the release before it drops that answer.
void ApiKeysPage::CloseCreateSheet(bool force) {
  if (!force && !createFlow_.CanClose()) return;
  if (createDialog_ && createDialog_->get_visible()) {
    if (force) createFlow_.Close();
    // its hide wipes the key from the label and opens the gate
    createDialog_->set_visible(false);
    return;
  }
  createFlow_.Close();
}

// The sheet is gone: the key is wiped, the gate opens, and the focus goes back
// to Create API key.
void ApiKeysPage::CreateSheetHidden() {
  copiedTimer_.disconnect();
  createFlow_.Close();
  if (sheet_.key != nullptr) sheet_.key->set_text({});
  EndSheet();
  if (createButton_->get_sensitive()) createButton_->grab_focus();
}

// ---- delete -------------------------------------------------------------------------

void ApiKeysPage::PressDelete(const std::string& keyId) {
  if (!CanCallApi() || !binding_.Attached()) return;
  // nothing while that delete runs, while a confirmation is open, or for a
  // key that is no longer listed (DeleteFlow)
  const std::optional<api_keys::DeleteConfirmation> confirmation =
      deleteFlow_.Press(binding_.Read(), keyId, MakeWords());
  if (!confirmation) return;
  ShowDeleteConfirmation(*confirmation);
}

void ApiKeysPage::ShowDeleteConfirmation(const api_keys::DeleteConfirmation& confirmation) {
  Gtk::Window* root = RootWindow();
  if (root == nullptr) {
    g_warning("api keys: no window root; the delete confirmation did not open");
    deleteFlow_.Cancel();
    return;
  }
  if (!BeginSheet("delete an api key")) {
    deleteFlow_.Cancel();
    return;
  }

  // A MODAL confirmation for every delete (SessionsPage's): the destructive
  // word only on the committing control.
  const Glib::ustring title = Lookup(confirmation.title);
  confirmDialog_ = std::make_unique<Gtk::Window>();
  confirmDialog_->set_transient_for(*root);
  confirmDialog_->set_modal(true);
  confirmDialog_->set_title(title);
  confirmDialog_->set_resizable(false);
  // hide-on-close, not destroy: a unique_ptr owns this window
  confirmDialog_->set_hide_on_close(true);
  confirmDialog_->add_css_class("ur-sheet");
  AddEscapeToClose(*confirmDialog_);
  // Closed without Delete (Cancel, Escape, the window manager): nothing is
  // sent. The focus goes back to the row that asked.
  {
    auto alive = alive_;
    const std::string keyId = confirmation.keyId;
    confirmDialog_->signal_hide().connect([this, alive, keyId] {
      // never tear anything down inside the widget's own signal
      PostToMain([this, alive, keyId] {
        if (!*alive) return;
        deleteFlow_.Cancel();
        EndSheet();
        RestoreFocus(keyId);
      });
    });
  }

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->set_margin(24);
  box->set_size_request(kConfirmWidth, -1);
  auto* heading = Gtk::make_managed<Gtk::Label>(title);
  heading->add_css_class("ur-step-heading");
  heading->set_xalign(0);
  heading->set_wrap(true);
  box->append(*heading);
  // the key's name is plain text: the label takes no markup
  auto* body =
      Gtk::make_managed<Gtk::Label>(api_keys::ConfirmationBody(confirmation, MakeWords()));
  body->add_css_class("ur-body");
  body->set_xalign(0);
  body->set_wrap(true);
  body->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  body->set_max_width_chars(40);
  box->append(*body);

  auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  actions->set_halign(Gtk::Align::END);
  auto* cancel = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kCancelText));
  cancel->signal_clicked().connect([this] { confirmDialog_->set_visible(false); });
  actions->append(*cancel);
  auto* del = Gtk::make_managed<Gtk::Button>(Lookup(api_keys::kDeleteText));
  del->add_css_class("destructive-action");  // red IS the confirmation context
  del->signal_clicked().connect([this] {
    // the key, once: a second press cannot send a second delete
    const std::optional<std::string> keyId = deleteFlow_.Confirm();
    confirmDialog_->set_visible(false);
    if (keyId) binding_.Delete(*keyId);
  });
  actions->append(*del);
  box->append(*actions);

  confirmDialog_->set_child(*box);
  // DEFAULT IS CANCEL: Enter must not delete anything
  cancel->set_receives_default(true);
  confirmDialog_->set_default_widget(*cancel);
  confirmDialog_->present();
  cancel->grab_focus();
}

// After the confirmation: the focus back on the row's Delete, when it is
// still there and still takes a press.
void ApiKeysPage::RestoreFocus(const std::string& keyId) {
  for (RowWidgets& row : rows_) {
    if (row.keyId != keyId) continue;
    if (row.del->get_sensitive()) row.del->grab_focus();
    return;
  }
}

// ---- the one-modal gate --------------------------------------------------------------

Gtk::Window* ApiKeysPage::RootWindow() { return dynamic_cast<Gtk::Window*>(get_root()); }

bool ApiKeysPage::BeginSheet(const char* what) {
  if (sheetShowing_ || (sheet_open && sheet_open())) {
    // a click that opens nothing stays a mystery: logged, never silent
    g_message("api keys: %s suppressed — a modal is already open", what);
    return false;
  }
  sheetShowing_ = true;
  if (on_sheet_open_changed) on_sheet_open_changed(true);
  return true;
}

void ApiKeysPage::EndSheet() {
  if (!sheetShowing_) return;
  sheetShowing_ = false;
  if (on_sheet_open_changed) on_sheet_open_changed(false);
}

}  // namespace urnw
