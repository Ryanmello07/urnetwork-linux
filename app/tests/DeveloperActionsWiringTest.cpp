// The Developer page's action buttons are one role, a flat button wearing the
// accent as its text: Refresh no longer wears the lime fill kept for
// earnings and brand accents. The page needs GTK and the SDK, so this reads
// its source.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadDeveloperSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

UR_TEST(DeveloperActionsWiring_EveryActionWearsOneRole) {
  const std::string page = ReadDeveloperSource("DeveloperPage.cpp");
  UR_EXPECT_TRUE(!page.empty());
  UR_EXPECT_TRUE(page.find("Gtk::Button* MakeActionButton(const Glib::ustring& text) {") !=
                 std::string::npos);
  UR_EXPECT_TRUE(page.find("suggested-action") == std::string::npos);
  UR_EXPECT_TRUE(page.find("auto* refresh = MakeActionButton(T_(\"dev_refresh\", \"Refresh\"));") !=
                 std::string::npos);
}
