// The call sites of the control reply's encoding: every frame the daemon sends
// is encoded inside a try, on the glib callback that sends it, and the log ring
// cuts lines without splitting a character. The encoding itself is pure and
// runs in ControlProtocolTest.cpp and Utf8TruncateTest.cpp; the daemon needs
// glib, so this reads its sources with the line comments blanked, so prose
// cannot satisfy a contract.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

// The source with every // comment blanked; string literals are kept.
std::string ReadReplySource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  bool inString = false;
  for (size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (inString) {
      if (c == '\\') {
        ++at;
      } else if (c == '"' || c == '\n') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
      while (at < text.size() && text[at] != '\n') text[at++] = ' ';
    }
  }
  return text;
}

std::string ReplyBody(const std::string& source, const std::string& signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) return std::string();
  const size_t end = source.find("\n}\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// The text between the last "try {" before `call` and `call` has no closing
// brace at the try's own depth: `call` sits inside that try block, and a catch
// follows it.
bool InsideTry(const std::string& body, const std::string& call) {
  const size_t at = body.find(call);
  if (at == std::string::npos) return false;
  const size_t tryAt = body.rfind("try {", at);
  if (tryAt == std::string::npos) return false;
  int depth = 0;
  for (size_t i = tryAt + 4; i < at; ++i) {
    if (body[i] == '{') ++depth;
    if (body[i] == '}' && --depth == 0) return false;
  }
  return body.find("catch (", at) != std::string::npos;
}

}  // namespace

UR_TEST(ControlReplyWiring_EveryDaemonSendEncodesInsideATry) {
  const std::string server = ReadReplySource("daemon/ControlServer.cpp");
  const std::string pump = ReplyBody(server, "bool ControlServer::PumpConnection(");
  UR_EXPECT_TRUE(!pump.empty());
  UR_EXPECT_TRUE(InsideTry(pump, "SendFrame(conn, state->reply)"));
  const std::string deferred = ReplyBody(server, "void ControlServer::DeliverDeferredReply(");
  UR_EXPECT_TRUE(!deferred.empty());
  UR_EXPECT_TRUE(InsideTry(deferred, "SendFrame(conn, reply)"));
  // Exactly the two sends above: a third would need its own try.
  size_t sends = 0;
  for (size_t at = server.find("SendFrame(conn,"); at != std::string::npos;
       at = server.find("SendFrame(conn,", at + 1)) {
    ++sends;
  }
  UR_EXPECT_EQ(2u, sends);
}

UR_TEST(ControlReplyWiring_FramesAndSdkJsonUseTheReplacingDump) {
  const std::string protocol = ReadReplySource("ControlProtocol.hpp");
  UR_EXPECT_TRUE(protocol.find("EncodeFrame(const nlohmann::json& j) { return DumpForWire(j) + ") !=
                 std::string::npos);
  UR_EXPECT_TRUE(protocol.find("error_handler_t::replace") != std::string::npos);
  // The provider statistics carry SDK documents as json text inside the frame.
  const std::string host = ReadReplySource("daemon/TunnelHost.cpp");
  UR_EXPECT_TRUE(host.find(").dump()") == std::string::npos);
}

UR_TEST(ControlReplyWiring_LogRingCutsWholeCharacters) {
  const std::string log = ReadReplySource("daemon/DaemonLog.cpp");
  const std::string append = ReplyBody(log, "void DaemonLog::AppendLocked(");
  UR_EXPECT_TRUE(!append.empty());
  UR_EXPECT_TRUE(append.find("TruncateUtf8(text, kMaxLineBytes)") != std::string::npos);
  UR_EXPECT_TRUE(append.find(".resize(") == std::string::npos);
}
