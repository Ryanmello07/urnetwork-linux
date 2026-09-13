// SPDX-License-Identifier: MPL-2.0
#include "ExtenderImportSheet.hpp"

#include <cstdint>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

#include <glib.h>
#include <gtk/gtk.h>

#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

#if defined(UR_HAVE_ZXING)
#include <ZXing/ReadBarcode.h>
#endif

namespace urnw {
namespace {

// The English source each message key carries, so the lookup and its fallback
// stay together. The key itself comes out of the pure presentation (it may be
// the SDK's own error key), which is why this is a table rather than a switch.
const char* MessageText(const std::string& key) {
  if (key == extender::kImportErrorInvalid) {
    return T_("import_extenders_invalid", "This code is not an extender share.");
  }
  if (key == extender::kImportErrorForeignHost) {
    return T_("import_extenders_foreign_host",
              "This code is for {}, not this network. Use extender settings to switch networks.");
  }
  if (key == extender::kImportConfirmSettings) {
    return T_("import_extenders_confirm_settings",
              "Use the extender settings from this code? Extender lookups will use {}.");
  }
  // An error key this build does not know is still a refusal, and saying so is
  // better than showing the raw key id to a user.
  return T_("import_extenders_invalid", "This code is not an extender share.");
}

#if defined(UR_HAVE_ZXING)
// Reads the first QR code out of an image file. nullopt covers both "the file
// is not an image this build can decode" and "there is no QR code in it" --
// the caller says `qr_code_not_found` either way, which is the true statement
// from the user's side.
std::optional<std::string> DecodeQrFromFile(const std::string& path) {
  Glib::RefPtr<Gdk::Pixbuf> pixbuf;
  try {
    pixbuf = Gdk::Pixbuf::create_from_file(path);
  } catch (const Glib::Error& e) {
    g_warning("extender import: cannot read '%s': %s", path.c_str(), e.what());
    return std::nullopt;
  } catch (const std::exception& e) {
    g_warning("extender import: cannot read '%s': %s", path.c_str(), e.what());
    return std::nullopt;
  }
  if (!pixbuf) return std::nullopt;

  const int channels = pixbuf->get_n_channels();
  if (channels != 3 && channels != 4) return std::nullopt;
  // ImageFormat::RGB with an EXPLICIT pixel stride, which is what reads an
  // RGBA pixbuf correctly: the format says "R at +0, G at +1, B at +2" and the
  // stride says how far to the next pixel, so a 4-channel buffer is read as
  // RGB with the alpha byte stepped over. The named RGBA/RGBX constants are
  // deliberately avoided -- zxing-cpp 2.2 spells it RGBX and 2.3 deprecates
  // that in favour of RGBA, and this build has to compile against both.
  const ZXing::ImageView image(reinterpret_cast<const uint8_t*>(pixbuf->get_pixels()),
                               pixbuf->get_width(), pixbuf->get_height(),
                               ZXing::ImageFormat::RGB, pixbuf->get_rowstride(), channels);
  ZXing::ReaderOptions options;
  options.setFormats(ZXing::BarcodeFormat::QRCode);
  // a photo of a screen is rotated, skewed and often inverted by a dark theme
  options.setTryHarder(true);
  options.setTryRotate(true);
  options.setTryInvert(true);
  try {
    for (const auto& result : ZXing::ReadBarcodes(image, options)) {
      if (result.isValid() && !result.text().empty()) return result.text();
    }
  } catch (const std::exception& e) {
    g_warning("extender import: decode failed: %s", e.what());
  }
  return std::nullopt;
}
#endif

}  // namespace

ExtenderImportSheet::ExtenderImportSheet(Gtk::Window& parent, SdkHost& host) : host_(host) {
  set_transient_for(parent);
  set_modal(true);
  set_title(T_("import_extenders", "Import extenders"));
  set_default_size(440, -1);
  set_resizable(false);
  add_css_class("ur-sheet");

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->set_margin(24);

  auto* heading = Gtk::make_managed<Gtk::Label>(T_("import_extenders", "Import extenders"));
  heading->add_css_class("ur-step-heading");
  heading->set_xalign(0);
  box->append(*heading);

  // the two ways in: an image file, or the payload as text (no camera on this
  // platform -- K8)
  auto* sources = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  chooseFile_ = Gtk::make_managed<Gtk::Button>(T_("choose_image_file", "Choose image file"));
  chooseFile_->add_css_class("ur-btn");
  chooseFile_->add_css_class("ur-btn-secondary");
  chooseFile_->signal_clicked().connect([this] { ChooseImageFile(); });
#if !defined(UR_HAVE_ZXING)
  // Built without zxing-cpp: there is no decoder, so the affordance is absent
  // rather than present and permanently failing. Pasted text still works, and
  // it is the path that never depended on a library.
  chooseFile_->set_visible(false);
#endif
  sources->append(*chooseFile_);
  paste_ = Gtk::make_managed<Gtk::Button>(T_("paste_share_text", "Paste share text"));
  paste_->add_css_class("ur-btn");
  paste_->add_css_class("ur-btn-secondary");
  paste_->signal_clicked().connect([this] { PasteFromClipboard(); });
  sources->append(*paste_);
  box->append(*sources);

  // ...and the payload itself, editable, so a code that arrived by any other
  // route can simply be typed or dropped in
  text_ = Gtk::make_managed<Gtk::TextView>();
  text_->set_wrap_mode(Gtk::WrapMode::CHAR);
  text_->set_monospace(true);
  text_->add_css_class("ur-card-bordered");
  kit::SetAccessibleLabel(*text_, T_("paste_share_text", "Paste share text"));
  text_->get_buffer()->signal_changed().connect([this] {
    if (settingText_) return;
    SetPayload(text_->get_buffer()->get_text().raw());
  });
  auto* textScroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  textScroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  textScroll->set_min_content_height(72);
  textScroll->set_child(*text_);
  box->append(*textScroll);

  count_ = Gtk::make_managed<Gtk::Label>();
  count_->set_xalign(0);
  count_->set_visible(false);
  box->append(*count_);

  message_ = Gtk::make_managed<Gtk::Label>();
  message_->set_xalign(0);
  message_->set_wrap(true);
  message_->set_visible(false);
  box->append(*message_);

  auto useSettingsRow =
      kit::MakePaneTwoLineRow(T_("use_extender_settings", "Use extender settings"));
  useSettings_ = Gtk::make_managed<Gtk::Switch>();
  useSettings_->set_valign(Gtk::Align::CENTER);
  kit::SetAccessibleLabel(*useSettings_, T_("use_extender_settings", "Use extender settings"));
  useSettingsRow.trailing->append(*useSettings_);
  useSettings_->property_active().signal_changed().connect([this] { Refresh(); });
  useSettingsRow_ = useSettingsRow.root;
  useSettingsRow_->set_visible(false);
  box->append(*useSettingsRow_);

  auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  actions->set_halign(Gtk::Align::END);
  auto* cancel = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  cancel->add_css_class("ur-btn");
  cancel->add_css_class("ur-btn-secondary");
  cancel->signal_clicked().connect([this] { set_visible(false); });
  actions->append(*cancel);
  import_ = Gtk::make_managed<Gtk::Button>(T_("import_extenders", "Import extenders"));
  import_->add_css_class("ur-btn");
  import_->set_sensitive(false);
  import_->signal_clicked().connect([this] { StartImport(); });
  actions->append(*import_);
  box->append(*actions);

  set_child(*box);
  Refresh();
}

ExtenderImportSheet::~ExtenderImportSheet() { *alive_ = false; }

void ExtenderImportSheet::ChooseImageFile() {
#if defined(UR_HAVE_ZXING)
  GtkFileDialog* dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, T_("choose_image_file", "Choose image file"));
  // Images, by mime type rather than by gtk_file_filter_add_pixbuf_formats():
  // that helper is deprecated from GTK 4.20 and this tree still builds against
  // 4.14, so a wildcard mime filter is the spelling that is correct on both.
  // The decoder accepts whatever gdk-pixbuf can load, which is a superset.
  GtkFileFilter* filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, T_("choose_image_file", "Choose image file"));
  gtk_file_filter_add_mime_type(filter, "image/*");
  GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);
  g_object_unref(filter);

  struct Payload {
    ExtenderImportSheet* sheet;
    std::shared_ptr<bool> alive;
  };
  auto* payload = new Payload{this, alive_};
  gtk_file_dialog_open(
      dialog, GTK_WINDOW(gobj()), nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer data) {
        std::unique_ptr<Payload> owned(static_cast<Payload*>(data));
        GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, nullptr);
        if (file == nullptr) return;  // dismissed: a cancel is never a failure
        gchar* path = g_file_get_path(file);
        const std::string chosen = path != nullptr ? path : "";
        g_free(path);
        g_object_unref(file);
        if (!*owned->alive) return;
        // A file with no local path (a portal handing back a URI this process
        // cannot open) reads the same as a file with no code in it.
        const std::optional<std::string> decoded =
            chosen.empty() ? std::optional<std::string>() : DecodeQrFromFile(chosen);
        if (!decoded) {
          owned->sheet->SetPayload("");
          owned->sheet->message_->set_text(
              T_("qr_code_not_found", "No QR code was found in the image."));
          owned->sheet->message_->set_visible(true);
          return;
        }
        owned->sheet->SetPayload(*decoded);
      },
      payload);
  g_object_unref(dialog);
#endif
}

void ExtenderImportSheet::PasteFromClipboard() {
  auto clipboard = get_clipboard();
  auto alive = alive_;
  clipboard->read_text_async([this, alive, clipboard](Glib::RefPtr<Gio::AsyncResult>& result) {
    Glib::ustring text;
    try {
      text = clipboard->read_text_finish(result);
    } catch (const Glib::Error& e) {
      g_warning("extender import: clipboard read failed: %s", e.what());
      return;
    }
    if (!*alive) return;
    SetPayload(text.raw());
  });
}

void ExtenderImportSheet::SetPayload(const std::string& text) {
  payload_ = text;
  // Keep the box in step when the text came from a file or the clipboard,
  // without re-entering this through the buffer's changed signal.
  if (text_->get_buffer()->get_text().raw() != text) {
    settingText_ = true;
    text_->get_buffer()->set_text(text);
    settingText_ = false;
  }
  Refresh();
}

void ExtenderImportSheet::Refresh() {
  // Correcting the settings switch below fires the handler that called this.
  // Bounded at one level anyway, but decode_share is a locked call into the
  // SDK and doing it twice per repaint is waste, not correctness.
  if (refreshing_) return;
  refreshing_ = true;
  // An empty box is "waiting", not "invalid": a user who has not pasted
  // anything yet has not made a mistake. The answer is cached per payload --
  // decode_share is a device rpc, and the settings switch repaints without
  // changing a byte of the code.
  if (payload_ != decodedFor_) {
    decoded_ = payload_.empty() ? std::optional<urnet::ExtenderShareDecodeResult>()
                                : host_.DecodeExtenderShare(payload_);
    decodedFor_ = payload_;
  }
  const std::optional<urnet::ExtenderShareDecodeResult>& decoded = decoded_;
  view_ = extender::ImportPresentationFor(
      decoded.has_value(), decoded ? decoded->Ok : false,
      decoded ? decoded->Error : std::string(), decoded ? decoded->NetworkHost : std::string(),
      decoded ? decoded->ForeignHost : false, decoded ? decoded->Count : 0,
      decoded ? decoded->HasSettings : false,
      decoded ? decoded->SettingsHost : std::string(), useSettings_->get_active());

  count_->set_visible(view_.showCount);
  if (view_.showCount) {
    count_->set_text(Format(TN_("share_extenders_count", "{} extender", "{} extenders",
                                static_cast<unsigned long>(view_.count)),
                            view_.count));
  }
  const bool haveMessage = !view_.messageKey.empty();
  message_->set_visible(haveMessage);
  if (haveMessage) {
    message_->set_text(Format(MessageText(view_.messageKey), view_.messageArg));
  }
  useSettingsRow_->set_visible(view_.showUseSettings);
  if (!view_.showUseSettings && useSettings_->get_active()) {
    // a code with no settings block must not leave the switch armed for the
    // next code pasted into the same sheet
    useSettings_->set_active(false);
  }
  import_->set_sensitive(view_.canImport);
  refreshing_ = false;
}

void ExtenderImportSheet::StartImport() {
  if (!view_.canImport) return;
  if (view_.confirmBeforeImport) {
    ConfirmThenImport();
    return;
  }
  RunImport();
}

void ExtenderImportSheet::ConfirmThenImport() {
  // Rule 4 of this app's confirmations: the default control is Cancel and the
  // committing word appears only on the committing control. Replacing the dns
  // name, gossip url and root keys is what is being agreed to here.
  confirm_ = std::make_unique<Gtk::Window>();
  confirm_->set_transient_for(*this);
  confirm_->set_modal(true);
  confirm_->set_title(T_("use_extender_settings", "Use extender settings"));
  confirm_->set_resizable(false);
  confirm_->add_css_class("ur-sheet");

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->set_margin(24);
  auto* line = Gtk::make_managed<Gtk::Label>(
      Format(T_("import_extenders_confirm_settings",
                "Use the extender settings from this code? Extender lookups will use {}."),
             view_.messageArg));
  line->set_wrap(true);
  line->set_xalign(0);
  box->append(*line);

  auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  actions->set_halign(Gtk::Align::END);
  auto* cancel = Gtk::make_managed<Gtk::Button>(T_("cancel", "Cancel"));
  cancel->add_css_class("ur-btn");
  cancel->add_css_class("ur-btn-secondary");
  cancel->signal_clicked().connect([this] { confirm_->set_visible(false); });
  actions->append(*cancel);
  auto* apply = Gtk::make_managed<Gtk::Button>(T_("use_extender_settings", "Use extender settings"));
  apply->add_css_class("ur-btn");
  apply->signal_clicked().connect([this] {
    confirm_->set_visible(false);
    RunImport();
  });
  actions->append(*apply);
  box->append(*actions);

  confirm_->set_child(*box);
  cancel->grab_focus();
  confirm_->set_visible(true);
}

void ExtenderImportSheet::RunImport() {
  const auto result = host_.ImportExtenderShare(payload_, useSettings_->get_active());
  if (!result || !result->Ok) {
    const std::string key = (result && !result->Error.empty()) ? result->Error
                                                               : extender::kImportErrorInvalid;
    const Glib::ustring text = Format(MessageText(key), view_.messageArg);
    message_->set_text(text);
    message_->set_visible(true);
    if (on_message) on_message(text, true);
    return;
  }
  const int64_t imported = result->ImportedCount;
  const Glib::ustring text = Format(TN_("import_extenders_imported", "Imported {} extender",
                                        "Imported {} extenders",
                                        static_cast<unsigned long>(imported)),
                                    imported);
  if (on_message) on_message(text, false);
  // the code may have replaced the dns name, gossip url and root keys, so the
  // form behind this sheet is stale the moment an import with settings lands
  if (on_imported) on_imported();
  set_visible(false);
}

}  // namespace urnw
