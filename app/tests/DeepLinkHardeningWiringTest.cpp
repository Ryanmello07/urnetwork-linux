// A urnetwork:// link is something any program on the machine can fire at the
// app, and the wallet bridges' returns (urnetwork://phantom-connect and the
// rest) are opened on the GTK main loop: an exception the SDK throws for a
// nonce, data or key of the wrong length must stay inside the handler, which
// answers it as the failure it already reports, instead of leaving the loop
// and aborting the app. The sign-in return's rejection line names the reason
// in this app's own words, never the error= text the link carried. The
// handlers need GTK and the SDK, so this reads their sources.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::CountOf;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

namespace {

// Each needle occurs after the one before it.
bool DeepLinkInOrder(const std::string& text, const std::vector<std::string>& needles) {
  size_t from = 0;
  for (const std::string& needle : needles) {
    const size_t at = text.find(needle, from);
    if (at == std::string::npos) return false;
    from = at + needle.size();
  }
  return true;
}

// The text with the inside of every string literal blanked, so a call that is
// only named in a message is not taken for the call.
std::string BlankStrings(const std::string& code) {
  std::string text = code;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '"') continue;
    for (++i; i < text.size() && text[i] != '"' && text[i] != '\n'; ++i) {
      if (text[i] == '\\' && i + 1 < text.size()) text[i++] = ' ';
      text[i] = ' ';
    }
  }
  return text;
}

bool StartsAfterSpace(const std::string& text, size_t from, const std::string& word) {
  while (from < text.size() &&
         (text[from] == ' ' || text[from] == '\t' || text[from] == '\r' || text[from] == '\n')) {
    ++from;
  }
  return text.compare(from, word.size(), word) == 0;
}

bool EndsBeforeSpace(const std::string& text, size_t end, const std::string& word) {
  while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                     text[end - 1] == '\n')) {
    --end;
  }
  return end >= word.size() && text.compare(end - word.size(), word.size(), word) == 0;
}

// The SDK calls that throw urnet::Error in `code` (comments already blanked)
// that are not directly inside a try block whose catch takes std::exception;
// `found` counts every such call, guarded or not.
std::vector<std::string> UnguardedThrowers(const std::string& code, int& found) {
  const std::string text = BlankStrings(code);
  std::vector<std::string> problems;
  found = 0;
  for (const std::string call : {"urnet::decryptData(", "urnet::generateSharedSecret("}) {
    for (size_t at = text.find(call); at != std::string::npos; at = text.find(call, at + 1)) {
      ++found;
      // the brace that opens the block the call sits in
      size_t start = std::string::npos;
      int depth = 0;
      for (size_t i = at + 1; i-- > 0 && start == std::string::npos;) {
        if (text[i] == '}') ++depth;
        if (text[i] == '{') {
          if (depth == 0) {
            start = i;
          } else {
            --depth;
          }
        }
      }
      bool guarded = false;
      if (start != std::string::npos && EndsBeforeSpace(text, start, "try")) {
        depth = 0;
        for (size_t i = start; i < text.size(); ++i) {
          if (text[i] == '{') ++depth;
          if (text[i] == '}' && --depth == 0) {
            guarded = StartsAfterSpace(text, i + 1, "catch (const std::exception&");
            break;
          }
        }
      }
      if (!guarded) {
        problems.push_back(call.substr(0, call.size() - 1) + " at offset " + std::to_string(at) +
                           " is not inside a try that catches std::exception");
      }
    }
  }
  return problems;
}

}  // namespace

// The two encrypted wallet returns decrypt through TryDecrypt, and a
// decryption that failed (no answer, or an empty one) is the handler's error
// and not a document: without the check the handler reads the beginning of a
// vector that is not there.
UR_TEST(DeepLinkHardening_TheWalletReturnsDecryptThroughTryDecrypt) {
  const std::string wallet = ReadCode("WalletConnect.cpp");
  UR_EXPECT_TRUE_MSG("one decryptData call in WalletConnect.cpp",
                     CountOf(wallet, "urnet::decryptData(") == 1);
  const std::string tryDecrypt =
      FunctionBody(wallet, "std::optional<std::vector<uint8_t>> TryDecrypt(");
  UR_EXPECT_TRUE_MSG("the call is TryDecrypt's", Contains(tryDecrypt, "urnet::decryptData("));
  UR_EXPECT_TRUE(Before(tryDecrypt, "try {", "urnet::decryptData("));
  UR_EXPECT_TRUE(
      Contains(tryDecrypt, "} catch (const std::exception&) {\n    return std::nullopt;"));
  const struct {
    const char* handler;
    const char* failure;
  } handlers[] = {
      {"void WalletConnect::HandleConnect(", "failed to decrypt wallet connection"},
      {"void WalletConnect::HandleSignMessage(", "failed to decrypt wallet signature"},
  };
  for (const auto& h : handlers) {
    const std::string body = FunctionBody(wallet, h.handler);
    UR_EXPECT_TRUE_MSG(h.handler, !body.empty());
    UR_EXPECT_TRUE_MSG(
        std::string(h.handler) + " checks the decryption before it reads it",
        DeepLinkInOrder(body,
                        {"const auto decrypted = TryDecrypt(params[\"data\"], params[\"nonce\"], "
                         "*sharedB58);",
                         "if (!decrypted || decrypted->empty()) {",
                         std::string("if (on_error) on_error(\"") + h.failure + "\");", "return;",
                         "decrypted->begin()"}));
    UR_EXPECT_TRUE_MSG(std::string(h.handler) + " decrypts without TryDecrypt",
                       !Contains(body, "urnet::decryptData("));
  }
}

// The wallet's encryption key came off the link: generateSharedSecret throws
// for a key of the wrong length.
UR_TEST(DeepLinkHardening_TheSharedSecretCatchesWhatTheSdkThrows) {
  const std::string body =
      FunctionBody(ReadCode("WalletConnect.cpp"),
                   "std::optional<std::string> WalletConnect::SharedSecretBase58() const {");
  UR_EXPECT_TRUE(Before(body, "try {", "urnet::generateSharedSecret("));
  UR_EXPECT_TRUE(Before(body, "urnet::encodeBase58(", "} catch (const std::exception&) {"));
  UR_EXPECT_TRUE(Contains(body, "} catch (const std::exception&) {\n    return std::nullopt;"));
}

// No source calls one of the SDK's throwing crypto functions outside a try
// that catches std::exception, and WalletConnect.cpp and ApprovalUnseal.hpp are
// found calling them.
UR_TEST(DeepLinkHardening_NoSdkCryptoCallIsOutsideATry) {
  const std::filesystem::path src(UR_SRC_DIR);
  if (!std::filesystem::is_directory(src)) {
    UR_FAIL("UR_SRC_DIR is not a directory");
    return;
  }
  int scanned = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
    if (!entry.is_regular_file()) continue;
    const std::string ext = entry.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
    ++scanned;
    const std::string name = std::filesystem::relative(entry.path(), src).string();
    int found = 0;
    for (const std::string& problem : UnguardedThrowers(ReadCode(name), found)) {
      UR_FAIL(name + ": " + problem);
    }
    if (name == "WalletConnect.cpp") {
      UR_EXPECT_TRUE_MSG("WalletConnect.cpp calls the SDK's crypto", found == 2);
    }
    // the approval's one box: two shared secrets (the page's and the small-order constant) and
    // the decryption, all inside Unseal's try
    if (name == "ApprovalUnseal.hpp") {
      UR_EXPECT_TRUE_MSG("ApprovalUnseal.hpp calls the SDK's crypto", found == 3);
    }
  }
  UR_EXPECT_TRUE_MSG("the scan saw the sources", scanned > 10);
}

// The approval's unseal opens the page's box inside one try that catches
// std::exception, and nothing it holds outlives it unwiped.
UR_TEST(DeepLinkHardening_TheApprovalsUnsealCatchesWhatTheSdkThrows) {
  const std::string unseal =
      FunctionBody(ReadCode("ApprovalUnseal.hpp"), "inline std::optional<std::string> Unseal(");
  UR_EXPECT_TRUE_MSG("approval::Unseal", !unseal.empty());
  UR_EXPECT_TRUE(Before(unseal, "const detail::OnExit overwrite{[&] {", "try {"));
  UR_EXPECT_TRUE(
      DeepLinkInOrder(unseal, {"try {", "urnet::generateSharedSecret(*secret, *sealedBy);",
                               "urnet::generateSharedSecret(*secret, std::vector<uint8_t>(32, 0));",
                               "detail::SameBytes(shared, constant)",
                               "urnet::decryptData(link.data, link.nonce, sharedBase58);",
                               "SameSecret(state->get<std::string>(), link.state)",
                               "} catch (const std::exception&) {\n    return std::nullopt;"}));
  UR_EXPECT_EQ(std::size_t{1}, CountOf(unseal, "try {"));
  for (const char* wiped : {"Wipe(secretText);", "if (secret) Wipe(*secret);", "Wipe(shared);",
                            "Wipe(constant);", "Wipe(plain);", "Wipe(sharedBase58);"}) {
    UR_EXPECT_TRUE_MSG(std::string("Unseal wipes ") + wiped, Contains(unseal, wiped));
  }
}

// The check itself.
UR_TEST(DeepLinkHardening_TheCheckFindsACallOutsideItsTry) {
  int found = 0;
  const std::string unguarded = "void F() {\n  auto d = urnet::decryptData(a, b, c);\n  g(d);\n}\n";
  UR_EXPECT_TRUE_MSG("a decryptData call outside a try",
                     UnguardedThrowers(unguarded, found).size() == 1 && found == 1);
  const std::string guarded =
      "void F() {\n  try {\n    auto d = urnet::decryptData(a, b, c);\n  } catch (const "
      "std::exception&) {\n    return;\n  }\n}\n";
  UR_EXPECT_TRUE_MSG("a decryptData call inside a try",
                     UnguardedThrowers(guarded, found).empty() && found == 1);
  const std::string wrongCatch =
      "void F() {\n  try {\n    auto d = urnet::decryptData(a, b, c);\n  } catch (const "
      "nlohmann::json::exception&) {\n  }\n}\n";
  UR_EXPECT_TRUE_MSG("a try that catches something else",
                     UnguardedThrowers(wrongCatch, found).size() == 1);
  const std::string nested =
      "void F() {\n  try {\n    if (x) {\n      auto d = urnet::generateSharedSecret(a, b);\n    "
      "}\n  } catch (const std::exception&) {\n  }\n}\n";
  UR_EXPECT_TRUE_MSG("a call in a block of its own inside the try",
                     UnguardedThrowers(nested, found).size() == 1);
  const std::string named = "void F() {\n  g(\"urnet::decryptData(\");\n}\n";
  UR_EXPECT_TRUE_MSG("a call only named in a string",
                     UnguardedThrowers(named, found).empty() && found == 0);
}

// The sign-in return's error= is text from whatever program fired the link:
// the rejection line says that the return carried one, and never repeats it.
UR_TEST(DeepLinkHardening_ARejectedSignInReturnDoesNotLogTheLinksErrorText) {
  const std::string host = ReadCode("SdkHost.cpp");
  const size_t line = host.find("\"[sso] rejected return for %s: %s\\n\"");
  UR_EXPECT_TRUE_MSG("the rejection line", line != std::string::npos);
  if (line == std::string::npos) return;
  const std::string call = host.substr(line, host.find(';', line) - line);
  UR_EXPECT_TRUE_MSG("the line passes the reason it chose",
                     Contains(call, "provider.c_str(), why)"));
  UR_EXPECT_TRUE_MSG("the line passes the verdict's error as it is",
                     !Contains(call, "verdict.error.c_str()"));
  const size_t why = host.rfind("const char* why = ", line);
  UR_EXPECT_TRUE_MSG("the reason is chosen right before the line",
                     why != std::string::npos && line - why < 300);
  if (why == std::string::npos) return;
  const std::string choice = host.substr(why, line - why);
  UR_EXPECT_TRUE(
      DeepLinkInOrder(choice, {"!r.error.empty() && verdict.error == r.error",
                               "? \"the return carried an error\"", ": verdict.error.c_str();"}));
  UR_EXPECT_EQ(1, CountOf(host, "[sso] rejected return"));
}
