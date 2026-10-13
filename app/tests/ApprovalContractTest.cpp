// The approval contract this app keeps (docs/app-approval-contract.md, the
// repository's copy of the Windows app's, amended for the platform linux) and
// the vector it commits: the sentences the Windows app's tests pin, the ones
// the Linux amendments add, and the request vector of version 2 put together
// from its own parts. The document is read with its whitespace squashed, so a
// sentence may wrap anywhere.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "ApprovalNetwork.hpp"
#include "ApprovalVector.hpp"
#include "WiringSource.hpp"

using urnw::testing::wiring::Contains;
using urnw::testing::wiring::ReadCode;
using urnw::testing::wiring::ReadRaw;

namespace {

// The text with every run of whitespace made one space.
std::string Squash(const std::string& text) {
  std::istringstream words(text);
  std::string out;
  for (std::string word; words >> word;) {
    if (!out.empty()) out += ' ';
    out += word;
  }
  return out;
}

const std::string& Contract() {
  static const std::string contract = Squash(ReadRaw("../../docs/app-approval-contract.md"));
  return contract;
}

void RequireSentences(const std::string& where, const std::vector<std::string>& sentences) {
  for (const std::string& sentence : sentences) {
    UR_EXPECT_TRUE_MSG(where + " is missing \"" + sentence + "\"", Contains(Contract(), sentence));
  }
}

}  // namespace

UR_TEST(ApprovalContract_TheDocumentIsThere) {
  UR_EXPECT_TRUE_MSG("docs/app-approval-contract.md was read", Contract().size() > 10000);
  UR_EXPECT_TRUE(Contains(Contract(), "# URnetwork app approval contract, version 1"));
}

// Section 5, rule 1, and the model that keeps it (UrIoApproval.hpp).
UR_TEST(ApprovalContract_SaysWhenASecondClickReopens) {
  RequireSentences(
      "section 5, rule 1",
      {"a second click re-opens the same request instead of starting another, but only "
       "while the page would still accept it (200 s after `ts`, inside the page's 240 s "
       "window); later it starts a new attempt, which replaces the old one."});
  const std::string model = ReadCode("UrIoApproval.hpp");
  UR_EXPECT_TRUE(Contains(model, "inline constexpr std::chrono::seconds kReopenWindow{200};"));
  UR_EXPECT_TRUE(Contains(model, "static_assert(kReopenWindow < kLife);"));
}

// Section 11 and the rules of section 5 that name the network, as the Windows app's tests pin
// them; the descriptor's body is the one sentence the Linux copy changes (it lists linux).
UR_TEST(ApprovalContract_NamesTheNetworkOfVersionTwo) {
  RequireSentences(
      "section 11",
      {"Version 2 is version 1 plus one key, `api`:",
       "<page URL>#v=2&platform=windows&state=<S>&pk=<PK>&ts=<T>&api=<A>",
       "the host of the app's API, `host` or `host:port`, `https` implied: ASCII lower-case "
       "letters, "
       "digits, hyphens and dots, never a scheme, path, userinfo or query.",
       "Labels of 1 to 63 characters that start and end with a letter or digit; at least two "
       "labels; "
       "the last label is neither all digits nor `0x` followed by hex digits (a browser reads "
       "either "
       "as a number, so no IP literal in any spelling); a port is 1 to 65535 without a leading "
       "zero "
       "and is never `443`; at most 200 characters in all.",
       "The app sends version 2 only when its network space is not the official one, its API URL "
       "is "
       "`https://` plus a valid `api` value (no path, query or fragment; an `http` API, an IP "
       "literal "
       "or a non-ASCII host is not offered), and the descriptor lists 2. In the official network "
       "it "
       "sends version 1, whatever else the descriptor lists.",
       "It is read the way a URL is read, which is how the client and the page read it: capitals "
       "in "
       "the scheme or the host are the same host in small letters, an explicit `:443` is https' "
       "own "
       "port and names the same server without it, and a single trailing `/` is ignored; the `api` "
       "value is the small-letter form without `:443`.",
       "The official network is the operator's own space (its host, and no API or connect URL of "
       "the "
       "person's own) whose API really is the operator's `api.<host>`: a space with the operator's "
       "host but another API under it (a test environment of the operator, whose name the SDK puts "
       "in "
       "front of the service label) is named by that API like any other network",
       "The descriptor (section 1) is fetched once per run in every network whose API can be "
       "named, "
       "not only in the official one, because the version a network needs is listed in it.",
       "that request carries nothing of the network, and the app makes none when no network can be "
       "named.",
       "The attempt is bound to the network it started in: a network change ends it (section 5, "
       "rule "
       "5), and the redeem goes to the API the app is on.",
       "The binding is checked once more where the code leaves the app, because an accepted answer "
       "can "
       "wait for a sheet in the way to close: if a network change (or any other sign-in flow) has "
       "begun since the attempt did, nothing is sent and the redeem is refused.",
       "The page waits up to 30 s for each API call",
       "body `{\"contracts\":[1,2],\"platforms\":[\"windows\",\"linux\"]}`",
       "version 1 keeps its one action, 'Use the default server', which first signs this browser "
       "out "
       "of the server it leaves, then removes the override and goes on to the sign-in on the "
       "default "
       "server.",
       "the stored session belongs to the other server and is never sent to the new one, by this "
       "tab "
       "or by any other tab of the browser",
       "and then only if the browser's own URL parser gives it back exactly as written (`new "
       "URL('https://' + host).host` is `host`)",
       "For a host it reads otherwise Switch does nothing, and Cancel still answers.",
       "only in a network a request can name (the official one, version 1; another whose API url "
       "section 11 allows, version 2) and after the descriptor check lists the version that "
       "request "
       "is.",
       "and only while no network change or other sign-in flow has begun since the attempt did "
       "(section 11).",
       "a network-space change and quit"});
  UR_EXPECT_TRUE_MSG(
      "the descriptor body of before the Linux row",
      !Contains(Contract(), "body `{\"contracts\":[1,2],\"platforms\":[\"windows\"]}`"));
}

// Section 7's cases and section 9's reject vectors for a server that is not the request's, and an
// answer that outlives its network; and the sentences that stopped being true are gone.
UR_TEST(ApprovalContract_CasesNameTheServerMismatchAndTheRefusedRedeem) {
  RequireSentences(
      "the approval contract",
      {"if the browser holds a custom API base the page does not run the approval but shows a "
       "'Different API server' card whose one action is 'Use the default server' (section 7)",
       "| Framed, or has an opener | Non-interactive message | nothing |",
       "| The browser's API server is not the one the request is for (version 1: not the default "
       "server; version 2: not the host `api` names) | Before any sign-in redirect and before any "
       "call "
       "to any API: 'Different API server', the hosts as plain text. Version 1: 'Use the default "
       "server', which signs this browser out of the server it leaves, removes the override and "
       "goes "
       "on to the sign-in. Version 2: Cancel, and 'Switch to {host}' behind 'Only continue if you "
       "set "
       "this server in URnetwork yourself.' (signs this browser out, points it at "
       "`https://{host}`, "
       "signs in, back to the card) | nothing, or `access_denied` after Cancel |",
       "| A network change (or any other sign-in flow) begins after the answer was accepted | "
       "Before "
       "the code is sent: nothing is sent, and the redeem is refused like a spent code. While the "
       "redeem is out: what it brings back is dropped, not held for a confirm, and the redeem is "
       "refused likewise |",
       "an all-digit last label, a last label of `0x` and hex digits (`a.0x10`, `1.2.3.0x4`, "
       "`0x7f.0x0.0x0.0x1`, `a.0x`), an empty or hyphen-edged label"});
  for (const char* gone : {"Framed, has opener, or custom API base",
                           "the page refuses to run if the browser holds a custom API base",
                           "the last label is not all digits (so no IP literal)",
                           "its API URL is exactly `https://`"}) {
    UR_EXPECT_TRUE_MSG(std::string("the contract still says \"") + gone + "\"",
                       !Contains(Contract(), gone));
  }
}

// The amendments that make it this app's copy: the status, the platform row, the requests, the
// handler read back through GIO, no Copy link, the wording, the reject list (linux is a platform
// now, so macos and the capitalised Linux are the rejected ones) and section 10.
UR_TEST(ApprovalContract_NamesTheLinuxApp) {
  RequireSentences(
      "the Linux amendments",
      {"The Linux app implements section 5 too (platform `linux`, 2026-10-13).",
       "`windows` -> label `URnetwork for Windows`, return base `urnetwork://auth`; `linux` -> "
       "label "
       "`URnetwork for Linux`, return base `urnetwork://auth`.",
       "Make sure the per-user `urnetwork://` handler starts this app (read it back) before "
       "opening "
       "the browser (Windows: the HKCU key points at the running exe, read back through the shell; "
       "Linux: the desktop's default handler for `x-scheme-handler/urnetwork` is the app's own "
       "desktop entry `com.bringyour.network.desktop`, read back through GIO and never written); "
       "if "
       "that fails, fall back to the paste flow.",
       "a field for a pasted code is a password box. The Linux app does not offer it.",
       "Wording as built in the Windows app (2026-10-11) and the Linux app (2026-10-13).",
       "they need localization keys that the store tags for the app's platform,",
       "With the build flag off the sheet is the paste flow: on Windows its one button reads the "
       "store's `adv_open_ur_io` ('Open ur.io'); the Linux app keeps its auth code dialog. "
       "'Continue "
       "in browser' is this document's name for the action; no key carries it.",
       "platform `macos`, `Linux`, `__proto__`", "platforms other than Windows and Linux;"});
  // after each request template
  std::size_t notes = 0;
  const std::string note = "(The Linux app sends `platform=linux`; nothing else differs.)";
  for (std::size_t at = Contract().find(note); at != std::string::npos;
       at = Contract().find(note, at + 1)) {
    ++notes;
  }
  UR_EXPECT_EQ(std::size_t{2}, notes);
  UR_EXPECT_TRUE(
      Contains(Contract(), "<page URL>#v=1&platform=windows&state=<S>&pk=<PK>&ts=<T> " + note));
  UR_EXPECT_TRUE(Contains(
      Contract(), "<page URL>#v=2&platform=windows&state=<S>&pk=<PK>&ts=<T>&api=<A> " + note));
  const std::size_t rejects = Contract().find("- Parser reject, each by name:");
  const std::string rejectList =
      rejects == std::string::npos
          ? std::string()
          : Contract().substr(rejects, Contract().find(" - ", rejects + 1) - rejects);
  UR_EXPECT_TRUE_MSG("section 9's reject list", Contains(rejectList, "platform `macos`, `Linux`,"));
  UR_EXPECT_TRUE_MSG("linux is no rejected platform", !Contains(rejectList, "platform `linux`,"));
  UR_EXPECT_TRUE_MSG("the wording of before the Linux app",
                     !Contains(Contract(), "its one button reads 'Continue in browser'"));
  UR_EXPECT_TRUE_MSG("the handler of before the Linux app",
                     !Contains(Contract(), "handler points at the running exe (read it back)"));
}

// The request vector this app and the page commit (section 9): the constants of
// tests/ApprovalVector.hpp put together in the contract's order are the string the contract
// prints for linux, and UrIoApprovalTest builds exactly that from them.
UR_TEST(ApprovalContract_TheRequestVectorIsTheContractsOwn) {
  namespace v = approval_vector;
  UR_EXPECT_TRUE(v::kState == std::string(43, 'S'));
  UR_EXPECT_TRUE(v::kApi == "api.beta-test.net");
  UR_EXPECT_EQ(1790000000, v::kStartedUnix);
  UR_EXPECT_EQ(std::size_t{43}, v::kPublicKey.size());
  const std::string request =
      std::string("v=2&platform=") + std::string(urnw::approval::kPlatform) +
      "&state=" + std::string(v::kState) + "&pk=" + std::string(v::kPublicKey) +
      "&ts=1790000000&api=" + std::string(v::kApi);
  UR_EXPECT_TRUE_MSG("kRequestV2 is the request its own parts make", v::kRequestV2 == request);
  UR_EXPECT_EQ(std::size_t{151}, v::kRequestV2.size());
  RequireSentences(
      "section 9",
      {"- Request vector (version 2), Linux: the same values with `platform=linux`, exactly `" +
           std::string(v::kRequestV2) +
           "` (151 characters, the keys in this order). The "
           "Linux app and the page commit it.",
       "`api` = api.beta-test.net"});
  // the Windows vector is kept beside it, as the Windows app and the page commit it
  RequireSentences("section 9", {"v=2&platform=windows&state=" + std::string(v::kState) +
                                     "&pk=" + std::string(v::kPublicKey) +
                                     "&ts=1790000000&api=" + std::string(v::kApi),
                                 "(153 characters, the keys in this order)"});
  // the spec that builds it reads the vector from the header and nowhere else
  const std::string specText = ReadRaw("../tests/UrIoApprovalTest.cpp");
  for (const char* use : {"#include \"ApprovalVector.hpp\"", "approval_vector::kRequestV2",
                          "approval_vector::kPublicKey", "approval_vector::kApi"}) {
    UR_EXPECT_TRUE_MSG(std::string("UrIoApprovalTest.cpp uses ") + use, Contains(specText, use));
  }
}
