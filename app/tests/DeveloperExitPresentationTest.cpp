// The developer page's exit policy part (DeveloperExitPresentation.hpp): the
// built-in security rules generation of each exit's provider, which the exit's
// State cell shows once the provider's diagnostics arrive: the number,
// "unknown" for a provider that reports its policy without one, and nothing
// before its first diagnostics. The sdk carried no generation before, so no
// readout could show which exits run older rules.
//
// DeveloperPage needs GTK and the SDK, so its half is read from its source,
// the way ProviderStatusWiringTest reads SdkHost and EarningsPage: the State
// cell takes the part for every exit, after proven, with the store's two
// strings, and each English fallback is the catalog's msgid.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "DeveloperExitPresentation.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

using urnw::developerexit::PolicyGeneration;
using urnw::developerexit::PolicyGenerationOf;

namespace {

// urnet::Exit, reduced to the fields the decision reads
struct MirrorExit {
  bool ProviderDiagnosticsAvailable = false;
  int64_t ProviderSecurityPolicyGeneration = 0;
};

MirrorExit Exit(bool providerDiagnosticsAvailable, int64_t providerSecurityPolicyGeneration) {
  MirrorExit exit;
  exit.ProviderDiagnosticsAvailable = providerDiagnosticsAvailable;
  exit.ProviderSecurityPolicyGeneration = providerSecurityPolicyGeneration;
  return exit;
}

std::string ReadDeveloperExitSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

UR_TEST(DeveloperExit_NoPolicyPartBeforeTheProvidersFirstDiagnostics) {
  for (const int64_t generation : {int64_t{0}, int64_t{2}}) {
    UR_EXPECT_TRUE_MSG("generation " + std::to_string(generation),
                       !PolicyGenerationOf(Exit(false, generation)).has_value());
  }
}

UR_TEST(DeveloperExit_TheReportedGenerationIsShown) {
  for (const int64_t generation :
       {int64_t{1}, int64_t{2}, std::numeric_limits<int64_t>::max()}) {
    const std::optional<PolicyGeneration> policy = PolicyGenerationOf(Exit(true, generation));
    UR_EXPECT_TRUE_MSG("generation " + std::to_string(generation),
                       policy && policy->generation && *policy->generation == generation);
  }
}

// 0 is connect's unknown; a negative value never comes from the sdk and reads
// the same
UR_TEST(DeveloperExit_AProviderWithoutAGenerationReadsUnknown) {
  for (const int64_t generation : {int64_t{0}, int64_t{-1}}) {
    const std::optional<PolicyGeneration> policy = PolicyGenerationOf(Exit(true, generation));
    UR_EXPECT_TRUE_MSG("generation " + std::to_string(generation),
                       policy && !policy->generation);
  }
}

UR_TEST(DeveloperExit_TheStateCellShowsThePolicyPart) {
  const std::string page = ReadDeveloperExitSource("DeveloperPage.cpp");
  UR_EXPECT_TRUE(!page.empty());
  for (const std::string want : {
           std::string("#include \"DeveloperExitPresentation.hpp\""),
           std::string("if (const auto policy = developerexit::PolicyGenerationOf(exit)) {"),
           std::string("Format(T_(\"dev_exit_policy_generation\", \"policy generation {}\"),"),
           std::string("*policy->generation))"),
           std::string("T_(\"dev_exit_policy_generation_unknown\","),
           std::string("\"policy generation unknown\")"),
       }) {
    UR_EXPECT_TRUE_MSG("DeveloperPage.cpp is missing " + want, page.find(want) != std::string::npos);
  }
  // the part joins the State cell's parts after proven, before the cell is written
  const size_t proven = page.find("if (exit.Proven) addPart(T_(\"dev_state_proven\", \"proven\"));");
  const size_t policy = page.find("developerexit::PolicyGenerationOf(exit)");
  const size_t written = page.find("cells[5]->set_tooltip_text(state);");
  UR_EXPECT_TRUE(proven != std::string::npos && policy != std::string::npos &&
                 written != std::string::npos);
  UR_EXPECT_TRUE(proven < policy && policy < written);
}

// The English the page falls back to is the catalog's msgid, so every locale
// finds its translation.
UR_TEST(DeveloperExit_ThePolicyStringsAreInTheCatalog) {
  const std::string catalog = ReadDeveloperExitSource("../po/urnetwork.pot");
  UR_EXPECT_TRUE(catalog.find("msgctxt \"dev_exit_policy_generation\"\nmsgid \"policy generation {}\"") !=
                 std::string::npos);
  UR_EXPECT_TRUE(catalog.find("msgctxt \"dev_exit_policy_generation_unknown\"\nmsgid \"policy generation unknown\"") !=
                 std::string::npos);
  const std::string german = ReadDeveloperExitSource("../po/de.po");
  UR_EXPECT_TRUE(german.find("msgctxt \"dev_exit_policy_generation\"\nmsgid \"policy generation {}\"\nmsgstr \"Richtliniengeneration {}\"") !=
                 std::string::npos);
}
