// The compact count and the count rate a transfer chart labels its count rows
// with: "<compact count> reads/s" on the extender chart (EXTENDER.md O8).
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <string>

#include "CountFormat.hpp"

UR_TEST(CountFormat_Rate) {
  struct Case {
    int64_t count;
    const char* expected;
  };
  const Case cases[] = {
      {340, "340 reads/s"},
      {1234, "1.2k reads/s"},
      {12345, "12k reads/s"},
      {1234567, "1.2M reads/s"},
      {0, "0 reads/s"},
  };
  for (const Case& c : cases) {
    const std::string actual = urnw::FormatCountRate(c.count, "reads/s");
    UR_EXPECT_TRUE_MSG(std::string("expected \"") + c.expected + "\", got \"" + actual + "\"",
                       actual == c.expected);
  }
}
