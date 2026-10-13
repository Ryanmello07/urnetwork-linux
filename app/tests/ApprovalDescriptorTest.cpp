// Executable spec for the page's descriptor (src/ApprovalDescriptor.hpp): when a
// response from the approval page makes the app offer the browser approval, and
// every way it does not. It is the Windows app's tools/approval-descriptor-tests.cpp,
// case for case, for this app's row of the page's platform table (linux where it
// reads windows, and windows as the other platform), with the descriptor the page
// serves once it lists linux, and the one it serves today, which lists windows
// only and offers this app nothing.
//
// SPDX-License-Identifier: MPL-2.0
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ApprovalDescriptor.hpp"
#include "TestHarness.hpp"
#include "WiringSource.hpp"

// The Windows spec's Check, as this harness records it.
#define Check(cond, what) UR_EXPECT_TRUE_MSG((what), (cond))

using namespace urnw::approval;

namespace {

constexpr std::string_view kJson = "application/json";
// the page's descriptor once it lists this app (both versions, both platforms), and what an older
// page answers, which lists the official network's only
constexpr std::string_view kPageBody = R"({"contracts":[1,2],"platforms":["windows","linux"]})";
constexpr std::string_view kBody = R"({"contracts":[1],"platforms":["linux"]})";

DescriptorVerdict Verdict(std::string_view contentType, std::string_view body) {
  return CheckDescriptor(contentType, body);
}

void Expect(DescriptorVerdict want, std::string_view body, const std::string& what,
            std::string_view contentType = kJson) {
  const DescriptorVerdict got = Verdict(contentType, body);
  Check(got == want, what + " (got " + std::string(ToString(got)) + ")");
}

UR_TEST(ApprovalDescriptor_TheDescriptorTheContractNamesIsOffered) {
  Expect(DescriptorVerdict::Offered, kPageBody, "the contract's own descriptor");
  Expect(DescriptorVerdict::Offered, kBody,
         "an older page's, which lists the official network's version only");
  Check(kContract == 1 && kContractNamed == 2 && kPlatform == "linux",
        "constants: contracts 1 and 2, the linux row");
  Check(kCheckBudget == std::chrono::milliseconds(3000), "constants: three seconds");
  Check(kMaxDescriptorBytes == 4096,
        "constants: the cap on the body is 4096 bytes (the cases below are written against the "
        "constant)");
  // a descriptor that lists more
  Expect(DescriptorVerdict::Offered, R"({"contracts":[1,2],"platforms":["linux","windows"]})",
         "more contracts and platforms");
  Expect(DescriptorVerdict::Offered, R"({"contracts":[2,1],"platforms":["windows","linux"]})",
         "this one not first");
  Expect(DescriptorVerdict::Offered,
         R"({"contracts":[1],"platforms":["linux"],"extra":{"a":[1,2,3]},"note":null})",
         "keys it does not know are ignored");
  Expect(DescriptorVerdict::Offered, R"({"platforms":["linux"],"contracts":[1]})",
         "either order of keys");
  Expect(DescriptorVerdict::Offered,
         "  \n{ \"contracts\" : [ 1 ] , \"platforms\" : [ \"linux\" ] }\n",
         "whitespace around the tokens");
  Expect(DescriptorVerdict::Offered, R"({"contracts":[1],"platforms":["linux"]})",
         "charset parameter", "application/json; charset=utf-8");
  Expect(DescriptorVerdict::Offered, kBody, "upper case", "Application/JSON");
  Expect(DescriptorVerdict::Offered, kBody, "a space before the parameters",
         "application/json ;charset=utf-8");
  Expect(DescriptorVerdict::Offered, kBody, "leading blanks", "  application/json");
  Expect(DescriptorVerdict::Offered, kBody, "one leading blank", " application/json");
  Expect(DescriptorVerdict::Offered, kBody, "a leading tab", "\tapplication/json");
  Expect(DescriptorVerdict::Offered, "\xEF\xBB\xBF{\"contracts\":[1],\"platforms\":[\"linux\"]}",
         "a byte order mark");
}

UR_TEST(ApprovalDescriptor_AnythingElseIsNo) {
  // the content type
  Expect(DescriptorVerdict::WrongContentType, kBody,
         "text/html (the single-page app's own page answers this path before the deploy)",
         "text/html");
  Expect(DescriptorVerdict::WrongContentType, kBody, "text/html with a charset",
         "text/html; charset=utf-8");
  Expect(DescriptorVerdict::WrongContentType, kBody, "text/json", "text/json");
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/jsonp", "application/jsonp");
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/json-seq",
         "application/json-seq");
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/json5", "application/json5");
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/jso", "application/jso");
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/x-json", "application/x-json");
  Expect(DescriptorVerdict::WrongContentType, kBody, "no content type", "");
  Expect(DescriptorVerdict::WrongContentType, kBody, "a content type that only mentions json",
         "text/plain; format=application/json");
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/json glued to a suffix",
         "application/json+ld");
  // the media type is compared to its last character, whatever else it is as long as
  // application/json: a reader that only measured the type would take another sixteen-character one
  // for it
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/yaml (the same length)",
         "application/yaml");
  Expect(DescriptorVerdict::WrongContentType, kBody, "application/wasm (the same length)",
         "application/wasm");
  Expect(DescriptorVerdict::WrongContentType, kBody, "the last character differs",
         "application/jsom");
  Expect(DescriptorVerdict::WrongContentType, kBody, "the first character differs",
         "bpplication/json");
  Expect(DescriptorVerdict::WrongContentType, kBody, "one in the middle differs",
         "application\\json");
  Expect(DescriptorVerdict::WrongContentType, kBody, "the same length with a parameter",
         "application/yaml; charset=utf-8");

  // the body is not the document
  Expect(DescriptorVerdict::NotJson, "", "an empty body");
  Expect(DescriptorVerdict::NotJson, "<!doctype html><html></html>", "the single-page app's html");
  Expect(DescriptorVerdict::NotJson, R"({"contracts":[1],"platforms":["linux"])",
         "a truncated document");
  Expect(DescriptorVerdict::NotJson, R"([{"contracts":[1],"platforms":["linux"]}])",
         "an array holding the document");
  Expect(DescriptorVerdict::NotJson, R"("contracts")", "a string");
  Expect(DescriptorVerdict::NotJson, "1", "a number");
  Expect(DescriptorVerdict::NotJson, "null", "null");
  Expect(DescriptorVerdict::NotJson, R"({"contracts":[1],"platforms":["linux"]} trailing)",
         "text after the document");
  Expect(DescriptorVerdict::NotJson, R"({"contracts":[1],"platforms":["linux"],})",
         "a trailing comma");
  Expect(DescriptorVerdict::NotJson, R"({'contracts':[1],'platforms':['linux']})", "single quotes");
  Expect(DescriptorVerdict::TooLarge, std::string(5000, ' ') + std::string(kBody),
         "a body over the cap");
  Expect(DescriptorVerdict::TooLarge, std::string(kMaxDescriptorBytes + 1, 'a'),
         "a body one over the cap");
  Expect(DescriptorVerdict::NotJson,
         std::string(kMaxDescriptorBytes - 60, ' ') + std::string(kBody).substr(0, 20),
         "a body at the cap that is not the document");
  Expect(DescriptorVerdict::Offered,
         std::string(kMaxDescriptorBytes - kBody.size(), ' ') + std::string(kBody),
         "the document padded to exactly the cap");

  // the contracts
  Expect(DescriptorVerdict::NoContracts, R"({"platforms":["linux"]})", "no contracts");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":[],"platforms":["linux"]})",
         "an empty list");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":[2],"platforms":["linux"]})",
         "version 2 alone (the judgment is for the official network's version: ReadDescriptor is "
         "where 2 counts)");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":[0,2,3],"platforms":["linux"]})",
         "other versions");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":1,"platforms":["linux"]})",
         "a number, not a list");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":"1","platforms":["linux"]})",
         "a string, not a list");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":["1"],"platforms":["linux"]})",
         "the number as a string");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":[true],"platforms":["linux"]})",
         "true is not the number one");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":[1.5],"platforms":["linux"]})",
         "one and a half");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":[1.0],"platforms":["linux"]})",
         "a float is not the integer");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":[[1]],"platforms":["linux"]})",
         "a list in the list");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":{"1":true},"platforms":["linux"]})",
         "an object");
  Expect(DescriptorVerdict::NoContracts, R"({"contracts":null,"platforms":["linux"]})", "null");
  Expect(DescriptorVerdict::NoContracts, R"({"contract":1,"platforms":["linux"]})",
         "the page's first descriptor (a `contract` key, not `contracts`)");
  Expect(DescriptorVerdict::NoContracts, R"({"Contracts":[1],"platforms":["linux"]})",
         "keys are case-sensitive");

  // the platforms
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1,2],"platforms":["windows"]})",
         "today's deployed descriptor: nothing is offered until the page lists linux");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1]})", "no platforms");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":[]})", "an empty list");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":["windows"]})",
         "another platform");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":["Linux"]})",
         "another case");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":["linux "]})",
         "a trailing blank");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":["linux2"]})",
         "a longer name");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":"linux"})",
         "a string, not a list");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":[1]})", "a number");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":[["linux"]]})",
         "a list in the list");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":{"linux":true}})",
         "an object");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platforms":null})", "null");
  Expect(DescriptorVerdict::NoPlatform, R"({"contracts":[1],"platform":"linux"})",
         "a `platform` key, not `platforms`");

  // asking about another contract or platform is the same judgment
  Check(CheckDescriptor(kJson, R"({"contracts":[2],"platforms":["windows"]})", 2, "windows") ==
            DescriptorVerdict::Offered,
        "the judgment is the same for another contract and platform");
  Check(CheckDescriptor(kJson, kBody, 2, "linux") == DescriptorVerdict::NoContracts,
        "and refuses what it was not asked for");
}

// What the page lists, per version: the official network needs 1, another network needs 2, and the
// answer is read once.
UR_TEST(ApprovalDescriptor_ThePageListsTheVersionsItServes) {
  const auto read = [](std::string_view body, std::string_view contentType = kJson) {
    return ReadDescriptor(contentType, body);
  };
  const auto lists = [](const Listing& listing) {
    std::string out;
    if (listing.Lists(1)) out += "1";
    if (listing.Lists(2)) out += "2";
    return out.empty() ? std::string("none") : out;
  };
  const auto expect = [&](std::string_view body, DescriptorVerdict verdict,
                          const std::string& versions, const std::string& what) {
    const Listing listing = read(body);
    Check(listing.verdict == verdict && lists(listing) == versions,
          "listing: " + what + " (got " + std::string(ToString(listing.verdict)) + ", lists " +
              lists(listing) + ")");
  };

  // the page as it is, an older page, and a page that serves another network only
  expect(kPageBody, DescriptorVerdict::Offered, "12", "the page's descriptor lists both versions");
  expect(kBody, DescriptorVerdict::Offered, "1",
         "an older page lists the official network's version only, and is never sent version 2");
  expect(R"({"contracts":[2],"platforms":["linux"]})", DescriptorVerdict::Offered, "2",
         "a page that lists 2 only serves another network only");
  expect(R"({"contracts":[2,1],"platforms":["linux"]})", DescriptorVerdict::Offered, "12",
         "the order does not matter");
  expect(R"({"contracts":[1,1,2,2],"platforms":["linux"]})", DescriptorVerdict::Offered, "12",
         "nor does a version listed twice");
  expect(R"({"contracts":[0,1,3,4,5],"platforms":["linux"]})", DescriptorVerdict::Offered, "1",
         "versions the app does not speak are ignored");
  expect(R"({"contracts":[3,2,100,-2],"platforms":["linux"]})", DescriptorVerdict::Offered, "2",
         "so are versions far away or below zero");
  expect(R"({"contracts":[1],"platforms":["windows","linux"],"extra":{"contracts":[2]}})",
         DescriptorVerdict::Offered, "1", "a key it does not know cannot add a version");
  expect("  \n{ \"contracts\" : [ 1 , 2 ] , \"platforms\" : [ \"linux\" ] }\n",
         DescriptorVerdict::Offered, "12", "whitespace around the tokens");
  expect("\xEF\xBB\xBF{\"contracts\":[1,2],\"platforms\":[\"linux\"]}", DescriptorVerdict::Offered,
         "12", "a byte order mark");

  // an entry of the wrong type lists nothing, whatever it spells
  expect(R"({"contracts":["1","2"],"platforms":["linux"]})", DescriptorVerdict::NoContracts, "none",
         "the versions as strings");
  expect(R"({"contracts":[1.0,2.0],"platforms":["linux"]})", DescriptorVerdict::NoContracts, "none",
         "the versions as floats");
  expect(R"({"contracts":[true,[2],{"2":2},null],"platforms":["linux"]})",
         DescriptorVerdict::NoContracts, "none", "a bool, a list, an object and null");
  expect(R"({"contracts":[1,"2"],"platforms":["linux"]})", DescriptorVerdict::Offered, "1",
         "version 1 as a number and 2 as a string lists 1 only");
  expect(R"({"contracts":["1",2],"platforms":["linux"]})", DescriptorVerdict::Offered, "2",
         "and the other way round lists 2 only");
  expect(R"({"contracts":2,"platforms":["linux"]})", DescriptorVerdict::NoContracts, "none",
         "a number, not a list");
  expect(R"({"contracts":{"1":1,"2":2},"platforms":["linux"]})", DescriptorVerdict::NoContracts,
         "none", "an object, not a list");
  expect(R"({"contracts":[],"platforms":["linux"]})", DescriptorVerdict::NoContracts, "none",
         "an empty list");
  expect(R"({"contracts":[3],"platforms":["linux"]})", DescriptorVerdict::NoContracts, "none",
         "only a version the app does not speak");
  expect(R"({"platforms":["linux"]})", DescriptorVerdict::NoContracts, "none", "no contracts");
  expect(R"({"contract":2,"platforms":["linux"]})", DescriptorVerdict::NoContracts, "none",
         "a `contract` key, not `contracts`");

  // a page that does not list this platform lists nothing for it, whatever the versions say
  expect(R"({"contracts":[1,2],"platforms":["windows"]})", DescriptorVerdict::NoPlatform, "none",
         "both versions, another platform");
  expect(R"({"contracts":[1,2]})", DescriptorVerdict::NoPlatform, "none",
         "both versions, no platforms");
  expect(R"({"contracts":[1,2],"platforms":["windows"]})", DescriptorVerdict::NoPlatform, "none",
         "today's deployed descriptor lists windows only: nothing is offered until the page lists "
         "linux");
  expect(R"({"contracts":[2],"platforms":["windows"]})", DescriptorVerdict::NoPlatform, "none",
         "version 2 and another platform: the word is the platform's, the list was in order");
  expect(R"({"contracts":[1],"platforms":["windows"]})", DescriptorVerdict::NoPlatform, "none",
         "version 1 and another platform");
  expect(R"({"contracts":[3],"platforms":["windows"]})", DescriptorVerdict::NoContracts, "none",
         "another version and another platform: the versions come first");
  expect(R"({"contracts":[1,2],"platforms":["Linux"]})", DescriptorVerdict::NoPlatform, "none",
         "the platform's case");
  expect(R"({"contracts":[1,2],"platforms":"linux"})", DescriptorVerdict::NoPlatform, "none",
         "a platform that is a string, not a list");

  // not a descriptor at all: the same word the single-version judgment gives, and nothing listed
  const std::vector<std::pair<std::string, std::string_view>> notDescriptors = {
      {"", kJson},
      {"<!doctype html><html></html>", kJson},
      {R"({"contracts":[1,2],"platforms":["linux"])", kJson},
      {R"([{"contracts":[1,2],"platforms":["linux"]}])", kJson},
      {"null", kJson},
      {"1", kJson},
      {std::string(kPageBody), "text/html"},
      {std::string(kPageBody), ""},
      {std::string(kPageBody), "application/jsonp"},
      {std::string(kPageBody), "application/yaml"},
      {std::string(5000, ' ') + std::string(kPageBody), kJson},
      {std::string(kMaxDescriptorBytes + 1, 'a'), kJson},
      {std::string(kPageBody) + " trailing", kJson},
      {R"({"contracts":[1,2],"platforms":["linux"],})", kJson}};
  for (const auto& [body, contentType] : notDescriptors) {
    const Listing listing = read(body, contentType);
    const DescriptorVerdict single = CheckDescriptor(contentType, body);
    Check(listing.verdict == single && listing.verdict != DescriptorVerdict::Offered &&
              listing.contracts == 0,
          "listing: a response that is no descriptor ('" + body.substr(0, 24) + "', " +
              std::string(contentType) + ") lists nothing and says " +
              std::string(ToString(listing.verdict)));
  }
  expect(std::string(kMaxDescriptorBytes - kPageBody.size(), ' ') + std::string(kPageBody),
         DescriptorVerdict::Offered, "12", "the descriptor padded to exactly the cap");

  // the content type is judged for the listing as it is for the single version
  Check(read(kPageBody, "Application/JSON; charset=utf-8").Lists(2) &&
            read(kPageBody, " application/json").Lists(1),
        "listing: the content type's case, parameters and blanks");
  Check(!read(kPageBody, "text/json").Lists(1) && !read(kPageBody, "text/json").Lists(2),
        "listing: text/json lists nothing");

  // it agrees with the single-version judgment on every body: a version is listed exactly when that
  // judgment offers it, and the page is offered exactly when one of the two does
  const std::vector<std::string> bodies = {
      std::string(kPageBody),
      std::string(kBody),
      R"({"contracts":[2],"platforms":["linux"]})",
      R"({"contracts":[3],"platforms":["linux"]})",
      R"({"contracts":[1,2],"platforms":["windows"]})",
      R"({"contracts":[2],"platforms":["windows"]})",
      R"({"contracts":[],"platforms":[]})",
      R"({"platforms":["linux"]})",
      R"({"contracts":[1,2]})",
      R"({"contracts":["1",2],"platforms":["linux"]})",
      R"({"contracts":[1.0,2],"platforms":["linux"]})",
      R"({"contracts":null,"platforms":null})",
      R"([])",
      "",
      "{}",
      R"({"contracts":[0,1,2,3],"platforms":["linux","linux"]})"};
  for (const std::string& body : bodies) {
    const Listing listing = read(body);
    const bool official = CheckDescriptor(kJson, body, kContract) == DescriptorVerdict::Offered;
    const bool named = CheckDescriptor(kJson, body, kContractNamed) == DescriptorVerdict::Offered;
    Check(listing.Lists(1) == official && listing.Lists(2) == named &&
              (listing.verdict == DescriptorVerdict::Offered) == (official || named),
          "listing: agrees with the single-version judgment on '" + body + "'");
  }

  // the bit set is read in range only, and for the versions the app speaks nothing else is set
  Listing wide;
  wide.contracts = 0xFFFFFFFFU;
  Check(wide.Lists(0) && wide.Lists(1) && wide.Lists(2) && wide.Lists(31) && !wide.Lists(32) &&
            !wide.Lists(33) && !wide.Lists(64) && !wide.Lists(-1) && !wide.Lists(1000000),
        "listing: a version outside 0 to 31 is not listed, however the bits stand");
  Listing only2;
  only2.contracts = 1U << 2;
  Check(!only2.Lists(0) && !only2.Lists(1) && only2.Lists(2) && !only2.Lists(3),
        "listing: the bit of a version is 1 shifted by it");
  Check(Listing{}.contracts == 0 && !Listing{}.Lists(1) && !Listing{}.Lists(2) &&
            Listing{}.verdict != DescriptorVerdict::Offered,
        "listing: nothing is listed until a page has been read");
  for (const std::string& body : bodies) {
    const Listing listing = read(body);
    Check((listing.contracts & ~((1U << 1) | (1U << 2))) == 0,
          "listing: only the bits of versions 1 and 2 can be set ('" + body.substr(0, 40) + "')");
  }
}

UR_TEST(ApprovalDescriptor_TheTokensAreClosed) {
  std::set<std::string> seen;
  bool lowercase = true;
  for (const DescriptorVerdict v :
       {DescriptorVerdict::Offered, DescriptorVerdict::TooLarge,
        DescriptorVerdict::WrongContentType, DescriptorVerdict::NotJson,
        DescriptorVerdict::NoContracts, DescriptorVerdict::NoPlatform}) {
    const std::string token(ToString(v));
    seen.insert(token);
    for (const char c : token) lowercase = lowercase && ((c >= 'a' && c <= 'z') || c == '-');
  }
  Check(seen.size() == 6, "tokens: one per verdict, none repeated");
  Check(lowercase, "tokens: lowercase letters and hyphens only");
}

}  // namespace

// None of the verdict's log tokens is a key of the store, which the catalog tests would read as a
// lookup the store has to tag linux.
UR_TEST(ApprovalDescriptor_TheTokensAreNotKeysOfTheStore) {
  std::set<std::string> keys;
  std::istringstream lines(urnw::testing::wiring::ReadRaw("../po/linux-keys.txt"));
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] != '#') keys.insert(line);
  }
  Check(keys.size() > 500, "tokens: po/linux-keys.txt was read");
  for (const DescriptorVerdict v :
       {DescriptorVerdict::Offered, DescriptorVerdict::TooLarge,
        DescriptorVerdict::WrongContentType, DescriptorVerdict::NotJson,
        DescriptorVerdict::NoContracts, DescriptorVerdict::NoPlatform}) {
    const std::string token(ToString(v));
    Check(keys.count(token) == 0, "tokens: the log token '" + token + "' is a key of the store");
  }
}

// The header is the pure judgment: nlohmann/json and ApprovalNetwork.hpp, and the platform it
// judges for is the one the request names.
UR_TEST(ApprovalDescriptor_TheHeaderIsThePureJudgment) {
  const std::string code = urnw::testing::wiring::ReadCode("ApprovalDescriptor.hpp");
  for (const char* include :
       {"#include <chrono>", "#include <cstddef>", "#include <cstdint>", "#include <string_view>",
        "#include <nlohmann/json.hpp>", "#include \"ApprovalNetwork.hpp\""}) {
    Check(urnw::testing::wiring::Contains(code, include),
          std::string("header: ApprovalDescriptor.hpp has ") + include);
  }
  Check(urnw::testing::wiring::CountOf(code, "#include") == 6, "header: and no other include");
  for (const char* forbidden :
       {"kPlatform =", "<glib", "<gio", "<libsoup", "g_message", "g_warning", "printf"}) {
    Check(!urnw::testing::wiring::Contains(code, forbidden),
          std::string("header: ApprovalDescriptor.hpp has ") + forbidden);
  }
  Check(urnw::testing::wiring::Contains(code, "std::int64_t contract = kContract,") &&
            urnw::testing::wiring::Contains(code, "std::string_view platform = kPlatform)"),
        "header: the judgment defaults to ApprovalNetwork.hpp's contract and platform");
}
