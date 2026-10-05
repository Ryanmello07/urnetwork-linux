// SPDX-License-Identifier: MPL-2.0
#include "VlessSheet.hpp"

#include <glib.h>
#include <gtk/gtk.h>

#include <string>
#include <utility>

#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"
#include "UrTheme.hpp"

namespace urnw {
namespace {

constexpr int kSheetWidth = 460;
constexpr int kSheetHeight = 680;

// A presentation text through the catalog; an empty key is a value shown as it
// is (the fingerprints' technical names).
Glib::ustring Tr(const vless::Text& text) {
  if (text.key.empty()) return Glib::ustring(std::string(text.english));
  return g_dpgettext2(GETTEXT_PACKAGE, std::string(text.key).c_str(),
                      std::string(text.english).c_str());
}

Gtk::Label* MakeFieldLabel(const Glib::ustring& text) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->add_css_class("ur-input-label");
  label->set_xalign(0);
  return label;
}

// A wrapping line that is hidden until something is said on it. A result line
// is a PLAIN label, like the Extenders section's status: its tone comes from
// kit::ApplySupportingText, and a .ur-caption under it would win the cascade
// over the green .ur-value-on (the two live in different providers).
Gtk::Label* MakeMessageLine(const char* cssClass = nullptr) {
  auto* line = Gtk::make_managed<Gtk::Label>();
  if (cssClass) line->add_css_class(cssClass);
  line->set_xalign(0);
  line->set_wrap(true);
  line->set_visible(false);
  return line;
}

}  // namespace

VlessSheet::VlessSheet(Gtk::Window& parent, SdkHost& host) : host_(host) {
  // The login screen opens this before any page has run: the sheet carries
  // its own vocabulary rather than borrowing a page's.
  EnsureBrandCss();   // .ur-sheet / .ur-step-heading / .ur-caption / the inputs
  EnsureDrawerCss();  // .ur-value-on: the green of the saved and copied lines
  set_transient_for(parent);
  set_modal(true);
  set_title(T_("vless", "VLESS"));
  set_default_size(kSheetWidth, kSheetHeight);
  set_hide_on_close(true);
  add_css_class("ur-sheet");  // sheets sit ABOVE the page: #151515
  AddEscapeToClose(*this);

  auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroller->set_vexpand(true);
  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->set_margin(24);

  auto* heading = Gtk::make_managed<Gtk::Label>(T_("vless", "VLESS"));
  heading->add_css_class("ur-step-heading");
  heading->set_xalign(0);
  box->append(*heading);

  auto* description = Gtk::make_managed<Gtk::Label>(
      T_("vless_settings_description",
         "Connect to URnetwork through your own VLESS server when direct connections are "
         "blocked. Your traffic to URnetwork stays encrypted end to end."));
  description->add_css_class("ur-caption");
  description->set_xalign(0);
  description->set_wrap(true);
  box->append(*description);

  // Everything under the description is the form, so one switch makes it
  // insensitive when there is no space to edit.
  form_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  box->append(*form_);

  // The switch is otherwise NAMELESS (its on/off content is empty), so it
  // takes the row label as its accessible name.
  {
    auto row = kit::MakePaneTwoLineRow(T_("vless_enabled", "Use VLESS"));
    enabled_ = Gtk::make_managed<Gtk::Switch>();
    enabled_->set_valign(Gtk::Align::CENTER);
    kit::SetAccessibleLabel(*enabled_, T_("vless_enabled", "Use VLESS"));
    enabled_->property_active().signal_changed().connect([this] { OnEdited(); });
    row.trailing->append(*enabled_);
    form_->append(*row.root);
  }

  // The share link: a pasted link replaces the whole form; Copy builds one
  // from it.
  {
    auto* group = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    group->append(*MakeFieldLabel(T_("vless_link", "VLESS link")));
    link_ = Gtk::make_managed<Gtk::Entry>();
    link_->add_css_class("ur-input");
    kit::SetAccessibleLabel(*link_, T_("vless_link", "VLESS link"));
    link_->signal_activate().connect([this] { ReadLink(link_->get_text().raw()); });
    link_->signal_changed().connect([this] { ClearLinkMessage(); });
    group->append(*link_);
    auto* hint = Gtk::make_managed<Gtk::Label>(
        T_("vless_link_hint", "Paste a vless:// link to fill in the settings."));
    hint->add_css_class("ur-caption");
    hint->add_css_class("dim-label");
    hint->set_xalign(0);
    hint->set_wrap(true);
    group->append(*hint);

    auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    actions->set_margin_top(4);
    paste_ = Gtk::make_managed<Gtk::Button>(T_("vless_paste_link", "Paste link"));
    paste_->signal_clicked().connect([this] { PasteLink(); });
    actions->append(*paste_);
    copy_ = Gtk::make_managed<Gtk::Button>(T_("vless_copy_link", "Copy link"));
    copy_->signal_clicked().connect([this] { CopyLink(); });
    actions->append(*copy_);
    group->append(*actions);

    linkStatus_ = MakeMessageLine();
    group->append(*linkStatus_);
    form_->append(*group);
  }

  name_ = AddField(*form_, T_("name_label", "Name"));
  address_ = AddField(*form_, T_("vless_server_address", "Server address"));
  port_ = AddField(*form_, T_("vless_port", "Port"));
  port_.entry->set_input_purpose(Gtk::InputPurpose::DIGITS);
  port_.entry->set_max_length(5);
  id_ = AddField(*form_, T_("vless_user_id", "User ID (UUID)"));

  networkPicker_ =
      AddPicker(*form_, T_("transport", "Transport"), vless::NetworkOptions(), &network_);
  securityPicker_ =
      AddPicker(*form_, T_("vless_security", "Security"), vless::SecurityOptions(), &security_);
  // offered only with tcp and a security (VlessPresentation.hpp VisibilityFor)
  flowPicker_ = AddPicker(*form_, T_("vless_flow", "Flow"), vless::FlowOptions(), &flow_);

  // tls and reality
  serverName_ = AddField(*form_, T_("vless_server_name", "Server name (SNI)"));
  {
    fingerprints_ = vless::FingerprintOptions();
    std::vector<Glib::ustring> labels;
    labels.reserve(fingerprints_.size());
    for (const auto& option : fingerprints_) labels.push_back(Tr(option.label));
    fingerprintRoot_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    fingerprintRoot_->append(*MakeFieldLabel(T_("vless_fingerprint", "TLS fingerprint")));
    fingerprintBox_ = Gtk::make_managed<Gtk::DropDown>(labels);
    fingerprintBox_->set_halign(Gtk::Align::START);
    kit::SetAccessibleLabel(*fingerprintBox_, T_("vless_fingerprint", "TLS fingerprint"));
    fingerprintBox_->property_selected().signal_changed().connect([this] {
      if (applying_) return;
      const guint selected = fingerprintBox_->get_selected();
      if (selected < fingerprints_.size()) {
        fingerprint_ = std::string(fingerprints_[selected].value);
      }
      OnEdited();
    });
    fingerprintRoot_->append(*fingerprintBox_);
    form_->append(*fingerprintRoot_);
  }

  // tls
  alpn_ = AddField(*form_, T_("vless_alpn", "ALPN"));
  {
    auto row = kit::MakePaneTwoLineRow(
        T_("vless_allow_insecure", "Allow an insecure certificate"));
    insecure_ = Gtk::make_managed<Gtk::Switch>();
    insecure_->set_valign(Gtk::Align::CENTER);
    kit::SetAccessibleLabel(*insecure_,
                            T_("vless_allow_insecure", "Allow an insecure certificate"));
    insecure_->property_active().signal_changed().connect([this] { OnEdited(); });
    row.trailing->append(*insecure_);
    insecureRow_ = row.root;
    form_->append(*row.root);
  }

  // reality
  publicKey_ = AddField(*form_, T_("vless_public_key", "Public key"));
  shortId_ = AddField(*form_, T_("vless_short_id", "Short ID"));

  // ws and httpupgrade
  path_ = AddField(*form_, T_("vless_path", "Path"));
  hostHeader_ = AddField(*form_, T_("vless_host_header", "Host header"));

  scroller->set_child(*box);
  root->append(*scroller);

  // Save and the line that answers it stay in view however far the form is
  // scrolled: the answer belongs next to the button that asked.
  root->append(*kit::MakeDivider());
  auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  footer->set_margin_start(24);
  footer->set_margin_end(24);
  footer->set_margin_top(12);
  footer->set_margin_bottom(24);
  status_ = MakeMessageLine();
  footer->append(*status_);
  note_ = MakeMessageLine("ur-caption");
  note_->add_css_class("dim-label");
  footer->append(*note_);
  auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  actions->set_halign(Gtk::Align::END);
  actions->set_margin_top(8);
  auto* close = Gtk::make_managed<Gtk::Button>(T_("close", "Close"));
  close->signal_clicked().connect([this] { set_visible(false); });
  actions->append(*close);
  save_ = Gtk::make_managed<Gtk::Button>(T_("save", "Save"));
  save_->add_css_class("suggested-action");  // the dialog-primary role
  save_->signal_clicked().connect([this] { Save(); });
  actions->append(*save_);
  footer->append(*actions);
  root->append(*footer);

  set_child(*root);
  ApplyVisibility();
}

VlessSheet::~VlessSheet() { *alive_ = false; }

VlessSheet::Field VlessSheet::AddField(Gtk::Box& host, const Glib::ustring& label) {
  Field field;
  field.root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  field.root->append(*MakeFieldLabel(label));
  field.entry = Gtk::make_managed<Gtk::Entry>();
  field.entry->add_css_class("ur-input");
  // a caption above a field is not its accessible name
  kit::SetAccessibleLabel(*field.entry, label);
  field.entry->signal_changed().connect([this] { OnEdited(); });
  field.entry->signal_activate().connect([this] { Save(); });
  field.root->append(*field.entry);
  host.append(*field.root);
  return field;
}

VlessSheet::Picker VlessSheet::AddPicker(Gtk::Box& host, const Glib::ustring& label,
                                         std::vector<vless::Option> options, std::string* value) {
  Picker picker;
  picker.options = std::move(options);
  picker.value = value;
  picker.root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  picker.root->append(*MakeFieldLabel(label));
  auto* segmented = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  segmented->add_css_class("linked");
  Gtk::ToggleButton* first = nullptr;
  for (const vless::Option& option : picker.options) {
    const Glib::ustring text = Tr(option.label);
    auto* button = Gtk::make_managed<Gtk::ToggleButton>(text);
    button->set_hexpand(true);
    // "Transport, TCP": the caption above the row is not announced with it
    kit::SetAccessibleLabel(*button, label + ", " + text);
    if (first) {
      button->set_group(*first);
    } else {
      first = button;
    }
    const std::string optionValue(option.value);
    button->signal_toggled().connect([this, button, value, optionValue] {
      if (applying_ || !button->get_active()) return;
      *value = optionValue;
      ApplyVisibility();
      OnEdited();
    });
    segmented->append(*button);
    picker.buttons.push_back(button);
  }
  picker.root->append(*segmented);
  host.append(*picker.root);
  return picker;
}

void VlessSheet::SelectPicker(Picker& picker) {
  // A value no option carries selects nothing: the form keeps the value, and
  // the SDK says what is wrong with it if it is saved enabled.
  const int index = vless::OptionIndex(picker.options, *picker.value);
  for (size_t i = 0; i < picker.buttons.size(); ++i) {
    picker.buttons[i]->set_active(static_cast<int>(i) == index);
  }
}

void VlessSheet::SelectFingerprint() {
  const int index = vless::OptionIndex(fingerprints_, fingerprint_);
  fingerprintBox_->set_selected(index < 0 ? GTK_INVALID_LIST_POSITION
                                          : static_cast<guint>(index));
}

void VlessSheet::Open() {
  Load();
  present();
}

void VlessSheet::Load() {
  base_ = host_.GetVlessSettings();
  const bool editable = base_.has_value();
  ApplyForm(editable ? vless::FormFromSettings(*base_) : vless::Form{});
  link_->set_text("");
  ClearLinkMessage();
  ClearSaveMessage();
  form_->set_sensitive(editable);
  save_->set_sensitive(editable);
  if (!editable) {
    // No space to edit (the SDK never came up): say so, rather than offer a
    // form whose Save could only fail.
    ShowSaveMessage(T_("something_went_wrong", "Something went wrong."), {}, true);
  }
}

void VlessSheet::ApplyForm(const vless::Form& form) {
  applying_ = true;
  enabled_->set_active(form.enabled);
  name_.entry->set_text(form.name);
  address_.entry->set_text(form.address);
  port_.entry->set_text(form.port);
  id_.entry->set_text(form.id);
  network_ = form.network;
  security_ = form.security;
  flow_ = form.flow;
  fingerprint_ = form.fingerprint;
  SelectPicker(networkPicker_);
  SelectPicker(securityPicker_);
  SelectPicker(flowPicker_);
  SelectFingerprint();
  serverName_.entry->set_text(form.serverName);
  alpn_.entry->set_text(form.alpn);
  insecure_->set_active(form.allowInsecure);
  publicKey_.entry->set_text(form.publicKey);
  shortId_.entry->set_text(form.shortId);
  path_.entry->set_text(form.path);
  hostHeader_.entry->set_text(form.host);
  applying_ = false;
  ApplyVisibility();
}

vless::Form VlessSheet::ReadForm() const {
  vless::Form form;
  form.enabled = enabled_->get_active();
  form.name = name_.entry->get_text().raw();
  form.address = address_.entry->get_text().raw();
  form.port = port_.entry->get_text().raw();
  form.id = id_.entry->get_text().raw();
  form.network = network_;
  form.security = security_;
  form.flow = flow_;
  form.serverName = serverName_.entry->get_text().raw();
  form.fingerprint = fingerprint_;
  form.alpn = alpn_.entry->get_text().raw();
  form.allowInsecure = insecure_->get_active();
  form.publicKey = publicKey_.entry->get_text().raw();
  form.shortId = shortId_.entry->get_text().raw();
  form.path = path_.entry->get_text().raw();
  form.host = hostHeader_.entry->get_text().raw();
  return form;
}

urnet::VlessSettings VlessSheet::CurrentSettings() const {
  return vless::SettingsFromForm(ReadForm(), base_.value_or(urnet::VlessSettings{}));
}

void VlessSheet::ApplyVisibility() {
  const vless::Visibility visible = vless::VisibilityFor(network_, security_);
  flowPicker_.root->set_visible(visible.flow);
  serverName_.root->set_visible(visible.serverName);
  fingerprintRoot_->set_visible(visible.fingerprint);
  alpn_.root->set_visible(visible.alpn);
  insecureRow_->set_visible(visible.allowInsecure);
  publicKey_.root->set_visible(visible.publicKey);
  shortId_.root->set_visible(visible.shortId);
  path_.root->set_visible(visible.path);
  hostHeader_.root->set_visible(visible.host);
}

void VlessSheet::PasteLink() {
  auto clipboard = get_clipboard();
  auto alive = alive_;
  clipboard->read_text_async([this, alive, clipboard](Glib::RefPtr<Gio::AsyncResult>& result) {
    Glib::ustring text;
    try {
      text = clipboard->read_text_finish(result);
    } catch (const Glib::Error&) {
      // nothing readable on the clipboard: the field's own text is the link
    }
    if (!*alive) return;
    // Never logged: a link carries the server's user id.
    if (!text.empty()) link_->set_text(text);
    ReadLink(link_->get_text().raw());
  });
}

void VlessSheet::ReadLink(const std::string& text) {
  const std::optional<urnet::VlessLinkResult> result = SdkHost::ParseVlessLink(text);
  if (!result || !result->Error.empty() || !result->Settings) {
    ShowLinkMessage(Tr(vless::ErrorText(result ? result->Error : std::string())), true);
    return;
  }
  // The link REPLACES the form (enabled) and becomes the base a save writes
  // over, so what only the link carries -- the REALITY spider path -- is kept.
  base_ = result->Settings;
  ApplyForm(vless::FormFromSettings(*base_));
  ClearLinkMessage();
  ClearSaveMessage();  // the form changed; none of it is saved yet
}

void VlessSheet::CopyLink() {
  const urnet::VlessSettings settings = CurrentSettings();
  // Settings that do not validate have no link; saying why is more use than a
  // button that does nothing.
  const std::string error = SdkHost::ValidateVlessSettings(settings);
  const std::string link = error.empty() ? SdkHost::VlessSettingsLink(settings) : std::string();
  if (link.empty()) {
    ShowLinkMessage(Tr(vless::ErrorText(error)), true);
    return;
  }
  get_clipboard()->set_text(link);
  ShowLinkMessage(T_("vless_link_copied", "VLESS link copied"), false);
}

void VlessSheet::Save() {
  if (!base_) return;  // no space to save to; the form is insensitive
  const std::optional<std::string> answer = host_.SetVlessSettings(CurrentSettings());
  if (!answer) {
    ShowSaveMessage(T_("something_went_wrong", "Something went wrong."), {}, true);
    return;
  }
  const vless::SaveOutcome outcome = vless::SaveOutcomeFor(*answer);
  if (!outcome.saved) {
    ShowSaveMessage(Tr(outcome.message), {}, true);
    return;
  }
  // Read back what the SDK STORED -- it trims and lower-cases, and off with no
  // server clears -- so the form shows what is in force and the next open
  // agrees with it.
  if (auto stored = host_.GetVlessSettings()) {
    base_ = std::move(stored);
    ApplyForm(vless::FormFromSettings(*base_));
  }
  ShowSaveMessage(Tr(outcome.message), Tr(outcome.note), false);
}

void VlessSheet::ShowLinkMessage(const Glib::ustring& text, bool error) {
  kit::ApplySupportingText(*linkStatus_, text,
                           error ? kit::ValidationState::Invalid : kit::ValidationState::Valid);
  linkStatus_->set_visible(!text.empty());
}

void VlessSheet::ClearLinkMessage() {
  linkStatus_->set_text("");
  linkStatus_->set_visible(false);
}

void VlessSheet::ShowSaveMessage(const Glib::ustring& message, const Glib::ustring& note,
                                 bool error) {
  kit::ApplySupportingText(*status_, message,
                           error ? kit::ValidationState::Invalid : kit::ValidationState::Valid);
  status_->set_visible(!message.empty());
  kit::SetTextOrCollapse(*note_, note);
}

void VlessSheet::ClearSaveMessage() {
  status_->set_text("");
  status_->set_visible(false);
  kit::SetTextOrCollapse(*note_, {});
}

void VlessSheet::OnEdited() {
  if (applying_) return;
  ClearSaveMessage();
}

}  // namespace urnw
