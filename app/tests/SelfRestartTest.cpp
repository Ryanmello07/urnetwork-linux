// The daemon's self-restart after the SDK stopped answering (SelfRestart.hpp):
// the stop record one daemon leaves the next, and the exit status the unit
// restarts on.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "daemon/SelfRestart.hpp"

namespace sr = urnw::selfrestart;

namespace {

sr::StopRecord Record(int64_t writtenMillis) {
  sr::StopRecord record;
  record.stopReason = "failsafe_sdk_unresponsive";
  record.error = "The connection was stopped because it carried nothing.";
  record.code = "tunnel_dead";
  record.writtenMillis = writtenMillis;
  return record;
}

}  // namespace

// What the stopping daemon's status said is what the next one reads back.
UR_TEST(SelfRestart_TheStopRecordRoundTrips) {
  const sr::StopRecord written = Record(500000);
  const auto read = sr::DecodeStopRecord(sr::EncodeStopRecord(written), 502500);
  UR_EXPECT_TRUE(read.has_value());
  UR_EXPECT_TRUE(read && read->stopReason == written.stopReason);
  UR_EXPECT_TRUE(read && read->error == written.error);
  UR_EXPECT_TRUE(read && read->code == written.code);
  UR_EXPECT_EQ(500000, read ? read->writtenMillis : 0);
}

// The sentence can carry nft's output: a byte that is not UTF-8 must not cost
// the record, nor end the daemon that is writing it.
UR_TEST(SelfRestart_ABadByteInTheSentenceIsReplaced) {
  sr::StopRecord written = Record(1000);
  written.error = "could not be armed (nft: \xe2\x82)";
  const auto read = sr::DecodeStopRecord(sr::EncodeStopRecord(written), 2000);
  UR_EXPECT_TRUE(read.has_value());
  UR_EXPECT_TRUE(read && read->error.find("\xef\xbf\xbd") != std::string::npos);
}

// A record is about the stop just before this start: one older than the
// window, or from a later clock reading, is some other stop.
UR_TEST(SelfRestart_AStaleRecordIsDropped) {
  const std::string text = sr::EncodeStopRecord(Record(100000));
  UR_EXPECT_TRUE(sr::DecodeStopRecord(text, 100000 + sr::kStopRecordMaxAgeMillis).has_value());
  UR_EXPECT_FALSE(sr::DecodeStopRecord(text, 100000 + sr::kStopRecordMaxAgeMillis + 1).has_value());
  UR_EXPECT_FALSE(sr::DecodeStopRecord(text, 99999).has_value());
}

UR_TEST(SelfRestart_ABrokenRecordIsDropped) {
  UR_EXPECT_FALSE(sr::DecodeStopRecord("", 1000).has_value());
  UR_EXPECT_FALSE(sr::DecodeStopRecord("{\"stop_reason\":", 1000).has_value());
  UR_EXPECT_FALSE(sr::DecodeStopRecord("[]", 1000).has_value());
  // A record that names no stop.
  sr::StopRecord nameless = Record(1000);
  nameless.stopReason.clear();
  UR_EXPECT_FALSE(sr::DecodeStopRecord(sr::EncodeStopRecord(nameless), 1000).has_value());
  UR_EXPECT_FALSE(sr::DecodeStopRecord(
                      "{\"stop_reason\":\"x\",\"error\":\"\",\"error_code\":\"\","
                      "\"written_millis\":\"1000\"}",
                      1000)
                      .has_value());
  UR_EXPECT_FALSE(
      sr::DecodeStopRecord("{\"stop_reason\":\"x\",\"error\":1,\"error_code\":\"\","
                           "\"written_millis\":1000}",
                           1000)
          .has_value());
}

// Restart=on-failure restarts on any status but 0 and those the unit names
// (SelfRestartWiringTest reads that it names none); 1 is every other failed
// start, so this exit is told apart in the journal.
UR_TEST(SelfRestart_TheExitStatusIsItsOwn) {
  UR_EXPECT_EQ(75, sr::kExitStatus);
  UR_EXPECT_TRUE(std::string(sr::kStopRecordPath).rfind("/run/urnetwork/", 0) == 0);
  UR_EXPECT_TRUE(sr::kStopRecordMaxAgeMillis >= 60000);
}
