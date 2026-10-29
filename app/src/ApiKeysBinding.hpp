// The API keys page's half of its requests (sdk Api listApiKeys,
// createApiKey and deleteApiKey), over the calls it makes, so their order,
// their guards and their give-ups are tested with a fake service
// (tests/ApiKeysBindingTest.cpp) and the page carries none of it:
//
//   - Attach adopts the service of the account's Api; a different Api later
//     (a server or extender change replaces it) gets a service of its own.
//   - Refresh lists the keys, unless a list request is out already. Create
//     sends a name and hands the key the server returns to its caller, once;
//     the binding keeps no copy. The list is asked again after every create:
//     it shows the new key, and a key a failed or given-up create made
//     anyway. Delete removes one key, one request per key at a time; a
//     deleted key leaves the list at once, and a list that was already on its
//     way when it went cannot bring it back.
//   - Every request is given up on after Settings::requestTimeoutMs: the sdk
//     is known to drop callbacks, and a request that never answers must not
//     leave its progress up for good. The give-up is final: the late answer
//     is dropped, and a dropped create answer's key is wiped unread.
//   - Release drops the service and orphans every answer still on its way: an
//     answer carries the token of its request, and only a request still
//     pending takes one. No answer lands after a release, a sign-out or the
//     page's destruction, and a new account's page never shows the previous
//     account's keys.
//
// Pure C++17 (no GTK, no SDK); ApiKeysPage.cpp adapts the sdk's Api to
// Service and the main loop's timer to Schedule. Main loop only.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ApiKeysPresentation.hpp"

namespace urnw::api_keys {

// The page's tunables (connect/CODESTYLE.md "Tunables live in settings").
struct Settings {
  // How long a request may go unanswered before it is given up on, in ms.
  // 20 s is the app's give-up for a plain api call (docs/parity/CONTRACT.md
  // rule 3, AccountPage's AccountFlow). Non-positive never gives up.
  int requestTimeoutMs = 0;
  // How long Copy key reads Copied after a press, in ms: long enough to read,
  // short enough that a second press is answered again. Non-positive keeps
  // Copied until the sheet closes.
  int copiedMs = 0;
};

inline Settings DefaultSettings() {
  Settings settings;
  settings.requestTimeoutMs = 20 * 1000;
  settings.copiedMs = 2 * 1000;
  return settings;
}

// An answer, as plain data. `ok`: the sdk's result arrived with neither a
// transport error nor the server's error.
struct ListAnswer {
  bool ok = false;
  std::vector<Key> keys;
};

struct CreateAnswer {
  bool ok = false;
  std::string apiKey;  // the key, the one time the server returns it
};

struct DeleteAnswer {
  bool ok = false;
};

// The api calls (sdk Api). Each answers at most once, on the main loop.
class Service {
 public:
  virtual ~Service() = default;
  virtual void List(std::function<void(ListAnswer)> done) = 0;
  virtual void Create(const std::string& name, std::function<void(CreateAnswer)> done) = 0;
  virtual void Delete(const std::string& keyId, std::function<void(DeleteAnswer)> done) = 0;
};

// Runs `fire` once, `delayMs` from now, on the main loop.
using Schedule = std::function<void(int delayMs, std::function<void()> fire)>;

// A create's outcome for its caller: ok with the key, handed over once, or a
// failure with no key.
using CreateDone = std::function<void(bool ok, std::string apiKey)>;

class Binding {
 public:
  Binding(Settings settings, Schedule schedule)
      : settings_(settings), schedule_(std::move(schedule)) {}
  ~Binding() { Release(); }
  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;

  // After every change of the snapshot.
  std::function<void()> on_changed;

  // `source` names what the service calls (the Api's handle): a different one
  // later is a different Api, which needs a service of its own. A service
  // still attached is released first.
  void Attach(std::unique_ptr<Service> service, uint64_t source) {
    Release();
    if (!service) return;
    service_ = std::move(service);
    source_ = source;
  }

  bool Attached() const { return service_ != nullptr; }
  uint64_t Source() const { return source_; }

  // Detached: every answer still on its way is dropped, and the departed
  // account's keys go with it. Nothing is announced (the page renders after).
  void Release() {
    listToken_ = 0;
    createToken_ = 0;
    deleteTokens_.clear();
    listAgain_ = false;
    deletedDuringList_.clear();
    createDone_ = nullptr;
    snapshot_ = Snapshot{};
    source_ = 0;
    service_.reset();
  }

  const Snapshot& Read() const { return snapshot_; }

  // Lists the keys. False when nothing was sent: no service, or a list is out
  // already (its answer is new enough).
  bool Refresh() {
    if (!service_ || listToken_ != 0) return false;
    const uint64_t token = ++tokens_;
    listToken_ = token;
    // a key deleted from here on is gone from the list this request returns
    deletedDuringList_.clear();
    snapshot_.loading = true;
    Changed();
    service_->List([this, token](ListAnswer answer) { OnList(token, std::move(answer)); });
    GiveUpLater([this, token] { OnList(token, ListAnswer{}); });
    return true;
  }

  // Creates a key named `name` (NameToCreate's). `done` hears the outcome
  // once. False when nothing was sent: no service, a create is out already,
  // or the name cannot be sent.
  bool Create(const std::string& name, CreateDone done) {
    const std::optional<std::string> sendable = NameToCreate(name);
    if (!service_ || createToken_ != 0 || !sendable) return false;
    const uint64_t token = ++tokens_;
    createToken_ = token;
    createDone_ = std::move(done);
    snapshot_.creating = true;
    Changed();
    service_->Create(*sendable,
                     [this, token](CreateAnswer answer) { OnCreate(token, std::move(answer)); });
    GiveUpLater([this, token] { OnCreate(token, CreateAnswer{}); });
    return true;
  }

  // Deletes one key. False when nothing was sent: no service, no id, or that
  // key's delete is out already.
  bool Delete(const std::string& keyId) {
    if (!service_ || keyId.empty() || deleteTokens_.count(keyId) != 0) return false;
    const uint64_t token = ++tokens_;
    deleteTokens_[keyId] = token;
    Erase(snapshot_.deleteFailedIds, keyId);
    snapshot_.deletingIds.push_back(keyId);
    Changed();
    service_->Delete(keyId, [this, token, keyId](DeleteAnswer answer) {
      OnDelete(token, keyId, answer);
    });
    GiveUpLater([this, token, keyId] { OnDelete(token, keyId, DeleteAnswer{}); });
    return true;
  }

 private:
  static void Erase(std::vector<std::string>& ids, const std::string& id) {
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
  }

  void Changed() {
    if (on_changed) on_changed();
  }

  void GiveUpLater(std::function<void()> giveUp) {
    if (settings_.requestTimeoutMs <= 0 || !schedule_) return;
    schedule_(settings_.requestTimeoutMs, std::move(giveUp));
  }

  // The list's answer, or its give-up (a failed answer with the same token).
  void OnList(uint64_t token, ListAnswer answer) {
    if (token == 0 || token != listToken_) return;
    listToken_ = 0;
    snapshot_.loading = false;
    if (answer.ok) {
      std::vector<Key> keys;
      keys.reserve(answer.keys.size());
      for (Key& key : answer.keys) {
        // a key without an id can be neither told apart nor deleted, and a
        // key deleted while this list was on its way is gone
        if (key.id.empty() || Has(deletedDuringList_, key.id)) continue;
        keys.push_back(std::move(key));
      }
      SortKeys(keys);
      snapshot_.keys = std::move(keys);
      snapshot_.loaded = true;
      snapshot_.loadFailed = false;
      // a failed delete's line belongs to a key still listed
      std::vector<std::string> failed;
      for (const std::string& id : snapshot_.deleteFailedIds) {
        const bool listed = std::any_of(snapshot_.keys.begin(), snapshot_.keys.end(),
                                        [&](const Key& key) { return key.id == id; });
        if (listed) failed.push_back(id);
      }
      snapshot_.deleteFailedIds = std::move(failed);
    } else {
      snapshot_.loadFailed = true;
    }
    deletedDuringList_.clear();
    Changed();
    if (listAgain_) {
      listAgain_ = false;
      Refresh();
    }
  }

  // The create's answer, or its give-up. A key no request waits for is wiped
  // unread.
  void OnCreate(uint64_t token, CreateAnswer answer) {
    if (token == 0 || token != createToken_) {
      Wipe(answer.apiKey);
      return;
    }
    createToken_ = 0;
    snapshot_.creating = false;
    CreateDone done = std::move(createDone_);
    createDone_ = nullptr;
    const bool ok = answer.ok && !answer.apiKey.empty();
    if (!ok) Wipe(answer.apiKey);
    Changed();
    if (done) done(ok, std::move(answer.apiKey));
    Wipe(answer.apiKey);
    // the list again, for the new key or one a failed create made anyway; a
    // list already out may have been read before the key was made
    if (listToken_ != 0) {
      listAgain_ = true;
    } else {
      Refresh();
    }
  }

  // A delete's answer, or its give-up.
  void OnDelete(uint64_t token, const std::string& keyId, const DeleteAnswer& answer) {
    const auto pending = deleteTokens_.find(keyId);
    if (pending == deleteTokens_.end() || pending->second != token) return;
    deleteTokens_.erase(pending);
    Erase(snapshot_.deletingIds, keyId);
    if (answer.ok) {
      snapshot_.keys.erase(std::remove_if(snapshot_.keys.begin(), snapshot_.keys.end(),
                                          [&](const Key& key) { return key.id == keyId; }),
                           snapshot_.keys.end());
      if (listToken_ != 0) deletedDuringList_.push_back(keyId);
    } else {
      snapshot_.deleteFailedIds.push_back(keyId);
    }
    Changed();
  }

  Settings settings_;
  Schedule schedule_;
  std::unique_ptr<Service> service_;
  uint64_t source_ = 0;
  Snapshot snapshot_;
  uint64_t tokens_ = 0;       // requests sent so far; never reused, a release included
  uint64_t listToken_ = 0;    // the list request out, or 0
  uint64_t createToken_ = 0;  // the create request out, or 0
  std::map<std::string, uint64_t> deleteTokens_;  // the delete out per key id
  CreateDone createDone_;     // the create request's caller
  bool listAgain_ = false;    // list again once the list out has answered
  std::vector<std::string> deletedDuringList_;  // deleted while the list out was on its way
};

}  // namespace urnw::api_keys
