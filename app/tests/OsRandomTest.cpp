// The browser approval's random source (src/OsRandom.hpp): the operating
// system's, drawn whole. Two draws of a state's 32 bytes differ and are not
// zero, a draw of nothing succeeds, and the header asks getrandom(2) again when
// a signal interrupts it; it never draws from GLib's GRand or the C library.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <array>
#include <cstddef>
#include <string>

#include "OsRandom.hpp"
#include "WiringSource.hpp"

using urnw::testing::wiring::Before;
using urnw::testing::wiring::Contains;
using urnw::testing::wiring::FunctionBody;
using urnw::testing::wiring::ReadCode;

UR_TEST(OsRandom_TwoDrawsDifferAndAreNotZero) {
  std::array<unsigned char, 32> first{};
  std::array<unsigned char, 32> second{};
  UR_EXPECT_TRUE(urnw::osrandom::RandomBytes(first.data(), first.size()));
  UR_EXPECT_TRUE(urnw::osrandom::RandomBytes(second.data(), second.size()));
  UR_EXPECT_TRUE_MSG("two 32-byte draws differ", first != second);
  bool zero = true;
  for (const unsigned char byte : first) zero = zero && byte == 0;
  UR_EXPECT_TRUE_MSG("a 32-byte draw is not all zero", !zero);
  // a draw longer than one getrandom call may answer for is drawn whole
  std::array<unsigned char, 4096> large{};
  UR_EXPECT_TRUE(urnw::osrandom::RandomBytes(large.data(), large.size()));
  std::size_t zeros = 0;
  for (const unsigned char byte : large) zeros += byte == 0 ? 1 : 0;
  UR_EXPECT_TRUE_MSG("4096 bytes are drawn to the end, not left as they were", zeros < 64);
}

UR_TEST(OsRandom_ADrawOfNothingSucceeds) {
  unsigned char untouched = 0x5a;
  UR_EXPECT_TRUE(urnw::osrandom::RandomBytes(&untouched, 0));
  UR_EXPECT_EQ(0x5a, untouched);
}

UR_TEST(OsRandom_ItIsGetrandomAskedAgainAfterASignal) {
  const std::string code = ReadCode("OsRandom.hpp");
  const std::string body =
      FunctionBody(code, "inline bool RandomBytes(unsigned char* out, std::size_t length)");
  UR_EXPECT_TRUE_MSG("RandomBytes", !body.empty());
  UR_EXPECT_TRUE(Contains(body, "::getrandom(out + done, length - done, 0)"));
  UR_EXPECT_TRUE(Before(body, "::getrandom(", "if (errno == EINTR) continue;"));
  UR_EXPECT_TRUE(Contains(body, "if (n == 0) return false;"));
  UR_EXPECT_TRUE(Contains(code, "#include <sys/random.h>"));
  for (const char* other :
       {"g_random", "g_uuid", "GRand", "rand(", "mt19937", "random_device", "/dev/urandom"}) {
    UR_EXPECT_TRUE_MSG(std::string("OsRandom.hpp draws from ") + other, !Contains(code, other));
  }
}
