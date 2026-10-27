// The connection region (internal name: ingress country): where the user says
// they connect from, which picks how the app resolves URnetwork's own names
// (api, connect, extender) -- the ingress DNS. It never touches the traffic
// (egress) DNS of the tunnel, which the Custom DNS sheet edits.
//
// One stored value, the network space's ingress country (sdk
// NetworkSpace.Get/SetIngressCountryCode), read and written by two doors: the
// login screen's optional "Connecting from" control and the Custom DNS
// sheet's "Connection region" row. Both open the same picker. The value is a
// space value, local to the device, and applies in place from the next lookup.
//
// The picker offers exactly the regions the SDK has a recommendation for
// (urnet::ingressCountryCodes), by the locale's country name, then "Other /
// Undisclosed" (""). It shows the resolvers a choice uses.
//
// When a call to URnetwork fails at the network level (no answer at all, not
// an auth or validation refusal), the app offers "Can't reach URnetwork --
// choose where you're connecting from", which opens the picker. It never
// changes the region by itself.
//
// Pure, with no GTK or SDK dependency, so the unit suite runs it anywhere; the
// GTK sheet (ConnectionRegionSheet) and SdkHost supply the store and names.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace urnw {
namespace region {

// Where the stored connection region lives. SdkHost's network space in the
// app, a fake in the tests. Set answers the value actually stored (an unknown
// code is stored as undisclosed, "").
class Store {
 public:
  virtual ~Store() = default;
  virtual std::string Get() = 0;
  virtual std::string Set(const std::string& countryCode) = 0;
};

struct Option {
  std::string code;  // lower case iso code; "" is Other / Undisclosed
  std::string name;  // what the picker shows
  bool operator==(const Option& other) const {
    return code == other.code && name == other.name;
  }
};

inline std::string Lowercased(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// The picker's rows: the SDK's regions in its order (lower cased, blanks and
// repeats dropped), each named by `displayName`, then Other / Undisclosed.
inline std::vector<Option> Options(const std::vector<std::string>& sdkCodes,
                                   const std::function<std::string(const std::string&)>& displayName,
                                   const std::string& undisclosedName) {
  std::vector<Option> out;
  for (const std::string& raw : sdkCodes) {
    const std::string code = Lowercased(raw);
    if (code.empty()) continue;
    if (std::any_of(out.begin(), out.end(), [&](const Option& o) { return o.code == code; })) {
      continue;
    }
    std::string name = displayName ? displayName(code) : std::string();
    out.push_back(Option{code, name.empty() ? code : name});
  }
  out.push_back(Option{std::string(), undisclosedName});
  return out;
}

// The row of `stored`: a stored code the list no longer offers reads as
// Other / Undisclosed, the last row.
inline size_t SelectedIndex(const std::vector<Option>& options, const std::string& stored) {
  const std::string code = Lowercased(stored);
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i].code == code) return i;
  }
  return options.empty() ? 0 : options.size() - 1;
}

// The resolvers a region uses, for the picker to show: encrypted (DoH, ahead
// of the defaults) and the unencrypted last resort (URnetwork names only).
// Both empty is "Default resolvers".
struct Resolvers {
  std::vector<std::string> encrypted;
  std::vector<std::string> fallback;
  bool Default() const { return encrypted.empty() && fallback.empty(); }
};

// The picker's state over one store. The login control and the settings row
// each make one over the same store, so they read and write one value.
class Picker {
 public:
  Picker(Store& store, std::vector<Option> options) : store_(store), options_(std::move(options)) {}

  const std::vector<Option>& options() const { return options_; }
  // The stored value, read again on every call (the other door may have saved).
  std::string Stored() const { return store_.Get(); }
  size_t SelectedIndex() const { return region::SelectedIndex(options_, Stored()); }
  const Option& Selected() const { return options_[SelectedIndex()]; }
  // Saves row `index`; answers the stored value. Out of range saves nothing.
  std::string Select(size_t index) {
    if (index >= options_.size()) return Stored();
    return store_.Set(options_[index].code);
  }

 private:
  Store& store_;
  std::vector<Option> options_;
};

// ---- can't reach ------------------------------------------------------------

// Whether a failed call to URnetwork failed at the network level: the request
// got no answer (lookup, dial, tls or timeout), as against an answer that
// refused it (an http status, an auth or validation message). An api call that
// no route answered fails as "http request attempts exhausted" (connect
// HttpRequestExhaustedError), whose text names none of its causes; the other
// transport errors are Go's net and http errors. A server answer reads as
// "<status code> <status>: <body>" (connect HttpStatusError) or the server's
// own message.
inline bool IsCantReachError(const std::string& error) {
  if (error.empty()) return false;
  // an http status answer: the server was reached
  if (error.size() >= 4 && std::isdigit(static_cast<unsigned char>(error[0])) &&
      std::isdigit(static_cast<unsigned char>(error[1])) &&
      std::isdigit(static_cast<unsigned char>(error[2])) && error[3] == ' ') {
    return false;
  }
  const std::string lower = Lowercased(error);
  static const char* const kMarkers[] = {
      "http request attempts exhausted",
      "no such host",
      "could not resolve",
      "server misbehaving",
      "dial tcp",
      "dial udp",
      "i/o timeout",
      "connection refused",
      "connection reset",
      "network is unreachable",
      "no route to host",
      "host is down",
      "context deadline exceeded",
      "tls handshake timeout",
      "timeout awaiting response headers",
      "client.timeout exceeded",
  };
  for (const char* marker : kMarkers) {
    if (lower.find(marker) != std::string::npos) return true;
  }
  return false;
}

// The prompt's state. It reads failures and only shows or hides; it holds no
// store, so it cannot change the region (the user picks, in the picker).
class CantReachPrompt {
 public:
  bool visible() const { return visible_; }
  // A failed call: shows the prompt for a network-level failure. Any other
  // failure leaves it as it is.
  void OnFailure(const std::string& error) {
    if (IsCantReachError(error)) visible_ = true;
  }
  // A call that got an answer, or the user opened the picker.
  void Dismiss() { visible_ = false; }

 private:
  bool visible_ = false;
};

}  // namespace region
}  // namespace urnw
