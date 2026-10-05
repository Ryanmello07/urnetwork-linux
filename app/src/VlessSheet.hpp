// The VLESS settings editor: one VLESS server of the user's own that the
// client strategy also dials through when direct connections are blocked.
// Opened from Settings > Connections > VLESS and, before sign-in, from the
// login screen's "Change Network API" sheet.
//
// Both doors edit the ACTIVE network space through SdkHost: from Settings the
// signed-in space, from the login screen the space the next sign-in uses. The
// rules -- which rows show for a transport and a security, the form <->
// settings mapping, the messages -- are VlessPresentation.hpp's; reading and
// building links, validating and saving are the SDK's. This file renders.
//
// A modal sheet created on first use, like the other Settings sheets. It stays
// open after a save so the result line can say what happened: the VPN runs in
// urnetworkd, which takes the settings at its next tunnel start.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "SdkHost.hpp"
#include "VlessPresentation.hpp"

namespace urnw {

class VlessSheet : public Gtk::Window {
 public:
  VlessSheet(Gtk::Window& parent, SdkHost& host);
  ~VlessSheet() override;

  // Reads the space's settings into the form and presents the sheet. Every
  // open reads them again, so the form shows what the SDK stores now.
  void Open();

 private:
  // A labelled entry; `root` holds both, so hiding it hides the pair.
  struct Field {
    Gtk::Box* root = nullptr;
    Gtk::Entry* entry = nullptr;
  };
  // A labelled row of linked toggles over `options`; the selected value lives
  // in the member `value` points at, so a stored value no option carries
  // survives a save untouched instead of being replaced by a guess.
  struct Picker {
    Gtk::Box* root = nullptr;
    std::vector<vless::Option> options;
    std::vector<Gtk::ToggleButton*> buttons;
    std::string* value = nullptr;
  };

  Field AddField(Gtk::Box& host, const Glib::ustring& label);
  Picker AddPicker(Gtk::Box& host, const Glib::ustring& label,
                   std::vector<vless::Option> options, std::string* value);
  void SelectPicker(Picker& picker);
  void SelectFingerprint();

  void Load();
  void ApplyForm(const vless::Form& form);
  vless::Form ReadForm() const;
  urnet::VlessSettings CurrentSettings() const;
  void ApplyVisibility();

  void PasteLink();
  void ReadLink(const std::string& text);
  void CopyLink();
  void Save();

  // The line under the link field, and the two under Save (the result, then
  // the next-connect note). An error is the danger tone, else the green one.
  void ShowLinkMessage(const Glib::ustring& text, bool error);
  void ClearLinkMessage();
  void ShowSaveMessage(const Glib::ustring& message, const Glib::ustring& note, bool error);
  void ClearSaveMessage();
  // A user edit: the last save's verdict no longer describes the form.
  void OnEdited();

  SdkHost& host_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  // what the form was opened on (or the last link read into it): the base a
  // save writes over, which is what keeps the values the form never shows
  std::optional<urnet::VlessSettings> base_;
  bool applying_ = false;  // echo guard: ApplyForm's writes are not edits

  std::string network_ = vless::kNetworkTcp;
  std::string security_ = vless::kSecurityReality;
  std::string flow_ = vless::kFlowVision;
  std::string fingerprint_ = "chrome";

  Gtk::Box* form_ = nullptr;
  Gtk::Switch* enabled_ = nullptr;
  Gtk::Entry* link_ = nullptr;
  Gtk::Button* paste_ = nullptr;
  Gtk::Button* copy_ = nullptr;
  Gtk::Label* linkStatus_ = nullptr;
  Field name_;
  Field address_;
  Field port_;
  Field id_;
  Picker networkPicker_;
  Picker securityPicker_;
  Picker flowPicker_;
  Field serverName_;
  Gtk::Box* fingerprintRoot_ = nullptr;
  Gtk::DropDown* fingerprintBox_ = nullptr;
  std::vector<vless::Option> fingerprints_;
  Field alpn_;
  Gtk::Widget* insecureRow_ = nullptr;
  Gtk::Switch* insecure_ = nullptr;
  Field publicKey_;
  Field shortId_;
  Field path_;
  Field hostHeader_;
  Gtk::Label* status_ = nullptr;
  Gtk::Label* note_ = nullptr;
  Gtk::Button* save_ = nullptr;
};

}  // namespace urnw
