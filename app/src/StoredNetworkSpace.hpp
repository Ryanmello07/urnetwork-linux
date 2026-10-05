// What a network space STORES, read off NetworkSpace::toJson() -- the only
// reading of the stored value set the C ABI offers.
//
// updateNetworkSpaceValues REPLACES a space's whole value set, so a writer that
// means to change one value has to hand back everything else the space stores.
// The getters cannot be that source: they answer EFFECTIVE values, so writing
// them back pins every derived default (extender.<host>, wss://gossip.<host>,
// the bundled root keys) as an explicit override, and a value with no getter
// at all (alt_url, sn_chain, the VLESS server) is silently dropped. The
// space's json carries exactly what was written -- an unset value stays unset
// -- so the writers start from it and change only what they mean to change.
//
// Pure and SDK-free on purpose: templated over the SDK's NetworkSpaceKey /
// NetworkSpaceValues / NetworkSpaceManager shapes and read through their
// nlohmann from_json, so the unit tests pin it with a json string
// (tests/VlessPresentationTest.cpp). NetworkSpaceConfig.hpp binds it to the
// real SDK types.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace urnw {

template <class Key, class Values>
struct StoredNetworkSpace {
  Key key;
  Values values;
};

// NetworkSpace::toJson() -- {"key": {...}, "values": {...}}, the SDK's
// ExportNetworkSpace -- as the SDK's own key and value types. nullopt when the
// document does not read or names no host: a default key names a DIFFERENT
// space, so a writer must refuse rather than write somewhere else. A document
// with no "values" is a space that stores nothing, which is not an error.
template <class Key, class Values>
inline std::optional<StoredNetworkSpace<Key, Values>> ParseStoredNetworkSpace(
    const std::string& spaceJson) {
  try {
    const nlohmann::json document = nlohmann::json::parse(spaceJson);
    if (!document.is_object()) return std::nullopt;
    StoredNetworkSpace<Key, Values> out{};
    if (auto it = document.find("key"); it != document.end() && it->is_object()) {
      it->get_to(out.key);
    }
    if (!out.key.host_name || out.key.host_name->empty()) return std::nullopt;
    if (auto it = document.find("values"); it != document.end() && it->is_object()) {
      it->get_to(out.values);
    }
    return out;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// The value set `manager` stores for `key`, or an empty set when it holds no
// space for the key (a first launch, a server this client never used) or the
// space's json does not read.
template <class Key, class Values, class Manager>
inline Values StoredNetworkSpaceValues(const Manager& manager, const Key& key) {
  try {
    const auto space = manager.getNetworkSpace(key);
    if (!space) return Values{};
    if (auto stored = ParseStoredNetworkSpace<Key, Values>(space.toJson())) {
      return std::move(stored->values);
    }
  } catch (const std::exception&) {
    // an unreadable space reads as an empty one, like an absent one
  }
  return Values{};
}

}  // namespace urnw
