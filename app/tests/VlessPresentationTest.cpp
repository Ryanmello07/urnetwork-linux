// The VLESS settings editor's logic (VlessPresentation.hpp): which rows show
// for a transport and a security, the form <-> settings mapping, the pickers'
// options, the error id -> message table and the save line -- and the stored
// network space values every space write starts from (StoredNetworkSpace.hpp),
// which is what keeps a saved VLESS server when the private extender or the
// network server is written. The texts are pinned against po/en.po; the GTK
// and SDK call sites are read as source, the way AddSignInWiringTest reads
// theirs.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "NetworkSpaceBootstrap.hpp"
#include "StoredNetworkSpace.hpp"
#include "VlessPresentation.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace sdkshape {

// The SDK's types (urnetwork_sdk.hpp), field for field, with its generated
// json conversions: an unset optional is left out, and an absent or null key
// leaves the field unset. sn_chain and net_extender carry plain fields, which
// the generated code always writes.
struct VlessSettings {
  std::optional<bool> enabled;
  std::optional<std::string> name;
  std::optional<std::string> address;
  std::optional<int64_t> port;
  std::optional<std::string> id;
  std::optional<std::string> flow;
  std::optional<std::string> network;
  std::optional<std::string> security;
  std::optional<std::string> server_name;
  std::optional<std::string> fingerprint;
  std::optional<std::string> alpn;
  std::optional<bool> allow_insecure;
  std::optional<std::string> public_key;
  std::optional<std::string> short_id;
  std::optional<std::string> spider_x;
  std::optional<std::string> path;
  std::optional<std::string> host;
};

struct SnChainSettings {
  int64_t chain_id{};
  std::string vault_address{};
  std::string coordinator_address{};
  std::string no_id{};
  int64_t netuid{};
  std::string explorer_tx_url{};
  std::string artifact_base_url{};
  std::string tx_type{};
  int64_t lookback_epochs{};
};

struct NetExtender {
  std::string ip{};
  std::string secret{};
};

struct NetworkSpaceKey {
  std::optional<std::string> host_name;
  std::optional<std::string> env_name;
};

struct NetworkSpaceValues {
  std::optional<std::string> env_secret;
  std::optional<bool> bundled;
  std::optional<bool> net_expose_server_ips;
  std::optional<bool> net_expose_server_host_names;
  std::optional<std::string> link_host_name;
  std::optional<std::string> migration_host_name;
  std::optional<std::string> store;
  std::optional<std::string> wallet;
  std::optional<bool> sso_google;
  std::optional<std::string> api_url;
  std::optional<std::string> platform_url;
  std::optional<std::string> alt_url;
  std::optional<SnChainSettings> sn_chain;
  std::optional<NetExtender> net_extender;
  std::optional<std::string> extender_dns_name;
  std::optional<std::string> gossip_url;
  std::optional<std::vector<std::string>> extender_root_public_keys;
  std::optional<std::vector<std::string>> extender_hosts;
  std::optional<VlessSettings> vless;
};

template <class T>
void Put(nlohmann::json& j, const char* name, const std::optional<T>& value) {
  if (value) j[name] = *value;
}

template <class T>
void Take(const nlohmann::json& j, const char* name, std::optional<T>& value) {
  if (auto it = j.find(name); it != j.end() && !it->is_null()) value = it->template get<T>();
}

template <class T>
void TakePlain(const nlohmann::json& j, const char* name, T& value) {
  if (auto it = j.find(name); it != j.end() && !it->is_null()) it->get_to(value);
}

void to_json(nlohmann::json& j, const VlessSettings& v) {
  j = nlohmann::json::object();
  Put(j, "enabled", v.enabled);
  Put(j, "name", v.name);
  Put(j, "address", v.address);
  Put(j, "port", v.port);
  Put(j, "id", v.id);
  Put(j, "flow", v.flow);
  Put(j, "network", v.network);
  Put(j, "security", v.security);
  Put(j, "server_name", v.server_name);
  Put(j, "fingerprint", v.fingerprint);
  Put(j, "alpn", v.alpn);
  Put(j, "allow_insecure", v.allow_insecure);
  Put(j, "public_key", v.public_key);
  Put(j, "short_id", v.short_id);
  Put(j, "spider_x", v.spider_x);
  Put(j, "path", v.path);
  Put(j, "host", v.host);
}

void from_json(const nlohmann::json& j, VlessSettings& v) {
  if (!j.is_object()) return;
  Take(j, "enabled", v.enabled);
  Take(j, "name", v.name);
  Take(j, "address", v.address);
  Take(j, "port", v.port);
  Take(j, "id", v.id);
  Take(j, "flow", v.flow);
  Take(j, "network", v.network);
  Take(j, "security", v.security);
  Take(j, "server_name", v.server_name);
  Take(j, "fingerprint", v.fingerprint);
  Take(j, "alpn", v.alpn);
  Take(j, "allow_insecure", v.allow_insecure);
  Take(j, "public_key", v.public_key);
  Take(j, "short_id", v.short_id);
  Take(j, "spider_x", v.spider_x);
  Take(j, "path", v.path);
  Take(j, "host", v.host);
}

void to_json(nlohmann::json& j, const SnChainSettings& v) {
  j = nlohmann::json::object();
  j["chain_id"] = v.chain_id;
  j["vault_address"] = v.vault_address;
  j["coordinator_address"] = v.coordinator_address;
  j["no_id"] = v.no_id;
  j["netuid"] = v.netuid;
  j["explorer_tx_url"] = v.explorer_tx_url;
  j["artifact_base_url"] = v.artifact_base_url;
  j["tx_type"] = v.tx_type;
  j["lookback_epochs"] = v.lookback_epochs;
}

void from_json(const nlohmann::json& j, SnChainSettings& v) {
  if (!j.is_object()) return;
  TakePlain(j, "chain_id", v.chain_id);
  TakePlain(j, "vault_address", v.vault_address);
  TakePlain(j, "coordinator_address", v.coordinator_address);
  TakePlain(j, "no_id", v.no_id);
  TakePlain(j, "netuid", v.netuid);
  TakePlain(j, "explorer_tx_url", v.explorer_tx_url);
  TakePlain(j, "artifact_base_url", v.artifact_base_url);
  TakePlain(j, "tx_type", v.tx_type);
  TakePlain(j, "lookback_epochs", v.lookback_epochs);
}

void to_json(nlohmann::json& j, const NetExtender& v) {
  j = nlohmann::json::object();
  j["ip"] = v.ip;
  j["secret"] = v.secret;
}

void from_json(const nlohmann::json& j, NetExtender& v) {
  if (!j.is_object()) return;
  TakePlain(j, "ip", v.ip);
  TakePlain(j, "secret", v.secret);
}

void to_json(nlohmann::json& j, const NetworkSpaceKey& v) {
  j = nlohmann::json::object();
  Put(j, "host_name", v.host_name);
  Put(j, "env_name", v.env_name);
}

void from_json(const nlohmann::json& j, NetworkSpaceKey& v) {
  if (!j.is_object()) return;
  Take(j, "host_name", v.host_name);
  Take(j, "env_name", v.env_name);
}

void to_json(nlohmann::json& j, const NetworkSpaceValues& v) {
  j = nlohmann::json::object();
  Put(j, "env_secret", v.env_secret);
  Put(j, "bundled", v.bundled);
  Put(j, "net_expose_server_ips", v.net_expose_server_ips);
  Put(j, "net_expose_server_host_names", v.net_expose_server_host_names);
  Put(j, "link_host_name", v.link_host_name);
  Put(j, "migration_host_name", v.migration_host_name);
  Put(j, "store", v.store);
  Put(j, "wallet", v.wallet);
  Put(j, "sso_google", v.sso_google);
  Put(j, "api_url", v.api_url);
  Put(j, "platform_url", v.platform_url);
  Put(j, "alt_url", v.alt_url);
  Put(j, "sn_chain", v.sn_chain);
  Put(j, "net_extender", v.net_extender);
  Put(j, "extender_dns_name", v.extender_dns_name);
  Put(j, "gossip_url", v.gossip_url);
  Put(j, "extender_root_public_keys", v.extender_root_public_keys);
  Put(j, "extender_hosts", v.extender_hosts);
  Put(j, "vless", v.vless);
}

void from_json(const nlohmann::json& j, NetworkSpaceValues& v) {
  if (!j.is_object()) return;
  Take(j, "env_secret", v.env_secret);
  Take(j, "bundled", v.bundled);
  Take(j, "net_expose_server_ips", v.net_expose_server_ips);
  Take(j, "net_expose_server_host_names", v.net_expose_server_host_names);
  Take(j, "link_host_name", v.link_host_name);
  Take(j, "migration_host_name", v.migration_host_name);
  Take(j, "store", v.store);
  Take(j, "wallet", v.wallet);
  Take(j, "sso_google", v.sso_google);
  Take(j, "api_url", v.api_url);
  Take(j, "platform_url", v.platform_url);
  Take(j, "alt_url", v.alt_url);
  Take(j, "sn_chain", v.sn_chain);
  Take(j, "net_extender", v.net_extender);
  Take(j, "extender_dns_name", v.extender_dns_name);
  Take(j, "gossip_url", v.gossip_url);
  Take(j, "extender_root_public_keys", v.extender_root_public_keys);
  Take(j, "extender_hosts", v.extender_hosts);
  Take(j, "vless", v.vless);
}

// A manager that holds at most one space, as its toJson() document.
struct Space {
  std::string json;
  explicit operator bool() const { return !json.empty(); }
  std::string toJson() const { return json; }
};

struct OneSpaceManager {
  std::string hostName;
  std::string json;
  Space getNetworkSpace(const std::optional<NetworkSpaceKey>& key) const {
    if (key && key->host_name == hostName && !json.empty()) return Space{json};
    return Space{};
  }
};

}  // namespace sdkshape

namespace {

using urnw::vless::Form;
using urnw::vless::Option;
using urnw::vless::Text;
using urnw::vless::Visibility;
using Settings = sdkshape::VlessSettings;
using Stored = urnw::StoredNetworkSpace<sdkshape::NetworkSpaceKey, sdkshape::NetworkSpaceValues>;

#define UR_EXPECT_TEXT(expected, actual)                                                    \
  do {                                                                                      \
    const std::string ur_e_ = (expected);                                                   \
    const std::string ur_a_ = (actual);                                                     \
    if (ur_e_ != ur_a_)                                                                     \
      UR_FAIL(std::string(#actual) + ": expected \"" + ur_e_ + "\", got \"" + ur_a_ + "\""); \
  } while (0)

std::optional<Stored> Parse(const std::string& json) {
  return urnw::ParseStoredNetworkSpace<sdkshape::NetworkSpaceKey, sdkshape::NetworkSpaceValues>(
      json);
}

std::string ReadVlessSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of `signature` (up to the next top-level definition), "" if absent.
std::string VlessFunctionBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

// A REALITY server as a share link reads (sdk vlessSettingsFromConfig): every
// field set, the spider path included.
Settings RealityServer() {
  Settings s;
  s.enabled = true;
  s.name = "home";
  s.address = "vless.example.test";
  s.port = 443;
  s.id = "b831381d-6324-4d53-ad4f-8cda48b30811";
  s.flow = urnw::vless::kFlowVision;
  s.network = urnw::vless::kNetworkTcp;
  s.security = urnw::vless::kSecurityReality;
  s.server_name = "www.example.com";
  s.fingerprint = "chrome";
  s.alpn = "";
  s.allow_insecure = false;
  s.public_key = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
  s.short_id = "6ba85179e30d4fc2";
  s.spider_x = "/spider";
  s.path = "";
  s.host = "";
  return s;
}

nlohmann::json Json(const Settings& s) { return nlohmann::json(s); }

// toJson() of a self-hosted space whose user saved a VLESS server, with the
// overrides nobody in this app writes (alt_url, sn_chain) and NOTHING derived:
// no api/platform url, no extender dns name, gossip url or root keys.
const char* kStoredSpaceJson = R"({
  "key": {"host_name": "example.test", "env_name": "main"},
  "values": {
    "net_expose_server_ips": true,
    "net_expose_server_host_names": true,
    "link_host_name": "example.test",
    "wallet": "circle",
    "alt_url": "https://alt.example.test:8443",
    "sn_chain": {"chain_id": 945, "vault_address": "0xvault", "coordinator_address": "0xcoord",
                 "no_id": "7", "netuid": 12, "explorer_tx_url": "https://scan.example.test/tx/%s",
                 "artifact_base_url": "", "tx_type": "legacy", "lookback_epochs": 4},
    "extender_hosts": ["ext1.example.test"],
    "vless": {"enabled": true, "name": "home", "address": "vless.example.test", "port": 443,
              "id": "b831381d-6324-4d53-ad4f-8cda48b30811", "flow": "xtls-rprx-vision",
              "network": "tcp", "security": "reality", "server_name": "www.example.com",
              "fingerprint": "chrome",
              "public_key": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8",
              "short_id": "6ba85179e30d4fc2", "spider_x": "/spider"}
  }
})";

}  // namespace

// ---- visibility -----------------------------------------------------------------

// Every transport x security, as a literal table rather than the expressions
// the rule is made of.
UR_TEST(VlessVisibility_EveryTransportAndSecurity) {
  struct Case {
    const char* network;
    const char* security;
    bool flow, serverName, fingerprint, alpn, allowInsecure, publicKey, shortId, path, host;
  };
  const Case cases[] = {
      // network, security       flow   sni    fp     alpn   insec  pbk    sid    path   host
      {"tcp", "none",            false, false, false, false, false, false, false, false, false},
      {"tcp", "tls",             true,  true,  true,  true,  true,  false, false, false, false},
      {"tcp", "reality",         true,  true,  true,  false, false, true,  true,  false, false},
      {"ws", "none",             false, false, false, false, false, false, false, true,  true},
      {"ws", "tls",              false, true,  true,  true,  true,  false, false, true,  true},
      {"ws", "reality",          false, true,  true,  false, false, true,  true,  true,  true},
      {"httpupgrade", "none",    false, false, false, false, false, false, false, true,  true},
      {"httpupgrade", "tls",     false, true,  true,  true,  true,  false, false, true,  true},
      {"httpupgrade", "reality", false, true,  true,  false, false, true,  true,  true,  true},
  };
  for (const Case& c : cases) {
    const Visibility v = urnw::vless::VisibilityFor(c.network, c.security);
    const std::string at = std::string(c.network) + "/" + c.security;
    UR_EXPECT_TRUE_MSG(at + " flow", v.flow == c.flow);
    UR_EXPECT_TRUE_MSG(at + " server name", v.serverName == c.serverName);
    UR_EXPECT_TRUE_MSG(at + " fingerprint", v.fingerprint == c.fingerprint);
    UR_EXPECT_TRUE_MSG(at + " alpn", v.alpn == c.alpn);
    UR_EXPECT_TRUE_MSG(at + " allow insecure", v.allowInsecure == c.allowInsecure);
    UR_EXPECT_TRUE_MSG(at + " public key", v.publicKey == c.publicKey);
    UR_EXPECT_TRUE_MSG(at + " short id", v.shortId == c.shortId);
    UR_EXPECT_TRUE_MSG(at + " path", v.path == c.path);
    UR_EXPECT_TRUE_MSG(at + " host", v.host == c.host);
  }
}

// The SDK's spelling: an empty transport is tcp, an empty security none, and
// the enumerations are trimmed and case-blind.
UR_TEST(VlessVisibility_EmptyAndMixedCaseValuesReadAsTheSdkDoes) {
  const Visibility empty = urnw::vless::VisibilityFor("", "");
  UR_EXPECT_FALSE(empty.flow);
  UR_EXPECT_FALSE(empty.serverName);
  UR_EXPECT_FALSE(empty.path);
  const Visibility mixed = urnw::vless::VisibilityFor(" TCP ", "Reality");
  UR_EXPECT_TRUE(mixed.flow);
  UR_EXPECT_TRUE(mixed.publicKey);
  UR_EXPECT_FALSE(mixed.alpn);
  const Visibility form = urnw::vless::VisibilityFor(Form{});
  UR_EXPECT_TRUE_MSG("the default form is reality over tcp", form.flow && form.shortId);
}

// ---- the form <-> settings mapping ------------------------------------------------

// A new form is sdk NewVlessSettings(): 443, tcp, reality, vision, chrome, off.
UR_TEST(VlessForm_DefaultsAreTheSdkNewFormSettings) {
  const Form form;
  UR_EXPECT_FALSE(form.enabled);
  UR_EXPECT_TEXT("443", form.port);
  UR_EXPECT_TEXT("tcp", form.network);
  UR_EXPECT_TEXT("reality", form.security);
  UR_EXPECT_TEXT("xtls-rprx-vision", form.flow);
  UR_EXPECT_TEXT("chrome", form.fingerprint);
}

UR_TEST(VlessMapping_TheFormReadsEverySetting) {
  const Form form = urnw::vless::FormFromSettings(RealityServer());
  UR_EXPECT_TRUE(form.enabled);
  UR_EXPECT_TEXT("home", form.name);
  UR_EXPECT_TEXT("vless.example.test", form.address);
  UR_EXPECT_TEXT("443", form.port);
  UR_EXPECT_TEXT("b831381d-6324-4d53-ad4f-8cda48b30811", form.id);
  UR_EXPECT_TEXT("xtls-rprx-vision", form.flow);
  UR_EXPECT_TEXT("tcp", form.network);
  UR_EXPECT_TEXT("reality", form.security);
  UR_EXPECT_TEXT("www.example.com", form.serverName);
  UR_EXPECT_TEXT("chrome", form.fingerprint);
  UR_EXPECT_TEXT("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8", form.publicKey);
  UR_EXPECT_TEXT("6ba85179e30d4fc2", form.shortId);
}

// settings -> form -> settings over the same base gives back every field, the
// spider path the form never shows included.
UR_TEST(VlessMapping_SettingsRoundTripKeepsEveryField) {
  Settings tls = RealityServer();
  tls.security = urnw::vless::kSecurityTls;
  tls.alpn = "h2,http/1.1";
  tls.allow_insecure = true;
  Settings ws = RealityServer();
  ws.network = urnw::vless::kNetworkWs;
  ws.security = urnw::vless::kSecurityNone;
  ws.flow = "";
  ws.path = "/ray";
  ws.host = "cdn.example.test";
  for (const Settings& settings : {RealityServer(), tls, ws}) {
    const Settings back =
        urnw::vless::SettingsFromForm(urnw::vless::FormFromSettings(settings), settings);
    UR_EXPECT_TRUE_MSG(Json(settings).dump() + " != " + Json(back).dump(),
                       Json(back) == Json(settings));
  }
}

// form -> settings -> form gives back the form.
UR_TEST(VlessMapping_FormRoundTripKeepsEveryField) {
  Form form;
  form.enabled = true;
  form.name = "office";
  form.address = "2001:db8::1";
  form.port = "8443";
  form.id = "custom-id";
  form.security = urnw::vless::kSecurityTls;
  form.serverName = "office.example.test";
  form.fingerprint = "firefox";
  form.alpn = "h2";
  form.allowInsecure = true;
  form.publicKey = "kept-though-hidden";
  const Form back =
      urnw::vless::FormFromSettings(urnw::vless::SettingsFromForm(form, Settings{}));
  UR_EXPECT_TRUE(back == form);
}

// A save writes over the settings the form was opened on, so what only links
// carry -- the REALITY spider path -- survives every save.
UR_TEST(VlessMapping_TheSpiderPathIsKept) {
  Form form = urnw::vless::FormFromSettings(RealityServer());
  form.name = "renamed";
  form.port = "8443";
  const Settings saved = urnw::vless::SettingsFromForm(form, RealityServer());
  UR_EXPECT_TRUE(saved.spider_x == std::optional<std::string>("/spider"));
  UR_EXPECT_TRUE(saved.name == std::optional<std::string>("renamed"));
  UR_EXPECT_TRUE(saved.port == std::optional<int64_t>(8443));
}

// A flow the form does not show is blanked: Vision anywhere but tcp with tls or
// reality fails validation, and a flow the user cannot see must not be what
// refuses the save.
UR_TEST(VlessMapping_AHiddenFlowIsBlanked) {
  Form ws = urnw::vless::FormFromSettings(RealityServer());
  ws.network = urnw::vless::kNetworkWs;
  UR_EXPECT_TRUE(urnw::vless::SettingsFromForm(ws, Settings{}).flow ==
                 std::optional<std::string>(""));
  Form none = urnw::vless::FormFromSettings(RealityServer());
  none.security = urnw::vless::kSecurityNone;
  UR_EXPECT_TRUE(urnw::vless::SettingsFromForm(none, Settings{}).flow ==
                 std::optional<std::string>(""));
  const Form shown = urnw::vless::FormFromSettings(RealityServer());
  UR_EXPECT_TRUE(urnw::vless::SettingsFromForm(shown, Settings{}).flow ==
                 std::optional<std::string>("xtls-rprx-vision"));
}

// Every OTHER hidden value is kept as typed: switching the security away and
// back loses nothing (the SDK ignores it, and a link leaves it out).
UR_TEST(VlessMapping_OtherHiddenValuesAreKept) {
  Form form = urnw::vless::FormFromSettings(RealityServer());
  form.security = urnw::vless::kSecurityNone;
  const Settings saved = urnw::vless::SettingsFromForm(form, Settings{});
  UR_EXPECT_TRUE(saved.server_name == std::optional<std::string>("www.example.com"));
  UR_EXPECT_TRUE(saved.public_key ==
                 std::optional<std::string>("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8"));
  UR_EXPECT_TRUE(saved.short_id == std::optional<std::string>("6ba85179e30d4fc2"));
}

// What the SDK stores for an empty or unusual spelling reads the way the SDK
// reads it, and an unset port leaves the field empty rather than "0".
UR_TEST(VlessMapping_UnsetValuesReadAsTheSdkDefaults) {
  Settings sparse;
  sparse.network = "";
  sparse.fingerprint = " Chrome ";
  const Form form = urnw::vless::FormFromSettings(sparse);
  UR_EXPECT_FALSE(form.enabled);
  UR_EXPECT_TEXT("", form.port);
  UR_EXPECT_TEXT("tcp", form.network);
  UR_EXPECT_TEXT("none", form.security);
  UR_EXPECT_TEXT("", form.flow);
  UR_EXPECT_TEXT("chrome", form.fingerprint);
}

// Digits only. Anything else is 0, which the SDK answers with "Enter a port
// from 1 to 65535."; a number past the range never wraps back into it.
UR_TEST(VlessPort_DigitsOnly) {
  UR_EXPECT_EQ(443, urnw::vless::ParsePort("443"));
  UR_EXPECT_EQ(8443, urnw::vless::ParsePort(" 8443 "));
  UR_EXPECT_EQ(65535, urnw::vless::ParsePort("65535"));
  UR_EXPECT_EQ(0, urnw::vless::ParsePort(""));
  UR_EXPECT_EQ(0, urnw::vless::ParsePort("   "));
  UR_EXPECT_EQ(0, urnw::vless::ParsePort("44a"));
  UR_EXPECT_EQ(0, urnw::vless::ParsePort("-1"));
  UR_EXPECT_EQ(0, urnw::vless::ParsePort("4 43"));
  UR_EXPECT_EQ(65536, urnw::vless::ParsePort("70000"));
  UR_EXPECT_EQ(65536, urnw::vless::ParsePort("99999999999999999999999"));
}

// ---- the pickers --------------------------------------------------------------------

// The values are the SDK's lists (sdk VlessNetworks, VlessSecurities,
// VlessFlows, VlessFingerprints), in their order.
UR_TEST(VlessOptions_AreTheSdkLists) {
  auto values = [](const std::vector<Option>& options) {
    std::vector<std::string> out;
    for (const Option& option : options) out.emplace_back(option.value);
    return out;
  };
  UR_EXPECT_TRUE(values(urnw::vless::NetworkOptions()) ==
                 (std::vector<std::string>{"tcp", "ws", "httpupgrade"}));
  UR_EXPECT_TRUE(values(urnw::vless::SecurityOptions()) ==
                 (std::vector<std::string>{"none", "tls", "reality"}));
  UR_EXPECT_TRUE(values(urnw::vless::FlowOptions()) ==
                 (std::vector<std::string>{"", "xtls-rprx-vision"}));
  UR_EXPECT_TRUE(values(urnw::vless::FingerprintOptions()) ==
                 (std::vector<std::string>{"", "chrome", "firefox", "safari", "ios", "android",
                                           "edge", "360", "qq", "random", "randomized"}));
}

// The fingerprints are shown as their raw names, except the empty one: None.
UR_TEST(VlessOptions_FingerprintsShowTheirRawNames) {
  for (const Option& option : urnw::vless::FingerprintOptions()) {
    if (option.value.empty()) {
      UR_EXPECT_TEXT("none", std::string(option.label.key));
      UR_EXPECT_TEXT("None", std::string(option.label.english));
    } else {
      UR_EXPECT_TRUE_MSG(std::string(option.value), option.label.key.empty());
      UR_EXPECT_TEXT(std::string(option.value), std::string(option.label.english));
    }
  }
}

UR_TEST(VlessOptions_IndexOfAValue) {
  UR_EXPECT_EQ(2, urnw::vless::OptionIndex(urnw::vless::SecurityOptions(), "reality"));
  UR_EXPECT_EQ(0, urnw::vless::OptionIndex(urnw::vless::FlowOptions(), ""));
  UR_EXPECT_EQ(-1, urnw::vless::OptionIndex(urnw::vless::NetworkOptions(), "grpc"));
}

// ---- the messages ---------------------------------------------------------------------

// All twelve sdk VlessError ids: each IS its message's key, with the store's
// English.
UR_TEST(VlessErrors_EveryIdHasItsMessage) {
  const std::pair<const char*, const char*> errors[] = {
      {"vless_error_link_invalid", "This is not a valid VLESS link."},
      {"vless_error_link_unsupported",
       "This link uses a VLESS feature this app does not support."},
      {"vless_error_address_invalid", "Enter the server address."},
      {"vless_error_port_invalid", "Enter a port from 1 to 65535."},
      {"vless_error_id_invalid", "Enter the user ID (a UUID)."},
      {"vless_error_network_unsupported",
       "This transport is not supported. Use TCP, WebSocket or HTTPUpgrade."},
      {"vless_error_security_unsupported",
       "This security type is not supported. Use TLS, REALITY or none."},
      {"vless_error_flow_invalid",
       "The Vision flow works only with the TCP transport and TLS or REALITY security."},
      {"vless_error_server_name_required", "Enter the server name (SNI) for REALITY."},
      {"vless_error_fingerprint_unsupported", "This TLS fingerprint is not supported."},
      {"vless_error_public_key_invalid", "Enter the REALITY public key."},
      {"vless_error_short_id_invalid",
       "The REALITY short ID must be up to 16 hexadecimal characters."},
  };
  std::set<std::string> keys;
  for (const auto& [id, english] : errors) {
    const Text text = urnw::vless::ErrorText(id);
    UR_EXPECT_TEXT(id, std::string(text.key));
    UR_EXPECT_TEXT(english, std::string(text.english));
    keys.insert(std::string(text.key));
  }
  UR_EXPECT_EQ(12, static_cast<int>(keys.size()));
}

// An id this build does not know -- the C ABI's internal one for a call that
// could not run, or a newer SDK's -- is no refusal the user caused: the generic
// message, never a raw key and never an invalid link. A save that never ran
// says so, and nothing reads as saved.
UR_TEST(VlessErrors_AnUnknownOrInternalIdReadsAsTheGenericMessage) {
  UR_EXPECT_TEXT("internal_error", std::string(urnw::vless::kErrorInternal));
  for (const char* id : {urnw::vless::kErrorInternal, "", "vless_error_from_the_future",
                         "VLESS_ERROR_PORT_INVALID"}) {
    const Text text = urnw::vless::ErrorText(id);
    UR_EXPECT_TEXT("something_went_wrong", std::string(text.key));
    UR_EXPECT_TEXT("Something went wrong.", std::string(text.english));
  }
  const auto failed = urnw::vless::SaveOutcomeFor(urnw::vless::kErrorInternal);
  UR_EXPECT_FALSE(failed.saved);
  UR_EXPECT_TEXT("something_went_wrong", std::string(failed.message.key));
  UR_EXPECT_TRUE(failed.note.key.empty());
}

// Saved carries the next-connect note (the VPN runs in urnetworkd, which reads
// the space at its next tunnel start); an error is its message and saved
// nothing.
UR_TEST(VlessSave_TheResultLine) {
  const auto saved = urnw::vless::SaveOutcomeFor("");
  UR_EXPECT_TRUE(saved.saved);
  UR_EXPECT_TEXT("vless_settings_saved", std::string(saved.message.key));
  UR_EXPECT_TEXT("vless_settings_next_connect", std::string(saved.note.key));
  const auto refused = urnw::vless::SaveOutcomeFor("vless_error_port_invalid");
  UR_EXPECT_FALSE(refused.saved);
  UR_EXPECT_TEXT("vless_error_port_invalid", std::string(refused.message.key));
  UR_EXPECT_TRUE(refused.note.key.empty());
}

// Every key and English the presentation hands the sheet is the catalog's,
// byte for byte (I18n.hpp: the English is the msgid the catalog is keyed on).
UR_TEST(VlessText_KeysAreTheCatalogs) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../po/en.po", std::ios::binary);
  UR_EXPECT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string catalog = buffer.str();

  std::vector<Text> texts;
  for (const auto& options :
       {urnw::vless::NetworkOptions(), urnw::vless::SecurityOptions(), urnw::vless::FlowOptions(),
        urnw::vless::FingerprintOptions()}) {
    for (const Option& option : options) {
      if (!option.label.key.empty()) texts.push_back(option.label);
    }
  }
  for (const char* id :
       {urnw::vless::kErrorLinkInvalid, urnw::vless::kErrorLinkUnsupported,
        urnw::vless::kErrorAddressInvalid, urnw::vless::kErrorPortInvalid,
        urnw::vless::kErrorIdInvalid, urnw::vless::kErrorNetworkUnsupported,
        urnw::vless::kErrorSecurityUnsupported, urnw::vless::kErrorFlowInvalid,
        urnw::vless::kErrorServerNameRequired, urnw::vless::kErrorFingerprintUnsupported,
        urnw::vless::kErrorPublicKeyInvalid, urnw::vless::kErrorShortIdInvalid}) {
    texts.push_back(urnw::vless::ErrorText(id));
  }
  texts.push_back(urnw::vless::ErrorText(urnw::vless::kErrorInternal));
  const auto saved = urnw::vless::SaveOutcomeFor("");
  texts.push_back(saved.message);
  texts.push_back(saved.note);

  std::set<std::string> keys;
  for (const Text& text : texts) {
    keys.insert(std::string(text.key));
    const std::string entry = "msgctxt \"" + std::string(text.key) + "\"\nmsgid \"" +
                              std::string(text.english) + "\"\n";
    UR_EXPECT_TRUE_MSG(entry, catalog.find(entry) != std::string::npos);
  }
  // 3 networks, none (shared by security, flow and fingerprint), tls, reality,
  // vision, the 12 errors, the generic message, saved and the next-connect note
  UR_EXPECT_EQ(22, static_cast<int>(keys.size()));
}

// ---- the stored network space values ---------------------------------------------------

// The read is the space's json, and the json is what was STORED: the VLESS
// server, alt_url and sn_chain come back whole.
UR_TEST(StoredSpace_VlessAltUrlAndSnChainSurvive) {
  const auto stored = Parse(kStoredSpaceJson);
  UR_EXPECT_TRUE(stored.has_value());
  if (!stored) return;
  UR_EXPECT_TRUE(stored->key.host_name == std::optional<std::string>("example.test"));
  UR_EXPECT_TRUE(stored->key.env_name == std::optional<std::string>("main"));
  const auto& values = stored->values;
  UR_EXPECT_TRUE(values.alt_url == std::optional<std::string>("https://alt.example.test:8443"));
  UR_EXPECT_TRUE(values.sn_chain.has_value() && values.sn_chain->vault_address == "0xvault" &&
                 values.sn_chain->chain_id == 945 && values.sn_chain->lookback_epochs == 4);
  UR_EXPECT_TRUE(values.vless.has_value());
  if (values.vless) {
    UR_EXPECT_TRUE_MSG("the stored server reads back field for field",
                       Json(*values.vless) ==
                           nlohmann::json::parse(kStoredSpaceJson)["values"]["vless"]);
    UR_EXPECT_TRUE(values.vless->enabled == std::optional<bool>(true));
    UR_EXPECT_TRUE(values.vless->spider_x == std::optional<std::string>("/spider"));
  }
}

// Nothing the space did not store appears: the values a getter would answer
// with their DERIVED defaults (the api and platform urls, the extender dns
// name, the gossip url, the root keys) stay unset, so writing the set back
// cannot pin a default as an override -- and the write-back is the stored
// document exactly.
UR_TEST(StoredSpace_DefaultsAreNotTurnedIntoOverrides) {
  const auto stored = Parse(kStoredSpaceJson);
  UR_EXPECT_TRUE(stored.has_value());
  if (!stored) return;
  const auto& values = stored->values;
  UR_EXPECT_FALSE(values.api_url.has_value());
  UR_EXPECT_FALSE(values.platform_url.has_value());
  UR_EXPECT_FALSE(values.extender_dns_name.has_value());
  UR_EXPECT_FALSE(values.gossip_url.has_value());
  UR_EXPECT_FALSE(values.extender_root_public_keys.has_value());
  UR_EXPECT_FALSE(values.net_extender.has_value());
  UR_EXPECT_FALSE(values.bundled.has_value());
  const nlohmann::json written = values;
  UR_EXPECT_TRUE_MSG(written.dump(),
                     written == nlohmann::json::parse(kStoredSpaceJson)["values"]);
}

// SdkHost::SetPrivateExtender's write: the stored set with the private
// extender changed, and nothing else.
UR_TEST(StoredSpace_APrivateExtenderWriteChangesOnlyThatValue) {
  const auto stored = Parse(kStoredSpaceJson);
  UR_EXPECT_TRUE(stored.has_value());
  if (!stored) return;
  auto values = stored->values;
  values.net_extender = sdkshape::NetExtender{"192.0.2.7", "s3cret"};
  nlohmann::json written = values;
  UR_EXPECT_TRUE(written["net_extender"] ==
                 nlohmann::json({{"ip", "192.0.2.7"}, {"secret", "s3cret"}}));
  written.erase("net_extender");
  UR_EXPECT_TRUE_MSG(written.dump(),
                     written == nlohmann::json::parse(kStoredSpaceJson)["values"]);
}

// SdkHost::ApplyNetworkServer's write when the sheet re-applies the server in
// force: the host's values and the url overrides change, and the VLESS server,
// alt_url, sn_chain and the private extender stay.
UR_TEST(StoredSpace_ReapplyingTheServerKeepsVless) {
  auto stored = Parse(kStoredSpaceJson);
  UR_EXPECT_TRUE(stored.has_value());
  if (!stored) return;
  stored->values.net_extender = sdkshape::NetExtender{"192.0.2.7", "s3cret"};
  stored->values.api_url = "https://old-api.example.test";
  auto values = urnw::UrNetworkSpaceValuesOver(stored->values, false, "example.test");
  values.bundled = false;
  values.api_url = "";
  values.platform_url = "wss://connect2.example.test";
  UR_EXPECT_TRUE(values.vless.has_value() &&
                 values.vless->address == std::optional<std::string>("vless.example.test") &&
                 values.vless->enabled == std::optional<bool>(true));
  UR_EXPECT_TRUE(values.alt_url == std::optional<std::string>("https://alt.example.test:8443"));
  UR_EXPECT_TRUE(values.sn_chain.has_value() && values.sn_chain->no_id == "7");
  UR_EXPECT_TRUE(values.net_extender.has_value() && values.net_extender->ip == "192.0.2.7");
  UR_EXPECT_TRUE(values.extender_hosts ==
                 std::optional<std::vector<std::string>>(std::vector<std::string>{
                     "ext1.example.test"}));
  // ...and what the sheet decides is what it says
  UR_EXPECT_TRUE(values.link_host_name == std::optional<std::string>("example.test"));
  UR_EXPECT_TRUE(values.migration_host_name == std::optional<std::string>(""));
  UR_EXPECT_TRUE(values.api_url == std::optional<std::string>(""));
  UR_EXPECT_TRUE(values.platform_url == std::optional<std::string>("wss://connect2.example.test"));
}

// A document that does not read, or names no host, refuses: a default key
// names a DIFFERENT space. A space with no values stores nothing, which reads.
UR_TEST(StoredSpace_UnreadableDocumentsRefuse) {
  for (const char* json :
       {"", "not json", "[]", "null", R"({"values": {"wallet": "circle"}})",
        R"({"key": {"host_name": ""}, "values": {}})", R"({"key": "example.test"})"}) {
    UR_EXPECT_TRUE_MSG(json, !Parse(json).has_value());
  }
  const auto bare = Parse(R"({"key": {"host_name": "example.test", "env_name": "main"}})");
  UR_EXPECT_TRUE(bare.has_value());
  UR_EXPECT_TRUE(bare && nlohmann::json(bare->values) == nlohmann::json::object());
  // a value of the wrong type is a document that does not read, not a crash
  UR_EXPECT_FALSE(
      Parse(R"({"key": {"host_name": "example.test"}, "values": {"vless": {"port": "x"}}})")
          .has_value());
}

// The manager read: the stored set for a space it holds, an empty set for a
// key it holds no space for.
UR_TEST(StoredSpace_TheManagerReadIsEmptyWithoutASpace) {
  sdkshape::OneSpaceManager manager{"example.test", kStoredSpaceJson};
  sdkshape::NetworkSpaceKey held;
  held.host_name = "example.test";
  held.env_name = "main";
  const auto values =
      urnw::StoredNetworkSpaceValues<sdkshape::NetworkSpaceKey, sdkshape::NetworkSpaceValues>(
          manager, held);
  UR_EXPECT_TRUE(values.vless.has_value());
  sdkshape::NetworkSpaceKey other;
  other.host_name = "other.test";
  other.env_name = "main";
  const auto none =
      urnw::StoredNetworkSpaceValues<sdkshape::NetworkSpaceKey, sdkshape::NetworkSpaceValues>(
          manager, other);
  UR_EXPECT_TRUE(nlohmann::json(none) == nlohmann::json::object());
}

// ---- the call sites ----------------------------------------------------------------------

// The two space writers in the host start from the stored values: the private
// extender reads the space's json (never the getters, which answer derived
// defaults and know nothing of VLESS), and the network-server switch reads
// what the manager stores under the key it writes.
UR_TEST(VlessWiring_SpaceWritesStartFromTheStoredValues) {
  const std::string host = ReadVlessSource("SdkHost.cpp");
  UR_EXPECT_TRUE(!host.empty());
  const std::string extender =
      VlessFunctionBody(host, "bool SdkHost::SetPrivateExtender(");
  UR_EXPECT_TRUE(!extender.empty());
  UR_EXPECT_TRUE(Contains(extender, "ParseStoredNetworkSpace(networkSpace_->toJson())"));
  UR_EXPECT_TRUE(Contains(extender, "updateNetworkSpaceValues(stored->key, values)"));
  for (const char* getter :
       {"getExtenderDnsName", "getGossipUrl", "getExtenderRootPublicKeys", "getLinkHostName",
        "getConfiguredApiUrl", "getBundled"}) {
    UR_EXPECT_TRUE_MSG(getter, !Contains(extender, getter));
  }
  const std::string apply = VlessFunctionBody(host, "bool SdkHost::ApplyNetworkServer(");
  UR_EXPECT_TRUE(Contains(apply, "StoredNetworkSpaceValues(*spaceManager_, key)"));
  // the VLESS writes go through the space's own setter, which applies in place
  const std::string save = VlessFunctionBody(host, "SdkHost::SetVlessSettings(");
  UR_EXPECT_TRUE(Contains(save, "networkSpace_->setVlessSettings(settings)"));
  UR_EXPECT_TRUE_MSG("a VLESS save does not rebuild the space",
                     !Contains(save, "updateNetworkSpaceValues"));
}

// A check that could not run answers what the C ABI answers for a call that
// could not run, so it reads as the generic message: never a pass, and never
// an invalid link the user did not paste.
UR_TEST(VlessWiring_AValidateThatThrowsAnswersTheInternalId) {
  const std::string host = ReadVlessSource("SdkHost.cpp");
  const std::string validate =
      VlessFunctionBody(host, "std::string SdkHost::ValidateVlessSettings(");
  UR_EXPECT_TRUE(!validate.empty());
  UR_EXPECT_TRUE(Contains(validate, "return vless::kErrorInternal;"));
  UR_EXPECT_TRUE_MSG("a validate that throws reads as an invalid link",
                     !Contains(validate, "VlessErrorLinkInvalid"));
}

// Both doors open the same editor: the Settings row and the login screen's
// network sheet.
UR_TEST(VlessWiring_BothDoorsOpenTheEditor) {
  const std::string settings = ReadVlessSource("SettingsPage.cpp");
  UR_EXPECT_TRUE(Contains(settings, "MakePaneTwoLineRowButton(T_(\"vless\", \"VLESS\")"));
  UR_EXPECT_TRUE(Contains(settings, "std::make_unique<VlessSheet>(*root, host_)"));
  const std::string login = ReadVlessSource("NetworkServerSheet.cpp");
  UR_EXPECT_TRUE(Contains(login, "MakePaneTwoLineRowButton(T_(\"vless\", \"VLESS\"))"));
  UR_EXPECT_TRUE(Contains(login, "std::make_unique<VlessSheet>(*this, sdk_)"));
  // the editor saves through the host and reads back what was stored
  const std::string sheet = ReadVlessSource("VlessSheet.cpp");
  const std::string save = VlessFunctionBody(sheet, "void VlessSheet::Save(");
  UR_EXPECT_TRUE(Contains(save, "host_.SetVlessSettings(CurrentSettings())"));
  UR_EXPECT_TRUE(Contains(save, "host_.GetVlessSettings()"));
  UR_EXPECT_TRUE(Contains(VlessFunctionBody(sheet, "void VlessSheet::Load("),
                          "host_.GetVlessSettings()"));
}
