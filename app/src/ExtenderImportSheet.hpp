// The extender import sheet (connect/EXTENDER.md K7/K8): take a share code
// from an image file or from pasted text, show what it carries, and apply it.
//
// K8 for this platform: "Windows and linux: the code renders through a
// vendored single-file encoder, import reads an image file through zxing-cpp
// plus pasted text, NO CAMERA." So there are exactly two ways in, and the
// image path degrades to the text path when the build has no zxing.
//
// Every decision -- whether the code can be imported, whether the settings
// switch appears, which message shows, whether the import confirms first -- is
// the pure function in ExtenderSharePresentation.hpp, against the SDK's
// decode_share result. This sheet only renders it and calls import_share.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <gtkmm.h>

#include "ExtenderSharePresentation.hpp"
#include "SdkHost.hpp"

namespace urnw {

class ExtenderImportSheet : public Gtk::Window {
 public:
  ExtenderImportSheet(Gtk::Window& parent, SdkHost& host);
  ~ExtenderImportSheet() override;

  // Reports the outcome to the page's snackbar. error=true is the persistent
  // treatment -- an import failure is often the only diagnostic there is.
  std::function<void(const Glib::ustring& message, bool error)> on_message;
  // Fires after an import actually applied, so the section can re-read the
  // settings the code may have replaced.
  std::function<void()> on_imported;

 private:
  void ChooseImageFile();
  void PasteFromClipboard();
  void SetPayload(const std::string& text);  // decode and repaint
  void Refresh();                            // repaint from view_
  void StartImport();
  void RunImport();
  void ConfirmThenImport();

  SdkHost& host_;
  // liveness token for the file-dialog and clipboard completions, which can
  // land after this sheet is gone
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  Gtk::Button* chooseFile_ = nullptr;
  Gtk::Button* paste_ = nullptr;
  Gtk::TextView* text_ = nullptr;
  Gtk::Label* count_ = nullptr;
  Gtk::Label* message_ = nullptr;
  Gtk::Widget* useSettingsRow_ = nullptr;
  Gtk::Switch* useSettings_ = nullptr;
  Gtk::Button* import_ = nullptr;
  std::unique_ptr<Gtk::Window> confirm_;

  std::string payload_;
  // The SDK's answer for `decodedFor_`, cached: decode_share is a device rpc,
  // and flipping the settings switch repaints without changing the payload.
  std::optional<urnet::ExtenderShareDecodeResult> decoded_;
  std::string decodedFor_;
  extender::ImportPresentation view_;
  bool settingText_ = false;  // re-entry guard on the text buffer
  bool refreshing_ = false;   // re-entry guard on the settings switch
};

}  // namespace urnw
