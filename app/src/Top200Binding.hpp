// The Top 200 fleet binding a device signs in the app (support inbox 7934,
// factury support/plans/top200-in-app-signing.md): the pure half of the
// Earnings › Top 200 › Sign fleet binding sheet, with no GTK and no SDK so the
// unit tests reach every rule.
//
// The operator builds one binding per member device on the ur.io Top 200 page
// (or `provider fleet sign` prints it). The binding is one compact JSON object
// with exactly these 11 fields:
//
//   {"chain_id":945,"netuid":25,"coordinator":"0x<40 hex>","fleet_id":"0x<64 hex>",
//    "hotkey":"0x<64 hex>","client_id":"<uuid>","client_key":"0x<64 hex>",
//    "generation":3,"valid_from_epoch":100,"valid_to_epoch":200,
//    "commitment_hash":"0x<64 hex>"}
//
// The sheet validates the pasted text before any sdk call (ValidateBinding),
// shows the fleet id, hotkey, chain and epochs, and only on the user's tap asks
// the device to sign the trimmed text. The sdk refuses another device's
// client_id or client_key too, and ambiguous JSON (a duplicate, case-variant or
// extra key), in the process that holds the key (sdk 98ba0ea8, main 61e89201).
// The check here is the first line, not the only one: it runs before any sdk
// call so the review never shows a binding the device would not sign.
//
// The text must open with `{` once JSON whitespace is trimmed: nlohmann skips a
// leading UTF-8 BOM but the sdk's Go decoder refuses one, so a BOM (or any text
// before the object) is refused here and review and sign agree. Pasted text
// over kMaxBindingTextChars characters is refused before it is parsed.
//
// Go's encoding/json matches keys without regard to case and lets a later
// duplicate win, so a binding carrying both "hotkey" and "HOTKEY" could show
// the user one hotkey and sign another. The sdk refuses such a binding before
// it decodes it; here too the object must hold exactly the 11 keys, spelled
// exactly, each once.
//
// The payload copied back to the page is the trimmed binding text wrapped by
// string concatenation (BuildPayload), never re-serialized, so a page-built
// binding gives a payload byte-identical to `provider fleet sign`'s output.
//
// The app link urnetwork://top200/sign?binding=<base64url> only pre-fills the
// field (ParseSignLink): it never signs and never skips the review.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

namespace urnw {
namespace top200 {

// A link's binding parameter longer than this is refused before decoding.
inline constexpr size_t kMaxLinkBindingChars = 4096;
// Trimmed pasted text longer than this (in characters) is not a binding: a
// binding is about 600, and a huge clipboard is not parsed on each keystroke.
inline constexpr size_t kMaxBindingTextChars = 4096;
inline constexpr const char* kSignLinkPrefix = "urnetwork://top200/sign";
// The detail of a sign that returned no signature (top200_sign_error_failed).
inline constexpr const char* kEmptySignatureDetail = "empty signature";

// Each maps to one top200_sign_error_* string; None is a binding of this
// device, ready for review.
enum class BindingError { None, Invalid, NoKey, OtherDevice };

struct Binding {
  uint64_t chainId = 0;
  uint64_t netuid = 0;
  std::string coordinator;
  std::string fleetId;
  std::string hotkey;
  std::string clientId;
  std::string clientKey;
  uint64_t generation = 0;
  uint64_t validFromEpoch = 0;
  uint64_t validToEpoch = 0;
  std::string commitmentHash;
};

struct BindingCheck {
  BindingError error = BindingError::Invalid;
  // set when the text is a well-formed binding (error None, NoKey or OtherDevice)
  std::optional<Binding> binding;
  // the text the device signs and the payload wraps
  std::string trimmed;
};

// JSON whitespace (space, tab, CR, LF) off both ends.
inline std::string TrimJsonWhitespace(const std::string& text) {
  const char* space = " \t\r\n";
  const size_t start = text.find_first_not_of(space);
  if (start == std::string::npos) return std::string();
  const size_t end = text.find_last_not_of(space);
  return text.substr(start, end - start + 1);
}

inline bool IsHexDigit(char c) {
  return ('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}

inline std::string LowercaseAscii(std::string text) {
  for (char& c : text) {
    if ('A' <= c && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

// "0x" (lowercase x) and exactly `hexCount` hex digits, either case.
inline bool IsPrefixedHex(const std::string& text, size_t hexCount) {
  if (text.size() != 2 + hexCount || text[0] != '0' || text[1] != 'x') return false;
  for (size_t i = 2; i < text.size(); ++i) {
    if (!IsHexDigit(text[i])) return false;
  }
  return true;
}

// The 16 bytes of a client id as 32 lowercase hex digits: the uuid form
// (8-4-4-4-12) or 0x + 32 hex. nullopt for anything else.
inline std::optional<std::string> ClientIdHex(const std::string& clientId) {
  if (IsPrefixedHex(clientId, 32)) return LowercaseAscii(clientId.substr(2));
  if (clientId.size() != 36) return std::nullopt;
  std::string hex;
  for (size_t i = 0; i < clientId.size(); ++i) {
    const char c = clientId[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') return std::nullopt;
      continue;
    }
    if (!IsHexDigit(c)) return std::nullopt;
    hex += c;
  }
  return LowercaseAscii(hex);
}

// The number of characters (UTF-8 code points) in `text`: each byte that is
// not a continuation byte starts one.
inline size_t Utf8CharCount(const std::string& text) {
  size_t count = 0;
  for (const char c : text) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
  }
  return count;
}

// The binding check, in the spec's order: not a binding is Invalid whatever
// the device; then no client key (the daemon's device is not reachable) is
// NoKey; then a client_id or client_key that is not this device's is
// OtherDevice.
inline BindingCheck ValidateBinding(const std::string& text, const std::string& deviceClientId,
                                    const std::string& deviceClientKey) {
  BindingCheck check;
  check.trimmed = TrimJsonWhitespace(text);
  if (kMaxBindingTextChars < Utf8CharCount(check.trimmed)) return check;
  // nothing before the object: not a BOM (nlohmann skips one, Go refuses it)
  if (check.trimmed.empty() || check.trimmed.front() != '{') return check;

  static const std::set<std::string> kFields = {
      "chain_id",   "netuid",           "coordinator",    "fleet_id",
      "hotkey",     "client_id",        "client_key",     "generation",
      "valid_from_epoch", "valid_to_epoch", "commitment_hash"};
  bool duplicate = false;
  std::set<std::string> seenKeys;
  nlohmann::json root;
  try {
    // the callback sees each key of the top-level object (depth 1) as parsed
    root = nlohmann::json::parse(
        check.trimmed,
        [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
          if (event == nlohmann::json::parse_event_t::key && depth == 1) {
            if (!seenKeys.insert(parsed.get<std::string>()).second) duplicate = true;
          }
          return true;
        });
  } catch (const std::exception&) {
    return check;
  }
  if (duplicate || !root.is_object() || root.size() != kFields.size()) return check;
  for (const auto& field : kFields) {
    if (!root.contains(field)) return check;
  }

  Binding binding;
  // a JSON integer >= 0 (a float, a negative or an out-of-range number is not)
  const auto readUint = [&](const char* name, uint64_t& out) {
    const nlohmann::json& value = root.at(name);
    if (!value.is_number_unsigned()) return false;
    out = value.get<uint64_t>();
    return true;
  };
  const auto readHex = [&](const char* name, size_t hexCount, std::string& out) {
    const nlohmann::json& value = root.at(name);
    if (!value.is_string()) return false;
    out = value.get<std::string>();
    return IsPrefixedHex(out, hexCount);
  };
  if (!readUint("chain_id", binding.chainId) || binding.chainId == 0) return check;
  if (!readUint("netuid", binding.netuid) || 65535 < binding.netuid) return check;
  if (!readUint("generation", binding.generation) || binding.generation == 0) return check;
  if (!readUint("valid_from_epoch", binding.validFromEpoch)) return check;
  if (!readUint("valid_to_epoch", binding.validToEpoch)) return check;
  if (binding.validToEpoch < binding.validFromEpoch) return check;
  if (!readHex("coordinator", 40, binding.coordinator)) return check;
  if (!readHex("fleet_id", 64, binding.fleetId)) return check;
  if (!readHex("hotkey", 64, binding.hotkey)) return check;
  if (!readHex("client_key", 64, binding.clientKey)) return check;
  if (!readHex("commitment_hash", 64, binding.commitmentHash)) return check;
  const nlohmann::json& clientIdValue = root.at("client_id");
  if (!clientIdValue.is_string()) return check;
  binding.clientId = clientIdValue.get<std::string>();
  const std::optional<std::string> bindingClientIdHex = ClientIdHex(binding.clientId);
  if (!bindingClientIdHex) return check;
  check.binding = binding;

  if (deviceClientKey.empty()) {
    check.error = BindingError::NoKey;
    return check;
  }
  const std::optional<std::string> deviceClientIdHex = ClientIdHex(deviceClientId);
  if (LowercaseAscii(binding.clientKey) != LowercaseAscii(deviceClientKey) ||
      !deviceClientIdHex || *bindingClientIdHex != *deviceClientIdHex) {
    check.error = BindingError::OtherDevice;
    return check;
  }
  check.error = BindingError::None;
  return check;
}

// What the Sign button shows. Sign is enabled only for a reviewed binding of
// this device once the identity read has answered with a client key, and never
// while a sign is out (or waiting for the shared fleet worker): then the
// spinner shows instead, so a tap is never dropped without a sign of it.
struct SignButtonState {
  bool sensitive = false;
  bool busy = false;
};

inline SignButtonState SignButton(bool reviewed, bool identityLoaded, bool hasClientKey,
                                  bool signing) {
  SignButtonState state;
  state.busy = signing;
  state.sensitive = reviewed && identityLoaded && hasClientKey && !signing;
  return state;
}

// {"binding":<trimmed>,"client_signature":"<signature>"}, by concatenation.
inline std::string BuildPayload(const std::string& trimmed, const std::string& signature) {
  return "{\"binding\":" + trimmed + ",\"client_signature\":\"" + signature + "\"}";
}

struct SignOutcome {
  bool ok = false;
  std::string payload;  // when ok
  std::string detail;   // top200_sign_error_failed's {detail} when not ok
};

// What the sheet shows after SignSnFleetBinding answered with `signature`, or
// threw `error` (non-empty). An empty signature is a failure, never a payload.
inline SignOutcome FinishSign(const std::string& trimmed, const std::string& signature,
                              const std::string& error) {
  SignOutcome outcome;
  if (!error.empty()) {
    outcome.detail = error;
    return outcome;
  }
  if (signature.empty()) {
    outcome.detail = kEmptySignatureDetail;
    return outcome;
  }
  outcome.ok = true;
  outcome.payload = BuildPayload(trimmed, signature);
  return outcome;
}

// This device's fleet manifest member entry,
// {"client_id":"0x<32 lowercase hex>","client_key":"<client key>"}, or "" while
// either is unknown.
inline std::string MemberEntry(const std::string& clientId, const std::string& clientKey) {
  const std::optional<std::string> hex = ClientIdHex(clientId);
  if (!hex || clientKey.empty()) return std::string();
  return "{\"client_id\":\"0x" + *hex + "\",\"client_key\":\"" + clientKey + "\"}";
}

// "0x79b5562e…049664": the first 10 and last 6 characters; short values whole.
inline std::string ShortKey(const std::string& value) {
  if (value.size() <= 16) return value;
  return value.substr(0, 10) + "…" + value.substr(value.size() - 6);
}

// RFC 4648 §5 base64url without padding; nullopt for any other character, an
// impossible length or set trailing bits.
inline std::optional<std::string> DecodeBase64Url(const std::string& text) {
  const auto value = [](char c) -> int {
    if ('A' <= c && c <= 'Z') return c - 'A';
    if ('a' <= c && c <= 'z') return c - 'a' + 26;
    if ('0' <= c && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
  };
  if (text.size() % 4 == 1) return std::nullopt;
  std::string out;
  uint32_t buffer = 0;
  int bits = 0;
  for (const char c : text) {
    const int v = value(c);
    if (v < 0) return std::nullopt;
    buffer = (buffer << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (8 <= bits) {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xFF);
    }
  }
  if (bits != 0 && (buffer & ((1u << bits) - 1)) != 0) return std::nullopt;
  return out;
}

// Well-formed UTF-8: no overlong forms, no surrogates, nothing past U+10FFFF.
inline bool IsValidUtf8(const std::string& text) {
  size_t i = 0;
  while (i < text.size()) {
    const auto byte = [&](size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char lead = byte(i);
    size_t length = 0;
    uint32_t code = 0;
    uint32_t minimum = 0;
    if (lead < 0x80) {
      ++i;
      continue;
    } else if ((lead & 0xE0) == 0xC0) {
      length = 2;
      code = lead & 0x1F;
      minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
      code = lead & 0x0F;
      minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
      code = lead & 0x07;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (text.size() < i + length) return false;
    for (size_t k = 1; k < length; ++k) {
      if ((byte(i + k) & 0xC0) != 0x80) return false;
      code = (code << 6) | (byte(i + k) & 0x3F);
    }
    if (code < minimum || 0x10FFFF < code || (0xD800 <= code && code <= 0xDFFF)) return false;
    i += length;
  }
  return true;
}

struct SignLink {
  bool matched = false;  // a urnetwork://top200/sign link
  // the decoded binding text for the paste field; nullopt when the link's
  // binding parameter is missing, oversized, not base64url or not UTF-8 (the
  // sheet then opens showing top200_sign_error_invalid)
  std::optional<std::string> binding;
};

inline SignLink ParseSignLink(const std::string& url) {
  SignLink link;
  const std::string prefix = kSignLinkPrefix;
  if (url.compare(0, prefix.size(), prefix) != 0) return link;
  const std::string rest = url.substr(prefix.size());
  if (!rest.empty() && rest[0] != '?' && rest != "/" && rest.compare(0, 2, "/?") != 0) return link;
  link.matched = true;
  const size_t query = rest.find('?');
  if (query == std::string::npos) return link;
  std::string encoded;
  bool found = false;
  size_t i = query + 1;
  while (i <= rest.size()) {
    size_t amp = rest.find('&', i);
    if (amp == std::string::npos) amp = rest.size();
    const std::string pair = rest.substr(i, amp - i);
    if (pair.compare(0, 8, "binding=") == 0) {
      if (found) return link;  // two bindings: neither is the one to show
      found = true;
      encoded = pair.substr(8);
    }
    i = amp + 1;
  }
  if (!found || encoded.empty() || kMaxLinkBindingChars < encoded.size()) return link;
  std::optional<std::string> decoded = DecodeBase64Url(encoded);
  if (!decoded || !IsValidUtf8(*decoded)) return link;
  link.binding = std::move(decoded);
  return link;
}

}  // namespace top200
}  // namespace urnw
