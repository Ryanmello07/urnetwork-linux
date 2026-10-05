// The bootstrap DNS-over-HTTPS servers of the ACTIVE network space (sdk
// control_doh_ui.go): `https://<ip literal>/<path>` servers the space's own
// names (api, connect, extender) are looked up through ahead of the built-in
// ones, for a network that blocks those -- mainland China blocks all four. The
// servers see those lookups, which the description says, always on screen.
//
// Two doors show the same section. Account > Extenders shows it inline, under
// the extender settings. The login screen's "Change Network API" sheet opens it
// in a sheet of its own (ControlDohSheet), before sign-in: a fresh install
// behind such a network cannot resolve the api to sign in at all, so that door
// is the one it needs. Either way it edits the ACTIVE space through SdkHost,
// with or without a tunnel: the servers are a value of the GUI's own space,
// which urnetworkd imports at its next tunnel start.
//
// Top to bottom, as on every platform: the description, one server per line,
// the hint, "Use China resolvers" (fills the box from the SDK's preset without
// saving) with its hint, then "Use built-in servers only" (clears and saves)
// and Save. The rules -- how the box splits, the messages, the preset's country
// -- are ExtenderSharePresentation.hpp's; validating and saving are the SDK's.
// This file renders. The SDK calls are synchronous, as the extender form's and
// the VLESS sheet's are: a save swaps the space's DoH cache in place, which
// cancels the requests in flight before it waits for them.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

#include <gtkmm.h>

#include "ExtenderSharePresentation.hpp"
#include "SdkHost.hpp"

namespace urnw {

class ControlDohSection : public Gtk::Box {
 public:
  // `withTitle`: the section draws its own title, as it does in Account; a
  // sheet with a heading of its own passes false.
  ControlDohSection(SdkHost& host, bool withTitle);

  // Reads the space's servers into the box (again: every Load reads what the
  // SDK stores now).
  void Load();

 private:
  void UseChinaResolvers();
  void Save(const std::vector<std::string>& urls);
  // The box's text, without its echo counting as an edit.
  void SetBox(const std::string& text);
  void ApplyEnabled();
  // The two lines under Save: the result, then the next-connect note. An error
  // is the danger tone, else the green one.
  void ShowResult(const Glib::ustring& message, const Glib::ustring& note, bool error);
  void ClearResult();

  SdkHost& host_;
  // the servers could be read, so there is something to edit and save
  bool loaded_ = false;
  bool applying_ = false;  // echo guard: SetBox's writes are not edits

  Gtk::TextView* urls_ = nullptr;
  Gtk::Button* china_ = nullptr;
  Gtk::Button* reset_ = nullptr;
  Gtk::Button* save_ = nullptr;
  Gtk::Label* status_ = nullptr;
  Gtk::Label* note_ = nullptr;
};

// The login screen's door: the section in a modal sheet of its own, created on
// first use like the VLESS sheet beside it. Every Open reads the servers again.
class ControlDohSheet : public Gtk::Window {
 public:
  ControlDohSheet(Gtk::Window& parent, SdkHost& host);
  ~ControlDohSheet() override;

  void Open();

 private:
  ControlDohSection* section_ = nullptr;
};

}  // namespace urnw
