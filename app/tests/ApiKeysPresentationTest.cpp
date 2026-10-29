// Account -> API keys decided from plain data (ApiKeysPresentation.hpp): the
// server's create time as Unix seconds, the list's order, what a key's row
// says (its name or "Unnamed key", filtered for display; when it was made; its
// ID), the name a create sends, the screen for every state of the list, the
// delete's confirmation and the gestures before a delete is sent, and the
// create sheet's steps, which hold the new key only while the sheet shows it.
// The words are the English sources (the catalog with no translation loaded),
// pinned against po/en.po at the end; the dates are fixed strings, so nothing
// here depends on time or locale.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ApiKeysPresentation.hpp"

namespace k = urnw::api_keys;

#define UR_EXPECT_TEXT(expected, actual)                                                    \
  do {                                                                                      \
    const std::string ur_e_ = (expected);                                                   \
    const std::string ur_a_ = (actual);                                                     \
    if (ur_e_ != ur_a_)                                                                     \
      UR_FAIL(std::string(#actual) + ": expected \"" + ur_e_ + "\", got \"" + ur_a_ + "\""); \
  } while (0)

namespace {

// 2026-10-29T15:04:05Z, UTC Unix seconds (Python's datetime agrees)
constexpr int64_t kCreated = 1793286245;

// The store's {} substitution (I18n.hpp FormatArgs): "{}" takes the next
// argument, "{0}" "{1}" a given one.
std::string FormatLike(const std::string& pattern, const std::vector<std::string>& args) {
  std::string out;
  size_t next = 0;
  for (size_t i = 0; i < pattern.size();) {
    if (pattern[i] == '{') {
      const size_t close = pattern.find('}', i);
      if (close != std::string::npos) {
        const std::string spec = pattern.substr(i + 1, close - i - 1);
        size_t index = next;
        if (!spec.empty()) index = static_cast<size_t>(std::stoul(spec));
        if (index < args.size()) {
          out += args[index];
          if (spec.empty()) ++next;
          i = close + 1;
          continue;
        }
      }
    }
    out += pattern[i++];
  }
  return out;
}

k::Words EnglishWords() {
  k::Words words;
  words.lookup = [](const k::Text& text) { return std::string(text.english); };
  words.format = &FormatLike;
  words.date = [](int64_t t) { return "D" + std::to_string(t); };
  words.dateTime = [](int64_t t) { return "DT" + std::to_string(t); };
  return words;
}

k::Key KeyOf(const std::string& id, const std::string& name, int64_t createUnixSeconds) {
  k::Key key;
  key.id = id;
  key.name = name;
  key.createUnixSeconds = createUnixSeconds;
  return key;
}

const std::string kIdA = "019a2b3c-0000-7000-8000-00000000000a";
const std::string kIdB = "019a2b3c-0000-7000-8000-00000000000b";

// A loaded list of two keys.
k::Snapshot TwoKeys() {
  k::Snapshot snapshot;
  snapshot.loaded = true;
  snapshot.keys = {KeyOf(kIdB, "home server", kCreated), KeyOf(kIdA, "", kCreated - 86400)};
  return snapshot;
}

}  // namespace

// ---- the sdk's fields ---------------------------------------------------------------

UR_TEST(ApiKeys_TheServersCreateTimeReadsAsUnixSeconds) {
  const std::pair<const char*, int64_t> cases[] = {
      {"2026-10-29T15:04:05Z", kCreated},
      // Go writes the nanoseconds; they are dropped
      {"2026-10-29T15:04:05.123456789Z", kCreated},
      {"2026-10-29T15:04:05.5Z", kCreated},
      // an offset is the same instant
      {"2026-10-29T10:04:05-05:00", kCreated},
      {"2026-10-29T17:34:05+02:30", kCreated},
      {"2026-10-29t15:04:05z", kCreated},
      {"1970-01-01T00:00:00Z", 0},
      {"1969-12-31T23:59:59Z", -1},
      {"2024-02-29T12:00:00Z", 1709208000},
      {"2000-02-29T00:00:00Z", 951782400},
      // a leap second reads as the second before it
      {"2016-12-31T23:59:60Z", 1483228799},
  };
  for (const auto& [text, expected] : cases) {
    const std::optional<int64_t> parsed = k::ParseRfc3339UnixSeconds(text);
    UR_EXPECT_TRUE_MSG(text, parsed.has_value());
    if (parsed) UR_EXPECT_TRUE_MSG(text, *parsed == expected);
  }
}

UR_TEST(ApiKeys_ATimeThatDoesNotReadIsNone) {
  for (const char* text : {"", "2026-10-29", "2026-10-29T15:04:05", "2026-10-29 15:04:05Z",
                           "2026-10-29T15:04:05.Z", "2026-10-29T15:04:05+0500",
                           "2026-10-29T15:04:05+05", "2026-10-29T15:04:05Zjunk",
                           "2026-13-01T00:00:00Z", "2026-00-10T00:00:00Z",
                           "2026-02-29T00:00:00Z", "2100-02-29T00:00:00Z",
                           "2026-10-32T00:00:00Z", "2026-10-29T24:00:00Z",
                           "2026-10-29T15:60:00Z", "2026-10-29T15:04:61Z",
                           "2026-10-29T15:04:05+24:00", "26-10-29T15:04:05Z",
                           "2026/10/29T15:04:05Z", "0001-01-01T00:00:0xZ"}) {
    UR_EXPECT_TRUE_MSG(text, !k::ParseRfc3339UnixSeconds(text).has_value());
  }
}

// The server lists the keys in no order: newest first, ties by id (a
// time-ordered ULID), and a key whose time did not read after the rest.
UR_TEST(ApiKeys_TheKeysAreNewestFirst) {
  std::vector<k::Key> keys = {
      KeyOf("019a0000-0000-7000-8000-000000000001", "old", kCreated - 600),
      KeyOf("019a0000-0000-7000-8000-000000000002", "unknown time", 0),
      KeyOf("019a0000-0000-7000-8000-000000000003", "new", kCreated),
      KeyOf("019a0000-0000-7000-8000-000000000004", "same second, minted later", kCreated),
  };
  k::SortKeys(keys);
  UR_EXPECT_TEXT("same second, minted later", keys[0].name);
  UR_EXPECT_TEXT("new", keys[1].name);
  UR_EXPECT_TEXT("old", keys[2].name);
  UR_EXPECT_TEXT("unknown time", keys[3].name);
}

// ---- a row --------------------------------------------------------------------------

UR_TEST(ApiKeys_ARowSaysTheNameWhenItWasMadeAndItsId) {
  const k::Row row = k::RowFor(KeyOf(kIdB, "home server", kCreated), EnglishWords());
  UR_EXPECT_TEXT(kIdB, row.id);
  UR_EXPECT_TEXT("home server", row.title);
  UR_EXPECT_TEXT("Created D" + std::to_string(kCreated), row.created);
  UR_EXPECT_TEXT("DT" + std::to_string(kCreated), row.createdFull);
  // the whole ID: it is what the API takes
  UR_EXPECT_TEXT("ID " + kIdB, row.idLine);
  UR_EXPECT_TEXT("Delete. home server", row.deleteName);
}

UR_TEST(ApiKeys_AnUnknownTimeLeavesTheDateOut) {
  const k::Row row = k::RowFor(KeyOf(kIdB, "home server", 0), EnglishWords());
  UR_EXPECT_TEXT("", row.created);
  UR_EXPECT_TEXT("", row.createdFull);
  UR_EXPECT_TEXT("ID " + kIdB, row.idLine);
}

// A key made through the API may have no name: it reads "Unnamed key", in its
// row and in its confirmation.
UR_TEST(ApiKeys_AKeyWithoutANameIsUnnamed) {
  const k::Words words = EnglishWords();
  for (const char* name : {"", "   ", "\t\n", "\u200B", "\u202E\u200E"}) {
    const k::Key key = KeyOf(kIdA, name, kCreated);
    UR_EXPECT_TEXT("Unnamed key", k::RowFor(key, words).title);
    UR_EXPECT_TEXT("Delete. Unnamed key", k::RowFor(key, words).deleteName);
    UR_EXPECT_TEXT("\"Unnamed key\" will stop working right away, and anything using it will "
                   "lose access.",
                   k::ConfirmationBody(k::ConfirmationFor(key, words), words));
  }
}

// A name comes from outside the app (any API client names a key): it is
// filtered like every external name, kept whole however long, and trimmed.
UR_TEST(ApiKeys_ANameIsFilteredForDisplay) {
  const k::Words words = EnglishWords();
  UR_EXPECT_TEXT("home server", k::RowFor(KeyOf(kIdA, "home \u202Eserver", 0), words).title);
  UR_EXPECT_TEXT("home server", k::RowFor(KeyOf(kIdA, "  home server  ", 0), words).title);
  // one combining mark per base character
  UR_EXPECT_TEXT("e\u0301", k::RowFor(KeyOf(kIdA, "e\u0301\u0301\u0301\u0301", 0), words).title);
  const std::string longName(128, 'x');
  UR_EXPECT_TEXT(longName, k::RowFor(KeyOf(kIdA, longName, 0), words).title);
  // markup is text: the page sets it as plain text
  UR_EXPECT_TEXT("<b>x</b>", k::RowFor(KeyOf(kIdA, "<b>x</b>", 0), words).title);
}

// ---- the name a create sends ----------------------------------------------------------

UR_TEST(ApiKeys_TheNameACreateSends) {
  UR_EXPECT_TEXT("home server", *k::NameToCreate("home server"));
  UR_EXPECT_TEXT("home server", *k::NameToCreate("  home server\t\n"));
  // the app always names a key
  for (const char* empty : {"", " ", "\t \n"}) {
    UR_EXPECT_TRUE_MSG(std::string("\"") + empty + "\"", !k::NameToCreate(empty).has_value());
  }
  // the server stores 128 characters, not bytes
  UR_EXPECT_TRUE(k::NameToCreate(std::string(128, 'x')).has_value());
  UR_EXPECT_FALSE(k::NameToCreate(std::string(129, 'x')).has_value());
  std::string accents;
  for (int i = 0; i < 128; ++i) accents += "\u00E9";  // 2 bytes each
  UR_EXPECT_TRUE(k::NameToCreate(accents).has_value());
  UR_EXPECT_FALSE(k::NameToCreate(accents + "\u00E9").has_value());
  // ...and refuses a NUL
  UR_EXPECT_FALSE(k::NameToCreate(std::string("home\0server", 11)).has_value());
  UR_EXPECT_EQ(128u, k::kMaxNameCharacters);
}

// ---- the screen ------------------------------------------------------------------------

UR_TEST(ApiKeys_SignedOutListsNothingAndAsksNothing) {
  const k::Screen screen = k::ScreenFor(TwoKeys(), /*signedIn=*/false);
  UR_EXPECT_TRUE(screen.body == k::Body::NoSession);
  UR_EXPECT_FALSE(screen.canCreate);
  UR_EXPECT_TEXT("Please login to URnetwork", k::BodyText(screen.body)->english);
}

UR_TEST(ApiKeys_NeverLoadedShowsProgress) {
  k::Snapshot snapshot;
  UR_EXPECT_TRUE(k::ScreenFor(snapshot, true).body == k::Body::Progress);
  snapshot.loading = true;
  UR_EXPECT_TRUE(k::ScreenFor(snapshot, true).body == k::Body::Progress);
  UR_EXPECT_TEXT("Loading...", k::BodyText(k::Body::Progress)->english);
  // a key can be made before the list has loaded
  UR_EXPECT_TRUE(k::ScreenFor(snapshot, true).canCreate);
}

UR_TEST(ApiKeys_AFailedFirstLoadOffersRetry) {
  k::Snapshot snapshot;
  snapshot.loadFailed = true;
  k::Screen screen = k::ScreenFor(snapshot, true);
  UR_EXPECT_TRUE(screen.body == k::Body::LoadFailed);
  UR_EXPECT_TEXT("Couldn't load your API keys. Try again.", k::BodyText(screen.body)->english);
  UR_EXPECT_TEXT("Retry", k::kRetryText.english);
  // the retry in flight shows progress over the last failure
  snapshot.loading = true;
  UR_EXPECT_TRUE(k::ScreenFor(snapshot, true).body == k::Body::Progress);
}

UR_TEST(ApiKeys_LoadedEmptyIsDistinctFromNeverLoaded) {
  k::Snapshot snapshot;
  snapshot.loaded = true;
  const k::Screen screen = k::ScreenFor(snapshot, true);
  UR_EXPECT_TRUE(screen.body == k::Body::Empty);
  UR_EXPECT_TEXT("You don't have any API keys.", k::BodyText(screen.body)->english);
  UR_EXPECT_TRUE(screen.canCreate);
}

UR_TEST(ApiKeys_ARefreshKeepsTheRowsAndAFailedOneSaysSo) {
  k::Snapshot snapshot = TwoKeys();
  k::Screen screen = k::ScreenFor(snapshot, true);
  UR_EXPECT_TRUE(screen.body == k::Body::Rows);
  UR_EXPECT_FALSE(k::BodyText(screen.body).has_value());
  UR_EXPECT_FALSE(screen.refreshing);
  snapshot.loading = true;
  screen = k::ScreenFor(snapshot, true);
  UR_EXPECT_TRUE(screen.body == k::Body::Rows && screen.refreshing && !screen.refreshFailed);
  snapshot.loading = false;
  snapshot.loadFailed = true;
  screen = k::ScreenFor(snapshot, true);
  UR_EXPECT_TRUE(screen.body == k::Body::Rows && !screen.refreshing && screen.refreshFailed);
  // the notice goes while the next refresh runs
  snapshot.loading = true;
  UR_EXPECT_FALSE(k::ScreenFor(snapshot, true).refreshFailed);
}

UR_TEST(ApiKeys_OneCreateAtATime) {
  k::Snapshot snapshot = TwoKeys();
  UR_EXPECT_TRUE(k::ScreenFor(snapshot, true).canCreate);
  snapshot.creating = true;
  UR_EXPECT_FALSE(k::ScreenFor(snapshot, true).canCreate);
}

// ---- deletes ------------------------------------------------------------------------------

UR_TEST(ApiKeys_ADeleteIsBusyThenGoneOrFailed) {
  k::Snapshot snapshot = TwoKeys();
  UR_EXPECT_TRUE(k::DeleteViewOf(snapshot, kIdA) == k::ActionView::Idle);
  snapshot.deletingIds = {kIdA};
  UR_EXPECT_TRUE(k::DeleteViewOf(snapshot, kIdA) == k::ActionView::Busy);
  UR_EXPECT_TRUE(k::DeleteViewOf(snapshot, kIdB) == k::ActionView::Idle);
  // a retry in flight is busy, whatever the last attempt said
  snapshot.deleteFailedIds = {kIdA};
  UR_EXPECT_TRUE(k::DeleteViewOf(snapshot, kIdA) == k::ActionView::Busy);
  snapshot.deletingIds.clear();
  UR_EXPECT_TRUE(k::DeleteViewOf(snapshot, kIdA) == k::ActionView::Failed);
  UR_EXPECT_TRUE(k::DeleteViewOf(snapshot, "") == k::ActionView::Idle);
  UR_EXPECT_TEXT("Couldn't delete the API key. Try again.",
                 k::DeleteStatusText(k::ActionView::Failed)->english);
  UR_EXPECT_FALSE(k::DeleteStatusText(k::ActionView::Busy).has_value());
  UR_EXPECT_FALSE(k::DeleteStatusText(k::ActionView::Idle).has_value());
}

UR_TEST(ApiKeys_TheConfirmationNamesTheKey) {
  const k::Snapshot snapshot = TwoKeys();
  const k::Words words = EnglishWords();
  const k::DeleteConfirmation confirmation = k::ConfirmationFor(snapshot.keys[0], words);
  UR_EXPECT_TEXT(kIdB, confirmation.keyId);
  UR_EXPECT_TEXT("Delete this API key?", confirmation.title.english);
  UR_EXPECT_TEXT("\"home server\" will stop working right away, and anything using it will lose "
                 "access.",
                 k::ConfirmationBody(confirmation, words));
  // the name as it shows: filtered
  UR_EXPECT_TEXT("\"home server\" will stop working right away, and anything using it will lose "
                 "access.",
                 k::ConfirmationBody(k::ConfirmationFor(KeyOf(kIdB, "home\u200B server", 0), words),
                                     words));
}

// The delete gestures: a press opens one confirmation, Cancel sends nothing,
// the confirmation's Delete sends once.
UR_TEST(ApiKeys_APressConfirmsBeforeAnythingIsSent) {
  const k::Snapshot snapshot = TwoKeys();
  const k::Words words = EnglishWords();
  k::DeleteFlow flow;
  std::optional<k::DeleteConfirmation> confirmation = flow.Press(snapshot, kIdB, words);
  UR_EXPECT_TRUE(confirmation.has_value() && flow.Open());
  flow.Cancel();
  UR_EXPECT_FALSE(flow.Open());
  UR_EXPECT_FALSE(flow.Confirm().has_value());
  confirmation = flow.Press(snapshot, kIdB, words);
  UR_EXPECT_TRUE(confirmation.has_value());
  const std::optional<std::string> sent = flow.Confirm();
  UR_EXPECT_TRUE(sent.has_value() && *sent == kIdB);
  UR_EXPECT_FALSE(flow.Confirm().has_value());  // a second press of Delete sends nothing
}

UR_TEST(ApiKeys_DuplicateDeletesAreSuppressed) {
  k::Snapshot snapshot = TwoKeys();
  const k::Words words = EnglishWords();
  k::DeleteFlow flow;
  // one confirmation at a time, on any row
  UR_EXPECT_TRUE(flow.Press(snapshot, kIdB, words).has_value());
  UR_EXPECT_FALSE(flow.Press(snapshot, kIdB, words).has_value());
  UR_EXPECT_FALSE(flow.Press(snapshot, kIdA, words).has_value());
  flow.Cancel();
  // a key already being deleted opens nothing
  snapshot.deletingIds = {kIdB};
  UR_EXPECT_FALSE(flow.Press(snapshot, kIdB, words).has_value());
  UR_EXPECT_FALSE(flow.Open());
  // ...but a failed one can be tried again
  snapshot.deletingIds.clear();
  snapshot.deleteFailedIds = {kIdB};
  UR_EXPECT_TRUE(flow.Press(snapshot, kIdB, words).has_value());
  flow.Cancel();
  // a key no longer listed (deleted elsewhere) opens nothing
  UR_EXPECT_FALSE(flow.Press(snapshot, "gone", words).has_value());
  UR_EXPECT_FALSE(flow.Press(snapshot, "", words).has_value());
}

// ---- the create sheet ------------------------------------------------------------------

UR_TEST(ApiKeys_TheCreateSheetSendsTheNameOnce) {
  k::CreateFlow flow;
  UR_EXPECT_FALSE(flow.IsOpen());
  UR_EXPECT_FALSE(flow.CanSubmit("home server"));  // not open
  flow.Open();
  UR_EXPECT_TRUE(flow.Step() == k::CreateStep::Name);
  UR_EXPECT_FALSE(flow.CanSubmit(""));
  UR_EXPECT_FALSE(flow.CanSubmit("   "));
  UR_EXPECT_FALSE(flow.Submit("").has_value());
  UR_EXPECT_TRUE(flow.CanSubmit(" home server "));
  const std::optional<std::string> name = flow.Submit(" home server ");
  UR_EXPECT_TRUE(name.has_value() && *name == "home server");
  UR_EXPECT_TRUE(flow.Step() == k::CreateStep::Creating);
  // nothing more while the request is out, and the sheet stays
  UR_EXPECT_FALSE(flow.Submit("home server").has_value());
  UR_EXPECT_FALSE(flow.CanClose());
  UR_EXPECT_TEXT("", flow.ShownKey());
}

UR_TEST(ApiKeys_TheKeyIsShownOnceAndWipedWhenTheSheetCloses) {
  k::CreateFlow flow;
  flow.Open();
  flow.Submit("home server");
  flow.Settle(true, "urn_k3y-secret-0001");
  UR_EXPECT_TRUE(flow.Step() == k::CreateStep::Created);
  UR_EXPECT_TEXT("urn_k3y-secret-0001", flow.ShownKey());
  UR_EXPECT_TRUE(flow.CanClose());
  // Copy key: the whole key, and Copied in its place for a moment
  UR_EXPECT_TEXT("Copy key", flow.CopyLabel().english);
  const std::optional<std::string> copied = flow.Copy();
  UR_EXPECT_TRUE(copied.has_value() && *copied == "urn_k3y-secret-0001");
  UR_EXPECT_TEXT("Copied", flow.CopyLabel().english);
  flow.EndCopied();
  UR_EXPECT_TEXT("Copy key", flow.CopyLabel().english);
  // Done, Escape or the window manager: the key is gone
  flow.Close();
  UR_EXPECT_FALSE(flow.IsOpen());
  UR_EXPECT_TEXT("", flow.ShownKey());
  UR_EXPECT_FALSE(flow.Copy().has_value());
  // and a sheet opened again starts with nothing
  flow.Open();
  UR_EXPECT_TEXT("", flow.ShownKey());
  UR_EXPECT_TRUE(flow.Step() == k::CreateStep::Name);
  UR_EXPECT_TEXT("Copy key", flow.CopyLabel().english);
}

UR_TEST(ApiKeys_AFailedCreateReturnsToTheName) {
  k::CreateFlow flow;
  flow.Open();
  flow.Submit("home server");
  flow.Settle(false, std::string());
  UR_EXPECT_TRUE(flow.Step() == k::CreateStep::Name);
  UR_EXPECT_TRUE(flow.Failed());
  UR_EXPECT_TEXT("", flow.ShownKey());
  UR_EXPECT_TEXT("Couldn't create the API key. Try again.", k::kCreateFailedText.english);
  // a success without a key is a failure too
  UR_EXPECT_TRUE(flow.Submit("home server").has_value());
  UR_EXPECT_FALSE(flow.Failed());  // the failure goes while the retry runs
  flow.Settle(true, std::string());
  UR_EXPECT_TRUE(flow.Step() == k::CreateStep::Name && flow.Failed());
  // a reopened sheet shows no old failure
  flow.Close();
  flow.Open();
  UR_EXPECT_FALSE(flow.Failed());
}

// An answer that comes when the sheet is not waiting (closed by a sign-out, or
// another answer already settled it) is wiped unread.
UR_TEST(ApiKeys_AnAnswerTheSheetDoesNotWaitForIsNotKept) {
  k::CreateFlow flow;
  flow.Settle(true, "urn_k3y-secret-0002");  // never opened
  UR_EXPECT_TEXT("", flow.ShownKey());
  flow.Open();
  flow.Submit("home server");
  flow.Close();  // a sign-out closes it with the request out
  flow.Settle(true, "urn_k3y-secret-0003");
  UR_EXPECT_TEXT("", flow.ShownKey());
  UR_EXPECT_FALSE(flow.IsOpen());
  flow.Open();
  flow.Settle(true, "urn_k3y-secret-0004");  // open, but nothing submitted
  UR_EXPECT_TEXT("", flow.ShownKey());
  UR_EXPECT_TRUE(flow.Step() == k::CreateStep::Name);
}

UR_TEST(ApiKeys_WipeLeavesNothing) {
  std::string secret = "urn_k3y-secret-0005";
  k::Wipe(secret);
  UR_EXPECT_TRUE(secret.empty());
  std::string empty;
  k::Wipe(empty);
  UR_EXPECT_TRUE(empty.empty());
}

UR_TEST(ApiKeys_TheIntroLinksTheApiDocs) {
  UR_EXPECT_TEXT("https://ur.io/docs/api", k::kDocsUrl);
  UR_EXPECT_TEXT("How to use API keys", k::kDocsLinkText.english);
}

// ---- every word is the catalog's ----------------------------------------------------

UR_TEST(ApiKeys_EveryKeyAndEnglishIsTheCatalogs) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/../po/en.po", std::ios::binary);
  UR_EXPECT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string catalog = buffer.str();
  std::vector<k::Text> texts = {
      k::kTitleText,           k::kIntroText,         k::kDocsLinkText,
      k::kEmptyText,           k::kCreateText,        k::kNameLabelText,
      k::kNamePlaceholderText, k::kCreatedTitleText,  k::kCreatedWarningText,
      k::kCopyText,            k::kCopiedText,        k::kCreatedAtText,
      k::kIdText,              k::kUnnamedText,       k::kDeleteText,
      k::kDeleteConfirmTitleText, k::kDeleteConfirmBodyText, k::kLoadFailedText,
      k::kCreateFailedText,    k::kDeleteFailedText,  k::kCancelText,
      k::kDoneText,            k::kRetryText,         k::kRefreshText,
      k::kLoadingText,         k::kSomethingWentWrongText, k::kAccountText,
  };
  for (k::Body body : {k::Body::NoSession, k::Body::Progress, k::Body::Empty, k::Body::LoadFailed}) {
    texts.push_back(*k::BodyText(body));
  }
  texts.push_back(*k::DeleteStatusText(k::ActionView::Failed));
  // the po escapes a quote in the English
  const auto poEscape = [](const std::string& english) {
    std::string out;
    for (const char c : english) {
      if (c == '"' || c == '\\') out += '\\';
      out += c;
    }
    return out;
  };
  for (const k::Text& text : texts) {
    const std::string entry = std::string("msgctxt \"") + text.key + "\"\nmsgid \"" +
                              poEscape(text.english) + "\"\n";
    UR_EXPECT_TRUE_MSG(entry, catalog.find(entry) != std::string::npos);
  }
}
