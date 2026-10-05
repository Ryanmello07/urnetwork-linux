// Change Network API — point the client at a different network deployment
// (a self-hosted or forked server) instead of the official one, with optional
// explicit api/connect url overrides. The GTK port of the Windows
// NetworkServerSheet (itself iOS Shared/Views/NetworkServerSheet).
//
// Offered from the SIGNED-OUT screen only, as on iOS/Windows: switching
// servers swaps the LocalState and therefore the stored jwt, so it cannot be
// done underneath a live session.
//
// Its VLESS row opens the VLESS editor for the ACTIVE space -- the one the
// sign-in will use -- which saves on its own; Apply never touches it. Its
// bootstrap DNS-over-HTTPS row opens that space's servers the same way: on a
// network that blocks the built-in DoH servers, a sign-in needs them first.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <memory>
#include <string>

#include <gtkmm.h>

#include "SdkHost.hpp"

namespace urnw {

class ControlDohSheet;
class VlessSheet;

class NetworkServerSheet : public Gtk::Window {
 public:
  NetworkServerSheet(Gtk::Window& parent, SdkHost& sdk);
  ~NetworkServerSheet() override;

  // fired after a successful Apply (the sheet closes itself); the login flow
  // resets to the initial step on the new server
  std::function<void()> on_applied;

 private:
  void ApplyDerivedPlaceholders();  // preview the urls the host would produce
  void UpdateInsecureWarning();     // http:// / ws:// override -> amber advisory
  void Apply(const std::string& host, const std::string& apiUrl,
             const std::string& connectUrl);
  void UseDefault();
  void OpenVless();
  void OpenControlDoh();
  std::string DefaultHost() const;
  void SetStatus(const Glib::ustring& text, bool error);

  SdkHost& sdk_;
  SdkHost::NetworkServer current_;
  Gtk::Entry* hostBox_ = nullptr;
  Gtk::Entry* apiBox_ = nullptr;
  Gtk::Entry* connectBox_ = nullptr;
  Gtk::Label* insecureText_ = nullptr;
  Gtk::Label* statusText_ = nullptr;
  std::unique_ptr<VlessSheet> vlessSheet_;  // created on first use
  std::unique_ptr<ControlDohSheet> controlDohSheet_;  // created on first use
};

}  // namespace urnw
