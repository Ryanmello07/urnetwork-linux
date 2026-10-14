// The log card's log_tail round trip (LogTailProtocol.hpp): the client sends
// the flat frame every verb uses, and the daemon reads it as that. The daemon
// read a nested "args" object the client never sent, so every log_tail threw
// out_of_range and was answered with log_access_denied, which the Developer
// page shows as "This account may drive the daemon but may not read its log."
// The call sites need glib, so they are read as source with comments blanked.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "LogTailProtocol.hpp"
#include "WiringSource.hpp"

namespace ctl = urnw::ctl;
using namespace urnw::testing::wiring;

// The frame LogTailClient builds reads back into the same request.
UR_TEST(LogTail_TheClientsFrameReadsBackAsItsRequest) {
  ctl::LogTailRequest sent;
  sent.cursor = 4711;
  sent.max_lines = 250;
  nlohmann::json frame = nlohmann::json(sent);
  frame["verb"] = ctl::kVerbLogTail;
  frame["id"] = int64_t{7};
  const ctl::LogTailRequest read = frame.get<ctl::LogTailRequest>();
  UR_EXPECT_EQ(sent.cursor, read.cursor);
  UR_EXPECT_EQ(sent.max_lines, read.max_lines);
  UR_EXPECT_TRUE_MSG("the frame carries no nested args", !frame.contains("args"));
}

// Both halves agree on that shape.
UR_TEST(LogTail_TheDaemonReadsTheFlatFrame) {
  const std::string server = ReadCode("daemon/ControlServer.cpp");
  UR_EXPECT_TRUE(Contains(server, "const auto req = request.get<ctl::LogTailRequest>();"));
  UR_EXPECT_TRUE_MSG("no verb reads a nested args object", !Contains(server, "at(\"args\")"));
  const std::string client = ReadCode("LogTailClient.cpp");
  UR_EXPECT_TRUE(Before(client, "nlohmann::json frame = nlohmann::json(request);",
                        "frame[\"verb\"] = ctl::kVerbLogTail;"));
  UR_EXPECT_TRUE_MSG("the client sends no nested args", !Contains(client, "\"args\""));
}
