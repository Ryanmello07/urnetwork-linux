// The extender share sheet (connect/EXTENDER.md K7): the known extender
// addresses as a QR code another URnetwork app can scan, plus the same payload
// as copyable text.
//
// The payload itself is the SDK's -- ExtenderViewController::buildShare returns
// `ur-ext:1:` + base64url of the addresses (at most 48, active first, then
// usable, then manual), optionally with the settings block. Nothing here
// encodes or interprets it; this sheet renders what the SDK handed over and
// rebuilds it when the "include extender settings" switch moves.
//
// The code renders at error correction level H through the vendored Nayuki
// encoder (app/third_party/qrcodegen), with the connector glyph centred on a
// connector-shaped white plate and a 4 px outline around it. Level H tolerates
// roughly 30 % erasure and the glyph covers about 5 % of the area, so the
// code still scans -- measured by round-tripping both a 41-module and a
// 173-module render back through a decoder.
//
// The module size is snapped to WHOLE PIXELS and the code centred in what is
// left; the canvas grows with the module count. At a fractional module size
// cairo antialiases every module edge, and a dense code greys out into
// something no decoder can read -- also measured. See ExtenderShareSheet.cpp.
//
// The copyable text below the code is the fallback that depends on none of
// that, and it is why a payload too large to encode at all (48 addresses WITH
// settings can exceed even version 40-H) still leaves the sheet usable.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "ExtenderSharePresentation.hpp"
#include "SdkHost.hpp"

namespace urnw {

class ExtenderShareSheet : public Gtk::Window {
 public:
  ExtenderShareSheet(Gtk::Window& parent, SdkHost& host);

  // The page's snackbar: a copy that says nothing is indistinguishable from a
  // copy that did not happen, and this sheet owns no surface of its own.
  std::function<void(const Glib::ustring& message, bool error)> on_message;

 private:
  // Ask the SDK for a payload and repaint everything from it. SYNCHRONOUS --
  // buildShare is a local encode over the directory the device already holds,
  // not a round trip -- so there is no in-flight state and no watchdog here.
  void Rebuild();

  SdkHost& host_;

  // The encoded code, cached as a module bitmap: the encoder runs once per
  // payload rather than on every expose, and the header stays free of
  // qrcodegen. Row-major, moduleCount_ x moduleCount_; empty when the payload
  // did not encode.
  std::vector<bool> codeModules_;
  int moduleCount_ = 0;

  Gtk::DrawingArea* code_ = nullptr;
  Gtk::Label* count_ = nullptr;
  Gtk::Label* unavailable_ = nullptr;  // "no code to show": the SDK said nothing
  Gtk::TextView* payload_ = nullptr;
  Gtk::Button* copy_ = nullptr;
  Gtk::Switch* includeSettings_ = nullptr;

  extender::SharePresentation share_;
  // re-entry guard: Rebuild() can correct the switch, which fires the
  // handler that called it
  bool rebuilding_ = false;
};

}  // namespace urnw
