// ApiKeysPage — Account -> API keys: the account's API keys, reached from the
// Account pane's API keys row (right after Sessions) and left through its own
// "‹ Account", like the Sessions page. One pane:
//
//   header  API keys, progress while a refresh runs, refresh, "‹ Account"
//   intro   what a key is for and what it can do, and "How to use API keys"
//           (https://ur.io/docs/api)
//   create  Create API key, which opens the create sheet
//   notice  "Couldn't load your API keys. Try again." over a list kept
//   body    one row per key, newest first: the name ("Unnamed key" for a key
//           without one), when it was made and its ID, and a flat destructive
//           Delete; or the screen's state line (progress, empty, failed with
//           Retry, signed out)
//
// The create sheet asks for the key's name, sends it, and shows the key the
// server returns, the one time the server returns it, with Copy key and the
// warning that it won't be shown again and that anyone who has it can use the
// account. The key is held only while the sheet shows it (api_keys::CreateFlow)
// and wiped, from the sheet's label too, when the sheet closes; the sheet
// stays open while the request is out. It is never written to settings,
// files, logs or telemetry.
//
// Every delete is confirmed first in a modal that names the key and whose
// default is Cancel (as SessionsPage's sign-out).
//
// The requests go to the account's Api in this process (sdk listApiKeys,
// createApiKey, deleteApiKey). ApiKeysBinding.hpp carries their lifecycle and
// ApiKeysPresentation.hpp every decision; this file is the GTK. An answer
// arrives on an sdk thread and only posts to the main loop, where it is
// dropped unless the page is alive (alive_) and the request it answers is
// still pending in the binding.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "ApiKeysBinding.hpp"
#include "ApiKeysPresentation.hpp"
#include "PaneKit.hpp"
#include "SdkHost.hpp"

namespace urnw {

class ApiKeysPage : public Gtk::Box {
 public:
  explicit ApiKeysPage(SdkHost& host);
  ~ApiKeysPage() override;

  // Nav-select and auth change: the service on the account's Api (one per
  // Api: a replaced Api gets a new one), and the keys listed again. Signed
  // out, the no-session state, and no request.
  void Load();
  // A sign-out: the service is released, and the departed account's keys, the
  // create sheet (with any key it shows) and any open confirmation go with it.
  void ResetForSignOut();
  // --preview-ui: no service; the page settles on its no-session state.
  void SetPreviewMode(bool on);
  void ShowPreviewState();

  std::function<void()> on_back;  // the header's "‹ Account"
  // The window's one-modal-at-a-time gate, both edges reported.
  std::function<bool()> sheet_open;
  std::function<void(bool open)> on_sheet_open_changed;
  std::function<void(const Glib::ustring& message, bool error)> on_snackbar;

 private:
  // One key's row. Its callbacks name the key by id, never by name.
  struct RowWidgets {
    std::string keyId;
    Gtk::Widget* root = nullptr;
    Gtk::Label* title = nullptr;
    Gtk::Label* created = nullptr;
    Gtk::Label* id = nullptr;
    Gtk::Box* status = nullptr;  // progress or the failure, under the lines
    Gtk::Spinner* spinner = nullptr;
    Gtk::Label* statusText = nullptr;
    Gtk::Button* del = nullptr;
  };

  // The create sheet's widgets, built as it opens.
  struct CreateSheet {
    Gtk::Label* heading = nullptr;
    Gtk::Box* nameStep = nullptr;
    Gtk::Entry* name = nullptr;
    Gtk::Label* failure = nullptr;
    Gtk::Spinner* spinner = nullptr;
    Gtk::Button* cancel = nullptr;
    Gtk::Button* create = nullptr;
    Gtk::Box* keyStep = nullptr;
    Gtk::Label* key = nullptr;
    Gtk::Button* copy = nullptr;
    Gtk::Button* done = nullptr;
  };

  void BuildPane();
  bool CanCallApi();
  api_keys::Words MakeWords() const;
  void EnsureService();
  void Render();
  void RenderRows(const api_keys::Words& words);
  RowWidgets BuildRow(const std::string& keyId);
  void UpdateRow(RowWidgets& row, const api_keys::Row& text, api_keys::ActionView view);
  void Retry();
  void OpenDocs();

  void OpenCreateSheet();
  void SubmitCreate();
  void SettleCreate(bool ok, std::string apiKey);
  void RenderCreateSheet();
  void CopyCreatedKey();
  void CloseCreateSheet(bool force);
  void CreateSheetHidden();

  void PressDelete(const std::string& keyId);
  void ShowDeleteConfirmation(const api_keys::DeleteConfirmation& confirmation);
  void RestoreFocus(const std::string& keyId);
  void Snack(const Glib::ustring& message, bool error);

  Gtk::Window* RootWindow();
  bool BeginSheet(const char* what);
  void EndSheet();

  SdkHost& host_;
  const api_keys::Settings settings_ = api_keys::DefaultSettings();
  // marshaled work's liveness: a post or a timer that lands after the page is
  // gone does nothing. Before binding_, which its give-up timers check it.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  api_keys::Binding binding_;
  api_keys::DeleteFlow deleteFlow_;
  api_keys::CreateFlow createFlow_;
  bool previewMode_ = false;
  bool sheetShowing_ = false;  // this page's half of the one-modal gate

  kit::Pane pane_;
  Gtk::Spinner* refreshSpinner_ = nullptr;
  Gtk::Button* refreshButton_ = nullptr;
  Gtk::Button* createButton_ = nullptr;
  Gtk::Widget* noticeRow_ = nullptr;
  Gtk::Box* stateBox_ = nullptr;
  Gtk::Spinner* stateSpinner_ = nullptr;
  Gtk::Label* stateText_ = nullptr;
  Gtk::Button* retryButton_ = nullptr;
  Gtk::Box* rowsBox_ = nullptr;
  std::vector<RowWidgets> rows_;
  std::unique_ptr<Gtk::Window> createDialog_;
  CreateSheet sheet_;
  sigc::connection copiedTimer_;
  std::unique_ptr<Gtk::Window> confirmDialog_;
};

}  // namespace urnw
