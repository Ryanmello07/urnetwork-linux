// Earnings › Top 200 › Sign fleet binding (support inbox 7934; factury
// support/plans/top200-in-app-signing.md): this device signs its own Top 200
// fleet binding with its Ed25519 client key.
//
// Top to bottom: this device's client id and client key (each copyable) and
// its fleet manifest member entry; the binding field with Paste; once the
// pasted text is a binding of this device, the review (fleet id, hotkey,
// chain and subnet, epochs); Sign with this device; then the
// {binding, client_signature} payload with Copy payload.
//
// The rules live in Top200Binding.hpp. The sheet reads the identity and signs
// through SdkHost's fleet worker (the existing device rpc to the daemon's
// DeviceLocal), never on the GTK loop. It signs only on the user's tap, only a
// binding whose client_id and client_key are this device's, and only after the
// review is on screen. Nothing goes over the network: the user copies the
// payload back to the page. The binding and payload are never logged.
//
// The app link urnetwork://top200/sign pre-fills the field (Prefill) and runs
// the same check as a paste; it never signs.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <gtkmm.h>

#include "SdkHost.hpp"
#include "Top200Binding.hpp"

namespace urnw {

class Top200SignSheet : public Gtk::Window {
 public:
  explicit Top200SignSheet(SdkHost& host);
  ~Top200SignSheet() override;

  // The app link's binding, or (nullopt) a link whose binding did not decode,
  // which shows top200_sign_error_invalid over an empty field.
  void Prefill(const std::optional<std::string>& binding);

 private:
  void RequestIdentity();
  void OnIdentity(const std::string& clientId, const std::string& clientKey);
  void PasteFromClipboard();
  void OnTextChanged();
  void Revalidate();
  void OnSign();
  void ShowError(const Glib::ustring& message);
  void UpdateSignButton();
  void FlashCopied(Gtk::Button& button, const Glib::ustring& label, sigc::connection& reset);

  SdkHost& host_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  bool identityLoaded_ = false;
  std::string clientId_;
  std::string clientKey_;
  // the link's binding did not decode: shown until the field is edited
  bool linkInvalid_ = false;
  std::optional<top200::BindingCheck> check_;
  bool signing_ = false;
  // the trimmed text the in-flight sign was asked for: a reply for any other
  // text (the field was edited meanwhile) is dropped
  std::string signingText_;
  bool settingText_ = false;

  Gtk::Label* clientIdValue_ = nullptr;
  Gtk::Label* clientKeyValue_ = nullptr;
  Gtk::Label* deviceError_ = nullptr;
  Gtk::Button* copyMember_ = nullptr;
  Gtk::TextView* text_ = nullptr;
  Gtk::Label* errorLine_ = nullptr;
  Gtk::Box* reviewBox_ = nullptr;
  Gtk::Label* fleetIdValue_ = nullptr;
  Gtk::Label* hotkeyValue_ = nullptr;
  Gtk::Label* chainValue_ = nullptr;
  Gtk::Label* epochsValue_ = nullptr;
  Gtk::Button* signButton_ = nullptr;
  Gtk::Spinner* spinner_ = nullptr;
  Gtk::Box* doneBox_ = nullptr;
  Gtk::Label* payloadValue_ = nullptr;
  Gtk::Button* copyPayload_ = nullptr;
  std::string payload_;
  sigc::connection memberCopiedReset_;
  sigc::connection payloadCopiedReset_;
  sigc::connection identityRetry_;
};

}  // namespace urnw
