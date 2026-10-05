// The VLESS settings editor's logic (Settings > VLESS and the login screen's
// network sheet): the form the sheet edits, which rows show for a transport
// and a security, the form <-> settings mapping, the pickers' options and the
// error id -> message table -- all as pure functions so the rules are pinned
// by tests and the sheet only renders what these decided.
//
// Reading a vless:// link, building one, validating and saving live in the
// SDK (sdk vless_settings_ui.go), one implementation for every app. NOTHING
// here parses a link or second-guesses validation: the SDK answers with an
// error id, and this maps the id to the store's message.
//
// Header-only and free of GTK and the SDK so the unit tests need no vendored
// headers (tests/VlessPresentationTest.cpp). The mapping is templated over the
// shape of the SDK's urnet::VlessSettings (std::optional fields, named as in
// the generated urnetwork_sdk.hpp); VlessSheet.cpp instantiates it with the
// real type.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace urnw::vless {

// ---- the wire values ----------------------------------------------------------
// sdk VlessNetworks(), VlessSecurities() and VlessFlows() (connect vless.go).
inline constexpr const char* kNetworkTcp = "tcp";
inline constexpr const char* kNetworkWs = "ws";
inline constexpr const char* kNetworkHttpUpgrade = "httpupgrade";
inline constexpr const char* kSecurityNone = "none";
inline constexpr const char* kSecurityTls = "tls";
inline constexpr const char* kSecurityReality = "reality";
inline constexpr const char* kFlowNone = "";
inline constexpr const char* kFlowVision = "xtls-rprx-vision";

// A store key and its English (the gettext msgid). An EMPTY key is a value
// shown exactly as it is: the fingerprints' technical names are what people
// paste and look up, so they are deliberately not localized.
struct Text {
  std::string_view key;
  std::string_view english;
};

// One choice of a picker: the value the settings carry, and what it shows.
struct Option {
  std::string_view value;
  Text label;
};

inline std::vector<Option> NetworkOptions() {
  return {
      {kNetworkTcp, {"vless_network_tcp", "TCP"}},
      {kNetworkWs, {"vless_network_ws", "WebSocket"}},
      {kNetworkHttpUpgrade, {"vless_network_httpupgrade", "HTTPUpgrade"}},
  };
}

inline std::vector<Option> SecurityOptions() {
  return {
      {kSecurityNone, {"none", "None"}},
      {kSecurityTls, {"vless_security_tls", "TLS"}},
      {kSecurityReality, {"vless_security_reality", "REALITY"}},
  };
}

// The empty flow is no flow.
inline std::vector<Option> FlowOptions() {
  return {
      {kFlowNone, {"none", "None"}},
      {kFlowVision, {"vless_flow_vision", "Vision"}},
  };
}

// sdk VlessFingerprints(), in its order. The empty fingerprint reads "None"
// (the Go tls hello for tls; reality falls back to chrome); the rest are raw.
inline std::vector<Option> FingerprintOptions() {
  return {
      {"", {"none", "None"}},
      {"chrome", {"", "chrome"}},
      {"firefox", {"", "firefox"}},
      {"safari", {"", "safari"}},
      {"ios", {"", "ios"}},
      {"android", {"", "android"}},
      {"edge", {"", "edge"}},
      {"360", {"", "360"}},
      {"qq", {"", "qq"}},
      {"random", {"", "random"}},
      {"randomized", {"", "randomized"}},
  };
}

// The position of `value` in `options`, or -1 when no option carries it (a
// stored value this build does not offer selects nothing rather than a guess).
inline int OptionIndex(const std::vector<Option>& options, std::string_view value) {
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i].value == value) return static_cast<int>(i);
  }
  return -1;
}

// ---- spelling ----------------------------------------------------------------
// The settings' own spelling rules (sdk VlessSettings.normalized, connect
// VlessConfig.network() / security()): the enumerations are trimmed and lower
// case, an empty transport is tcp and an empty security is none. The form
// holds them in this spelling so the pickers and the visibility rules agree
// with what the SDK will make of the same value.
inline std::string NormalizedValue(std::string_view value) {
  auto space = [](unsigned char c) { return std::isspace(c) != 0; };
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && space(static_cast<unsigned char>(value[begin]))) ++begin;
  while (end > begin && space(static_cast<unsigned char>(value[end - 1]))) --end;
  std::string out(value.substr(begin, end - begin));
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

inline std::string NormalizedNetwork(std::string_view value) {
  std::string out = NormalizedValue(value);
  return out.empty() ? std::string(kNetworkTcp) : out;
}

inline std::string NormalizedSecurity(std::string_view value) {
  std::string out = NormalizedValue(value);
  return out.empty() ? std::string(kSecurityNone) : out;
}

// ---- the form ------------------------------------------------------------------

// What the sheet's fields hold. The defaults are sdk NewVlessSettings(): a
// REALITY server over raw tcp with the Vision flow and a chrome hello on 443,
// not enabled -- the shape most shared servers have.
struct Form {
  bool enabled = false;
  std::string name;
  std::string address;
  std::string port = "443";  // as typed; ParsePort reads it on save
  std::string id;
  std::string network = kNetworkTcp;
  std::string security = kSecurityReality;
  std::string flow = kFlowVision;
  std::string serverName;
  std::string fingerprint = "chrome";
  std::string alpn;
  bool allowInsecure = false;
  std::string publicKey;
  std::string shortId;
  std::string path;
  std::string host;

  friend bool operator==(const Form& a, const Form& b) {
    return a.enabled == b.enabled && a.name == b.name && a.address == b.address &&
           a.port == b.port && a.id == b.id && a.network == b.network &&
           a.security == b.security && a.flow == b.flow && a.serverName == b.serverName &&
           a.fingerprint == b.fingerprint && a.alpn == b.alpn &&
           a.allowInsecure == b.allowInsecure && a.publicKey == b.publicKey &&
           a.shortId == b.shortId && a.path == b.path && a.host == b.host;
  }
  friend bool operator!=(const Form& a, const Form& b) { return !(a == b); }
};

// Which of the transport- and security-specific rows show.
struct Visibility {
  bool flow = false;           // tcp with tls or reality
  bool serverName = false;     // tls, reality
  bool fingerprint = false;    // tls, reality
  bool alpn = false;           // tls
  bool allowInsecure = false;  // tls
  bool publicKey = false;      // reality
  bool shortId = false;        // reality
  bool path = false;           // ws, httpupgrade
  bool host = false;           // ws, httpupgrade
};

// The Vision flow pads the inner tls handshake and lets the server send the
// rest outside the outer tls, which needs a raw tcp stream under tls or
// reality (connect VlessConfig.Validate) -- anywhere else it is an error, so
// the picker is not offered there at all.
inline Visibility VisibilityFor(std::string_view network, std::string_view security) {
  const std::string n = NormalizedNetwork(network);
  const std::string s = NormalizedSecurity(security);
  const bool secured = s != kSecurityNone;
  const bool tls = s == kSecurityTls;
  const bool reality = s == kSecurityReality;
  const bool http = n == kNetworkWs || n == kNetworkHttpUpgrade;
  Visibility out;
  out.flow = n == kNetworkTcp && secured;
  out.serverName = secured;
  out.fingerprint = secured;
  out.alpn = tls;
  out.allowInsecure = tls;
  out.publicKey = reality;
  out.shortId = reality;
  out.path = http;
  out.host = http;
  return out;
}

inline Visibility VisibilityFor(const Form& form) {
  return VisibilityFor(form.network, form.security);
}

// The port field's number: digits, with surrounding spaces allowed. Anything
// else reads as 0, which the SDK reports as vless_error_port_invalid -- the
// message that says what a port has to be. A number too long to be a port
// reads as one past the range instead of wrapping back into it.
inline int64_t ParsePort(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && text[begin] == ' ') ++begin;
  while (end > begin && text[end - 1] == ' ') --end;
  if (begin == end) return 0;
  int64_t port = 0;
  for (size_t i = begin; i < end; ++i) {
    const char c = text[i];
    if (c < '0' || '9' < c) return 0;
    port = port * 10 + (c - '0');
    if (port > 65535) return 65536;
  }
  return port;
}

// The form the sheet opens on for `settings` (networkSpace.getVlessSettings(),
// or a parsed link's settings).
template <class Settings>
inline Form FormFromSettings(const Settings& settings) {
  Form form;
  form.enabled = settings.enabled.value_or(false);
  form.name = settings.name.value_or(std::string());
  form.address = settings.address.value_or(std::string());
  const int64_t port = settings.port.value_or(0);
  form.port = port > 0 ? std::to_string(port) : std::string();
  form.id = settings.id.value_or(std::string());
  form.network = NormalizedNetwork(settings.network.value_or(std::string()));
  form.security = NormalizedSecurity(settings.security.value_or(std::string()));
  form.flow = NormalizedValue(settings.flow.value_or(std::string()));
  form.serverName = settings.server_name.value_or(std::string());
  form.fingerprint = NormalizedValue(settings.fingerprint.value_or(std::string()));
  form.alpn = settings.alpn.value_or(std::string());
  form.allowInsecure = settings.allow_insecure.value_or(false);
  form.publicKey = settings.public_key.value_or(std::string());
  form.shortId = settings.short_id.value_or(std::string());
  form.path = settings.path.value_or(std::string());
  form.host = settings.host.value_or(std::string());
  return form;
}

// The settings the form saves, written over `base` -- the settings the form
// was opened on (or the last link read into it). Starting from `base` is what
// keeps what the form never shows: the REALITY spider path (spider_x) only
// travels in links, and a save must not erase it.
//
// The one value the form BLANKS is a hidden flow: Vision anywhere but tcp with
// tls or reality fails validation, and a flow the user can no longer see must
// not be what refuses the save. Every other hidden value is kept as typed
// (the SDK ignores it for that transport and security, and a link leaves it
// out), so switching the security away and back loses nothing.
template <class Settings>
inline Settings SettingsFromForm(const Form& form, Settings base) {
  const Visibility visible = VisibilityFor(form);
  base.enabled = form.enabled;
  base.name = form.name;
  base.address = form.address;
  base.port = ParsePort(form.port);
  base.id = form.id;
  base.network = form.network;
  base.security = form.security;
  base.flow = visible.flow ? form.flow : std::string(kFlowNone);
  base.server_name = form.serverName;
  base.fingerprint = form.fingerprint;
  base.alpn = form.alpn;
  base.allow_insecure = form.allowInsecure;
  base.public_key = form.publicKey;
  base.short_id = form.shortId;
  base.path = form.path;
  base.host = form.host;
  return base;
}

// ---- messages ------------------------------------------------------------------

// sdk VlessError* (URNET_VLESS_ERROR_*): each id IS the localization key of
// its message.
inline constexpr const char* kErrorLinkInvalid = "vless_error_link_invalid";
inline constexpr const char* kErrorLinkUnsupported = "vless_error_link_unsupported";
inline constexpr const char* kErrorAddressInvalid = "vless_error_address_invalid";
inline constexpr const char* kErrorPortInvalid = "vless_error_port_invalid";
inline constexpr const char* kErrorIdInvalid = "vless_error_id_invalid";
inline constexpr const char* kErrorNetworkUnsupported = "vless_error_network_unsupported";
inline constexpr const char* kErrorSecurityUnsupported = "vless_error_security_unsupported";
inline constexpr const char* kErrorFlowInvalid = "vless_error_flow_invalid";
inline constexpr const char* kErrorServerNameRequired = "vless_error_server_name_required";
inline constexpr const char* kErrorFingerprintUnsupported = "vless_error_fingerprint_unsupported";
inline constexpr const char* kErrorPublicKeyInvalid = "vless_error_public_key_invalid";
inline constexpr const char* kErrorShortIdInvalid = "vless_error_short_id_invalid";

// The message of an SDK error id. An id this build does not know is still a
// refusal, and "this is not a valid VLESS link" says so better than a raw key.
inline Text ErrorText(std::string_view errorId) {
  static constexpr Text kErrors[] = {
      {kErrorLinkInvalid, "This is not a valid VLESS link."},
      {kErrorLinkUnsupported, "This link uses a VLESS feature this app does not support."},
      {kErrorAddressInvalid, "Enter the server address."},
      {kErrorPortInvalid, "Enter a port from 1 to 65535."},
      {kErrorIdInvalid, "Enter the user ID (a UUID)."},
      {kErrorNetworkUnsupported,
       "This transport is not supported. Use TCP, WebSocket or HTTPUpgrade."},
      {kErrorSecurityUnsupported, "This security type is not supported. Use TLS, REALITY or none."},
      {kErrorFlowInvalid,
       "The Vision flow works only with the TCP transport and TLS or REALITY security."},
      {kErrorServerNameRequired, "Enter the server name (SNI) for REALITY."},
      {kErrorFingerprintUnsupported, "This TLS fingerprint is not supported."},
      {kErrorPublicKeyInvalid, "Enter the REALITY public key."},
      {kErrorShortIdInvalid, "The REALITY short ID must be up to 16 hexadecimal characters."},
  };
  for (const Text& error : kErrors) {
    if (error.key == errorId) return error;
  }
  return kErrors[0];
}

// What the line under Save says for the SDK's answer to setVlessSettings.
// An empty answer is saved -- and on this platform the VPN runs in urnetworkd,
// which imports the network space at its next tunnel start, so the
// next-connect note always goes with it. An id is that error's message, and
// nothing was saved.
struct SaveOutcome {
  bool saved = false;
  Text message;
  Text note;  // an empty key: no second line
};

inline SaveOutcome SaveOutcomeFor(std::string_view errorId) {
  SaveOutcome out;
  if (!errorId.empty()) {
    out.message = ErrorText(errorId);
    return out;
  }
  out.saved = true;
  out.message = {"vless_settings_saved", "VLESS settings saved"};
  out.note = {"vless_settings_next_connect",
              "The VPN uses the new VLESS settings the next time it connects."};
  return out;
}

}  // namespace urnw::vless
