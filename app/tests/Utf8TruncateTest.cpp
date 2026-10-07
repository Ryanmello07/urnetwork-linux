// TruncateUtf8 (Utf8Truncate.hpp): the daemon log ring's byte cap never leaves
// half a character at the end of a line.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>

#include "Utf8Truncate.hpp"

UR_TEST(utf8TruncateLeavesShortTextAlone) {
  std::string text = "caf\xc3\xa9";
  UR_EXPECT_FALSE(urnw::TruncateUtf8(text, 5));
  UR_EXPECT_TRUE(text == "caf\xc3\xa9");
  UR_EXPECT_FALSE(urnw::TruncateUtf8(text, 64));
  UR_EXPECT_TRUE(text == "caf\xc3\xa9");
}

UR_TEST(utf8TruncateCutsAsciiAtTheLimit) {
  std::string text = "abcdef";
  UR_EXPECT_TRUE(urnw::TruncateUtf8(text, 4));
  UR_EXPECT_TRUE(text == "abcd");
}

// A character the limit falls inside is dropped whole, wherever the limit lands
// in it, and a limit on its first byte keeps everything before it.
UR_TEST(utf8TruncateDropsACharacterTheLimitWouldSplit) {
  const std::string euro = "ab\xe2\x82\xac" "cd";  // E2 82 AC at bytes 2..4
  for (std::size_t limit : {2u, 3u, 4u}) {
    std::string text = euro;
    UR_EXPECT_TRUE(urnw::TruncateUtf8(text, limit));
    UR_EXPECT_TRUE(text == "ab");
  }
  std::string text = euro;
  UR_EXPECT_TRUE(urnw::TruncateUtf8(text, 5));
  UR_EXPECT_TRUE(text == "ab\xe2\x82\xac");

  const std::string frog = "a\xf0\x9f\x90\xb8";  // a four-byte character at 1..4
  for (std::size_t limit : {1u, 2u, 3u, 4u}) {
    std::string cut = frog;
    UR_EXPECT_TRUE(urnw::TruncateUtf8(cut, limit));
    UR_EXPECT_TRUE(cut == "a");
  }
  std::string twoByte = "\xc3\xa9\xc3\xa9";
  UR_EXPECT_TRUE(urnw::TruncateUtf8(twoByte, 3));
  UR_EXPECT_TRUE(twoByte == "\xc3\xa9");
}

UR_TEST(utf8TruncateToZeroIsEmpty) {
  std::string text = "\xe2\x82\xac";
  UR_EXPECT_TRUE(urnw::TruncateUtf8(text, 0));
  UR_EXPECT_TRUE(text.empty());
}

// Text that was not UTF-8 is cut as bytes: a run of continuation bytes longer
// than any sequence can carry does not walk the cut back to the start.
UR_TEST(utf8TruncateCutsInvalidRunsWhereAsked) {
  std::string text(10, '\x80');
  UR_EXPECT_TRUE(urnw::TruncateUtf8(text, 8));
  UR_EXPECT_EQ(8u, text.size());
}
