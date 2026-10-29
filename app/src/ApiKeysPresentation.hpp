// Account -> API keys, decided without GTK or the SDK: what a key's row says,
// what the screen shows in each state of its list, which name a create sends,
// what the create sheet shows at each step and how long it holds the key it
// was given, and what a delete asks before it is sent. ApiKeysPage.cpp copies
// the sdk's answers into the plain data below on the GTK main loop and renders
// what these functions return; it holds no rule of its own
// (tests/ApiKeysPresentationTest.cpp).
//
// An API key is a full account credential: the server shows it once, in the
// answer to its create, and keeps only its hash. The app shows it once too,
// in the create sheet, and keeps it nowhere else: CreateFlow holds it while
// the sheet shows it and wipes it when the sheet closes. It is never written
// to settings, files, logs or telemetry.
//
// Every string is a store key and its English (the gettext msgid), which the
// page looks up; tests/CatalogLookupTest.cpp reads each {key, English} pair
// here. A key's name is text from outside the app (any API client can name a
// key), so it is shown filtered, as plain text, never markup.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "DisplayText.hpp"

namespace urnw::api_keys {

// A store key and its English.
struct Text {
  const char* key = "";
  const char* english = "";
};

// ---- the list, as plain data ----------------------------------------------------

// sdk PublicAccountApiKey: everything the server says about a key but the key.
struct Key {
  std::string id;
  std::string name;               // as the server stores it; "" when it has none
  int64_t createUnixSeconds = 0;  // UTC Unix SECONDS; 0 when the time did not read
};

// The page's list and the requests about it.
struct Snapshot {
  std::vector<Key> keys;                     // in display order (SortKeys)
  bool loaded = false;                       // a list has landed at least once
  bool loading = false;                      // a list request is out
  bool loadFailed = false;                   // the latest list request failed or was given up on
  bool creating = false;                     // a create request is out
  std::vector<std::string> deletingIds;      // delete requests out
  std::vector<std::string> deleteFailedIds;  // keys whose latest delete failed
};

// ---- the words ------------------------------------------------------------------

inline constexpr Text kTitleText{"api_keys_title", "API keys"};
inline constexpr Text kIntroText{
    "api_keys_intro",
    "API keys let your own tools and scripts use your URnetwork account through the API. Anyone "
    "with a key has full access to your account until you delete it."};
inline constexpr Text kDocsLinkText{"api_keys_docs_link", "How to use API keys"};
inline constexpr Text kEmptyText{"api_keys_empty", "You don't have any API keys."};
// the screen's button, and the create sheet's title and its confirm button
inline constexpr Text kCreateText{"api_keys_create", "Create API key"};
inline constexpr Text kNameLabelText{"api_keys_name_label", "Key name"};
inline constexpr Text kNamePlaceholderText{"api_keys_name_placeholder",
                                           "For example, home server"};
inline constexpr Text kCreatedTitleText{"api_keys_created_title", "Your new API key"};
inline constexpr Text kCreatedWarningText{
    "api_keys_created_warning",
    "Copy this key now. It won't be shown again, and anyone who has it can use your account."};
inline constexpr Text kCopyText{"api_keys_copy", "Copy key"};
// in place of Copy key for a moment after the key was copied
inline constexpr Text kCopiedText{"api_keys_copied", "Copied"};
inline constexpr Text kCreatedAtText{"api_keys_created_at", "Created {}"};
inline constexpr Text kIdText{"api_keys_id", "ID {}"};
inline constexpr Text kUnnamedText{"api_keys_unnamed", "Unnamed key"};
// a row's action, and the confirmation's destructive button
inline constexpr Text kDeleteText{"api_keys_delete", "Delete"};
inline constexpr Text kDeleteConfirmTitleText{"api_keys_delete_confirm_title",
                                              "Delete this API key?"};
inline constexpr Text kDeleteConfirmBodyText{
    "api_keys_delete_confirm_body",
    "\"{}\" will stop working right away, and anything using it will lose access."};
inline constexpr Text kLoadFailedText{"api_keys_load_failed",
                                      "Couldn't load your API keys. Try again."};
inline constexpr Text kCreateFailedText{"api_keys_create_failed",
                                        "Couldn't create the API key. Try again."};
inline constexpr Text kDeleteFailedText{"api_keys_delete_failed",
                                        "Couldn't delete the API key. Try again."};
inline constexpr Text kCancelText{"cancel", "Cancel"};
inline constexpr Text kDoneText{"done", "Done"};
inline constexpr Text kRetryText{"retry", "Retry"};
inline constexpr Text kRefreshText{"refresh", "Refresh"};
inline constexpr Text kLoadingText{"loading", "Loading..."};
inline constexpr Text kSomethingWentWrongText{"something_went_wrong", "Something went wrong."};
// the header's way back, "‹ Account"
inline constexpr Text kAccountText{"account", "Account"};

// The API's documentation, which the intro links to.
inline constexpr const char* kDocsUrl = "https://ur.io/docs/api";

// What the platform lends the texts: the catalog (T_), the store's {}
// substitution (I18n.hpp FormatArgs), and the renderings of a time
// (Formatters.hpp LocalDate, LocalDateTime).
struct Words {
  std::function<std::string(const Text&)> lookup;
  std::function<std::string(const std::string& pattern, const std::vector<std::string>& args)>
      format;
  std::function<std::string(int64_t unixSeconds)> date;
  std::function<std::string(int64_t unixSeconds)> dateTime;
};

// ---- the sdk's fields --------------------------------------------------------------

namespace detail {

// `count` decimal digits at `at`, or nullopt.
inline std::optional<int> Digits(const std::string& text, size_t at, size_t count) {
  if (at + count > text.size()) return std::nullopt;
  int value = 0;
  for (size_t i = at; i < at + count; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
    value = value * 10 + (text[i] - '0');
  }
  return value;
}

// Days from 1970-01-01 to the proleptic Gregorian date (H. Hinnant's
// days_from_civil).
inline int64_t DaysFromCivil(int64_t year, int month, int day) {
  year -= month <= 2 ? 1 : 0;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const int64_t yearOfEra = year - era * 400;
  const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + dayOfEra - 719468;
}

inline int DaysInMonth(int year, int month) {
  static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : kDays[month - 1];
}

}  // namespace detail

// The server's create_time, an RFC 3339 time as Go writes a time.Time in JSON
// ("2026-10-29T15:04:05.123456789Z", or with an offset "-05:00"), as UTC Unix
// seconds; nullopt when it does not read. A fraction of a second is dropped,
// and a leap second reads as the second before it.
inline std::optional<int64_t> ParseRfc3339UnixSeconds(const std::string& text) {
  using detail::Digits;
  const std::optional<int> year = Digits(text, 0, 4);
  const std::optional<int> month = Digits(text, 5, 2);
  const std::optional<int> day = Digits(text, 8, 2);
  const std::optional<int> hour = Digits(text, 11, 2);
  const std::optional<int> minute = Digits(text, 14, 2);
  const std::optional<int> second = Digits(text, 17, 2);
  if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;
  if (text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != 't') ||
      text[13] != ':' || text[16] != ':') {
    return std::nullopt;
  }
  if (*month < 1 || *month > 12 || *day < 1 || *day > detail::DaysInMonth(*year, *month) ||
      *hour > 23 || *minute > 59 || *second > 60) {
    return std::nullopt;
  }
  size_t at = 19;
  if (at < text.size() && text[at] == '.') {
    const size_t digits = ++at;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
    if (at == digits) return std::nullopt;
  }
  if (at >= text.size()) return std::nullopt;
  int64_t offsetSeconds = 0;
  if (text[at] == 'Z' || text[at] == 'z') {
    ++at;
  } else if (text[at] == '+' || text[at] == '-') {
    const std::optional<int> offsetHours = Digits(text, at + 1, 2);
    const std::optional<int> offsetMinutes = Digits(text, at + 4, 2);
    if (!offsetHours || !offsetMinutes || text[at + 3] != ':' || *offsetHours > 23 ||
        *offsetMinutes > 59) {
      return std::nullopt;
    }
    offsetSeconds = (text[at] == '-' ? -1 : 1) * (*offsetHours * 3600 + *offsetMinutes * 60);
    at += 6;
  } else {
    return std::nullopt;
  }
  if (at != text.size()) return std::nullopt;
  const int64_t days = detail::DaysFromCivil(*year, *month, *day);
  return days * 86400 + *hour * 3600 + *minute * 60 + std::min(*second, 59) - offsetSeconds;
}

// Newest first: by creation time, then by id, which the server mints as a
// time-ordered ULID; a key whose time did not read comes after the others.
// The server lists them in no order.
inline void SortKeys(std::vector<Key>& keys) {
  std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
    if (a.createUnixSeconds != b.createUnixSeconds) {
      return a.createUnixSeconds > b.createUnixSeconds;
    }
    return a.id > b.id;
  });
}

// ---- names --------------------------------------------------------------------------

// The server stores a name of at most 128 characters
// (account_api_key.name varchar(128); model.MaxApiKeyNameLength) and refuses a
// longer one, or one with a NUL in it.
inline constexpr size_t kMaxNameCharacters = 128;

// UTF-8 code points, the server's characters (an ill-formed byte counts as one).
inline size_t CharacterCount(const std::string& text) {
  size_t count = 0;
  for (const char c : text) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
  }
  return count;
}

inline bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// What the user typed, without white space at either end.
inline std::string Trimmed(const std::string& text) {
  size_t first = 0;
  size_t last = text.size();
  while (first < last && IsSpace(text[first])) ++first;
  while (last > first && IsSpace(text[last - 1])) --last;
  return text.substr(first, last - first);
}

// The name a create sends for what the user typed: trimmed; nullopt when
// nothing is left, it is longer than the server stores, or it holds a NUL. The
// app always names a key (the server would take an empty name, and a key
// created through the API may have none).
inline std::optional<std::string> NameToCreate(const std::string& typed) {
  std::string name = Trimmed(typed);
  if (name.empty() || CharacterCount(name) > kMaxNameCharacters ||
      name.find('\0') != std::string::npos) {
    return std::nullopt;
  }
  return name;
}

// The name a key is shown by: its name filtered for display, or "Unnamed key"
// when nothing of it would show.
inline std::string DisplayName(const Key& key, const Words& words) {
  const std::string name = Trimmed(SanitizeExternalDisplayText(key.name));
  return name.empty() ? words.lookup(kUnnamedText) : name;
}

// ---- a row --------------------------------------------------------------------------

struct Row {
  std::string id;           // the key's id, which its row and its delete are named by
  std::string title;        // line 1: the name, or "Unnamed key"
  std::string created;      // line 2: "Created 10/29/26"; "" when the time did not read
  std::string createdFull;  // line 2's full date and time; ""
  std::string idLine;       // line 3: "ID 019a2b3c-…"
  std::string deleteName;   // "Delete. home server": the row's action, by name
};

inline Row RowFor(const Key& key, const Words& words) {
  Row row;
  row.id = key.id;
  row.title = DisplayName(key, words);
  if (key.createUnixSeconds > 0) {
    row.created = words.format(words.lookup(kCreatedAtText), {words.date(key.createUnixSeconds)});
    row.createdFull = words.dateTime(key.createUnixSeconds);
  }
  row.idLine = words.format(words.lookup(kIdText), {SanitizeExternalDisplayText(key.id)});
  // the verb, then the row's name: GTK has no separate description channel
  // for a button (AccountPage's AddButtonRow)
  row.deleteName = words.lookup(kDeleteText) + ". " + row.title;
  return row;
}

// ---- a delete's state ------------------------------------------------------------------

enum class ActionView {
  Idle,
  Busy,    // the request is out: progress, the Delete off
  Failed,  // the failure under the row; the Delete on again for another try
};

inline bool Has(const std::vector<std::string>& ids, const std::string& id) {
  return !id.empty() && std::find(ids.begin(), ids.end(), id) != ids.end();
}

inline ActionView DeleteViewOf(const Snapshot& snapshot, const std::string& id) {
  if (Has(snapshot.deletingIds, id)) return ActionView::Busy;
  if (Has(snapshot.deleteFailedIds, id)) return ActionView::Failed;
  return ActionView::Idle;
}

// The line under a row's Delete: its failure; nothing while idle or busy (the
// row's progress says busy).
inline std::optional<Text> DeleteStatusText(ActionView view) {
  if (view == ActionView::Failed) return kDeleteFailedText;
  return std::nullopt;
}

// ---- the screen -----------------------------------------------------------------------

enum class Body {
  NoSession,   // signed out: nothing to list and nothing asked
  Progress,    // never loaded
  Rows,        // the list
  Empty,       // loaded, and there is no key
  LoadFailed,  // the first load failed: Try again
};

struct Screen {
  Body body = Body::Progress;
  bool refreshing = false;     // the header's progress over the list shown
  bool refreshFailed = false;  // the last list kept, under "Couldn't load API keys."
  bool canCreate = false;      // Create API key is on
};

inline Screen ScreenFor(const Snapshot& snapshot, bool signedIn) {
  Screen screen;
  if (!signedIn) {
    screen.body = Body::NoSession;
    return screen;
  }
  // a key can be made whether or not the list has loaded; one create at a time
  screen.canCreate = !snapshot.creating;
  if (!snapshot.loaded) {
    // a load in flight shows progress, even over the last attempt's failure
    screen.body = !snapshot.loading && snapshot.loadFailed ? Body::LoadFailed : Body::Progress;
    return screen;
  }
  screen.body = snapshot.keys.empty() ? Body::Empty : Body::Rows;
  screen.refreshing = snapshot.loading;
  screen.refreshFailed = snapshot.loadFailed && !snapshot.loading;
  return screen;
}

// The line a body without rows shows; nothing for the rows.
inline std::optional<Text> BodyText(Body body) {
  switch (body) {
    case Body::NoSession:
      return Text{"please_login_to_urnetwork", "Please login to URnetwork"};
    case Body::Progress:
      return kLoadingText;
    case Body::Rows:
      return std::nullopt;
    case Body::Empty:
      return kEmptyText;
    case Body::LoadFailed:
      return kLoadFailedText;
  }
  return std::nullopt;
}

// ---- the delete confirmation ------------------------------------------------------------

// Every delete is confirmed first, naming the key; Cancel is the default.
struct DeleteConfirmation {
  std::string keyId;
  Text title;
  Text body;
  std::vector<std::string> bodyArgs;  // {the key's name, or "Unnamed key"}
};

inline DeleteConfirmation ConfirmationFor(const Key& key, const Words& words) {
  DeleteConfirmation confirmation;
  confirmation.keyId = key.id;
  confirmation.title = kDeleteConfirmTitleText;
  confirmation.body = kDeleteConfirmBodyText;
  confirmation.bodyArgs = {DisplayName(key, words)};
  return confirmation;
}

inline std::string ConfirmationBody(const DeleteConfirmation& confirmation, const Words& words) {
  return words.format(words.lookup(confirmation.body), confirmation.bodyArgs);
}

// The delete gestures between a press and the request: a press opens one
// confirmation (never a second, never for a key already being deleted or no
// longer listed), Cancel drops it, and only the confirmation's Delete hands
// back the key to delete, once.
class DeleteFlow {
 public:
  std::optional<DeleteConfirmation> Press(const Snapshot& snapshot, const std::string& keyId,
                                          const Words& words) {
    if (open_ || keyId.empty()) return std::nullopt;
    const auto key = std::find_if(snapshot.keys.begin(), snapshot.keys.end(),
                                  [&](const Key& k) { return k.id == keyId; });
    if (key == snapshot.keys.end()) return std::nullopt;
    if (DeleteViewOf(snapshot, keyId) == ActionView::Busy) return std::nullopt;
    open_ = keyId;
    return ConfirmationFor(*key, words);
  }

  // Cancel, Escape or the window closed: nothing is sent.
  void Cancel() { open_.reset(); }

  // The confirmation's Delete: the key's id, once.
  std::optional<std::string> Confirm() {
    std::optional<std::string> keyId = std::move(open_);
    open_.reset();
    return keyId;
  }

  bool Open() const { return open_.has_value(); }

 private:
  std::optional<std::string> open_;
};

// ---- the create sheet ------------------------------------------------------------------

// Overwrites a secret's characters, then lets it go, so a key does not stay
// readable in memory the app has finished with.
inline void Wipe(std::string& secret) {
  volatile char* bytes = secret.data();
  for (size_t i = 0; i < secret.size(); ++i) bytes[i] = '\0';
  secret.clear();
  secret.shrink_to_fit();
}

// The sheet's steps: the name, the request out, the key shown once.
enum class CreateStep { Name, Creating, Created };

// The create sheet's state, from the press that opens it to its close. The
// key the server returns is held here while the sheet shows it, and wiped as
// the sheet closes, however it closes; the sheet stays open while the request
// is out, so the key always has somewhere to show.
class CreateFlow {
 public:
  ~CreateFlow() { Wipe(key_); }

  // The sheet opened: the name step, nothing held, no failure shown.
  void Open() {
    Wipe(key_);
    open_ = true;
    step_ = CreateStep::Name;
    failed_ = false;
    copied_ = false;
  }

  bool IsOpen() const { return open_; }
  CreateStep Step() const { return step_; }
  // The last create failed: the name step says so under the name.
  bool Failed() const { return failed_; }

  // Whether Create sends `typed` now.
  bool CanSubmit(const std::string& typed) const {
    return open_ && step_ == CreateStep::Name && NameToCreate(typed).has_value();
  }

  // Create pressed: the name to send, once; nothing while a create is out, or
  // for a name that cannot be sent.
  std::optional<std::string> Submit(const std::string& typed) {
    if (!CanSubmit(typed)) return std::nullopt;
    step_ = CreateStep::Creating;
    failed_ = false;
    return NameToCreate(typed);
  }

  // The answer to the create: the key, shown once, or the name step again with
  // the failure. An answer for a sheet that is not waiting is wiped unread.
  void Settle(bool ok, std::string key) {
    if (!open_ || step_ != CreateStep::Creating) {
      Wipe(key);
      return;
    }
    if (ok && !key.empty()) {
      key_ = std::move(key);
      step_ = CreateStep::Created;
      copied_ = false;
      return;
    }
    Wipe(key);
    step_ = CreateStep::Name;
    failed_ = true;
  }

  // The key while the sheet shows it; "" at any other step.
  const std::string& ShownKey() const { return key_; }

  // Copy key pressed: the key to put on the clipboard, and the button reads
  // Copied for a moment; nothing at any other step.
  std::optional<std::string> Copy() {
    if (step_ != CreateStep::Created || key_.empty()) return std::nullopt;
    copied_ = true;
    return key_;
  }
  // The moment is over: the button reads Copy key again.
  void EndCopied() { copied_ = false; }
  const Text& CopyLabel() const { return copied_ ? kCopiedText : kCopyText; }

  // The sheet may close: not while the create is out.
  bool CanClose() const { return step_ != CreateStep::Creating; }

  // The sheet closed (Done, Escape, the window manager, a sign-out): the key
  // is wiped.
  void Close() {
    Wipe(key_);
    open_ = false;
    step_ = CreateStep::Name;
    failed_ = false;
    copied_ = false;
  }

 private:
  std::string key_;
  bool open_ = false;
  CreateStep step_ = CreateStep::Name;
  bool failed_ = false;
  bool copied_ = false;
};

}  // namespace urnw::api_keys
