// The operator's network space is keyed bringyour.com/main, carries no
// migration host, and every launch moves a space stored under the retired
// ur.network key BEFORE the bundled space is built or any NetworkSpace is
// taken from the manager. The bootstrap is templated over the SDK's shapes
// (NetworkSpaceBootstrap.hpp), so a recording fake stands in for the manager
// here and the ORDER of the calls is what this file pins. The call sites in
// SdkHost.cpp and the daemon's TunnelHost.cpp are read as text, the way
// UpdateWiringTest reads MainWindow.cpp: a bootstrap nothing runs at launch
// would leave a v2026.9.19 install's jwt stranded under the old key.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "NetworkSpaceBootstrap.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

using urnw::BootstrapUrNetworkSpace;
using urnw::kUrEnvName;
using urnw::kUrHostName;
using urnw::kUrLegacyHostName;
using urnw::kUrLinkHostName;
using urnw::MigrateLegacyUrNetworkSpace;
using urnw::UrNetworkSpaceKey;
using urnw::UrNetworkSpaceValues;

// The SDK's NetworkSpaceKey / NetworkSpaceValues shapes (urnetwork_sdk.hpp),
// the fields the bootstrap touches.
struct Key {
  std::optional<std::string> host_name;
  std::optional<std::string> env_name;
};

struct Values {
  std::optional<bool> bundled;
  std::optional<bool> net_expose_server_ips;
  std::optional<bool> net_expose_server_host_names;
  std::optional<std::string> link_host_name;
  std::optional<std::string> migration_host_name;
  std::optional<std::string> wallet;
  std::optional<bool> sso_google;
  std::optional<std::string> api_url;
  std::optional<std::string> platform_url;
};

struct Space {
  Key key;
};

std::string Show(const std::optional<Key>& key) {
  if (!key) return "<none>";
  return key->host_name.value_or("<none>") + "/" + key->env_name.value_or("<none>");
}

// Records every call in order, the way the SDK manager would see them.
struct RecordingManager {
  bool migrateResult = false;
  std::vector<std::string> calls;
  std::optional<Values> built;

  bool migrateNetworkSpace(const std::optional<Key>& from, const std::optional<Key>& to) {
    calls.push_back("migrate " + Show(from) + " -> " + Show(to));
    return migrateResult;
  }
  Space updateNetworkSpaceValues(const std::optional<Key>& key, const std::optional<Values>& values) {
    calls.push_back("update " + Show(key));
    built = values;
    return Space{key.value_or(Key{})};
  }
};

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of the function whose definition starts with `signature`, or ""
// (braces balanced from the first '{' after the signature).
std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t at = source.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t open = source.find('{', at);
  if (open == std::string::npos) return std::string();
  int depth = 0;
  for (size_t i = open; i < source.size(); ++i) {
    if (source[i] == '{') ++depth;
    if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
  }
  return std::string();
}

size_t Position(const std::string& haystack, const char* needle) {
  return haystack.find(needle);
}

bool Has(const std::string& haystack, const char* needle) {
  return Position(haystack, needle) != std::string::npos;
}

}  // namespace

// ---- the constants ----------------------------------------------------------

UR_TEST(theOperatorStaysBringyourAndTheUrNetworkKeyIsRetired) {
  UR_EXPECT_TRUE_MSG("the official host is the operator's", std::string(kUrHostName) == "bringyour.com");
  UR_EXPECT_TRUE_MSG("the retired key is the cancelled migration's",
                     std::string(kUrLegacyHostName) == "ur.network");
  UR_EXPECT_TRUE(std::string(kUrEnvName) == "main");
  UR_EXPECT_TRUE_MSG("ur.io stays the link host (a site, not the operator)",
                     std::string(kUrLinkHostName) == "ur.io");
  UR_EXPECT_TRUE_MSG("the two keys differ, or the move is a no-op by definition",
                     std::string(kUrHostName) != std::string(kUrLegacyHostName));
}

UR_TEST(theOfficialValuesCarryNoMigrationHost) {
  const Values v = UrNetworkSpaceValues<Values>(true, kUrHostName);
  UR_EXPECT_TRUE(v.bundled.has_value() && *v.bundled);
  UR_EXPECT_TRUE(v.net_expose_server_ips.has_value() && *v.net_expose_server_ips);
  UR_EXPECT_TRUE(v.net_expose_server_host_names.has_value() && *v.net_expose_server_host_names);
  UR_EXPECT_TRUE(v.link_host_name == std::optional<std::string>("ur.io"));
  // SET and empty, not absent: updateNetworkSpaceValues must overwrite the
  // "bringyour.com" an older build persisted, not leave it in place
  UR_EXPECT_TRUE_MSG("migration host is written as empty",
                     v.migration_host_name == std::optional<std::string>(""));
  UR_EXPECT_TRUE(v.wallet == std::optional<std::string>("circle"));
  UR_EXPECT_TRUE(v.sso_google == std::optional<bool>(false));
  UR_EXPECT_TRUE_MSG("no url overrides in the bundled space", !v.api_url && !v.platform_url);
}

UR_TEST(aCustomDeploymentDerivesOffItsOwnName) {
  const Values v = UrNetworkSpaceValues<Values>(false, "example.test");
  UR_EXPECT_TRUE(v.bundled == std::optional<bool>(false));
  UR_EXPECT_TRUE(v.link_host_name == std::optional<std::string>("example.test"));
  UR_EXPECT_TRUE(v.migration_host_name == std::optional<std::string>(""));
}

// ---- the bootstrap ----------------------------------------------------------

UR_TEST(theBootstrapMovesTheLegacySpaceBeforeBuildingTheBundledOne) {
  RecordingManager manager;
  const Space space = BootstrapUrNetworkSpace<Key, Values>(manager);
  UR_EXPECT_EQ(2u, manager.calls.size());
  if (manager.calls.size() != 2) return;
  UR_EXPECT_TRUE_MSG("first call: " + manager.calls[0],
                     manager.calls[0] == "migrate ur.network/main -> bringyour.com/main");
  UR_EXPECT_TRUE_MSG("second call: " + manager.calls[1],
                     manager.calls[1] == "update bringyour.com/main");
  UR_EXPECT_TRUE(space.key.host_name == std::optional<std::string>("bringyour.com"));
  UR_EXPECT_TRUE(space.key.env_name == std::optional<std::string>("main"));
  UR_EXPECT_TRUE_MSG("the bundled space is built with the official values",
                     manager.built && manager.built->bundled == std::optional<bool>(true) &&
                         manager.built->migration_host_name == std::optional<std::string>(""));
}

UR_TEST(theBootstrapBuildsWhetherOrNotThereWasAnythingToMove) {
  // the SDK answers false when the source is absent or the destination
  // exists (every launch after the first); the build must not depend on it
  for (const bool moved : {false, true}) {
    RecordingManager manager;
    manager.migrateResult = moved;
    BootstrapUrNetworkSpace<Key, Values>(manager);
    UR_EXPECT_TRUE_MSG(std::string("moved=") + (moved ? "true" : "false"),
                       manager.calls.size() == 2 && manager.calls[1] == "update bringyour.com/main");
  }
}

UR_TEST(theLegacyMoveIsFromTheRetiredKeyToTheCurrentOne) {
  RecordingManager manager;
  manager.migrateResult = true;
  UR_EXPECT_TRUE(MigrateLegacyUrNetworkSpace<Key>(manager));
  UR_EXPECT_TRUE(manager.calls.size() == 1 &&
                 manager.calls[0] == "migrate ur.network/main -> bringyour.com/main");
  const Key key = UrNetworkSpaceKey<Key>(kUrHostName);
  UR_EXPECT_TRUE(key.host_name == std::optional<std::string>("bringyour.com"));
  UR_EXPECT_TRUE(key.env_name == std::optional<std::string>("main"));
}

// ---- the call sites ---------------------------------------------------------

UR_TEST(theGuiBootstrapsTheSpaceWhereItCreatesTheManager) {
  const std::string source = ReadSource("SdkHost.cpp");
  if (source.empty()) {
    UR_FAIL("could not read SdkHost.cpp");
    return;
  }
  const std::string init = FunctionBody(source, "bool SdkHost::Initialize(");
  UR_EXPECT_TRUE_MSG("Initialize creates the manager", Has(init, "newNetworkSpaceManager("));
  UR_EXPECT_TRUE_MSG("Initialize runs the bootstrap (the legacy move rides in it)",
                     Has(init, "BuildUrNetworkSpace(*spaceManager_)"));
  UR_EXPECT_TRUE_MSG("the bootstrap runs before anything is derived from the space",
                     Position(init, "BuildUrNetworkSpace(") < Position(init, "getApi()"));
  // the network-server switch writes the shared value set, not its own copy
  // with a migration host of its own
  const std::string apply = FunctionBody(source, "bool SdkHost::ApplyNetworkServer(");
  UR_EXPECT_TRUE_MSG("ApplyNetworkServer uses UrNetworkSpaceValues",
                     Has(apply, "UrNetworkSpaceValues(official, hostName)"));
  UR_EXPECT_TRUE_MSG("ApplyNetworkServer names no migration host of its own",
                     !Has(apply, "migration_host_name"));
}

UR_TEST(theDaemonMovesTheLegacySpaceWhereItCreatesItsManager) {
  const std::string source = ReadSource("daemon/TunnelHost.cpp");
  if (source.empty()) {
    UR_FAIL("could not read daemon/TunnelHost.cpp");
    return;
  }
  const size_t create = Position(source, "newNetworkSpaceManager(");
  const size_t migrate = Position(source, "MigrateLegacyUrNetworkSpace(*spaceManager_)");
  const size_t import = Position(source, "importNetworkSpaceFromJson(");
  const size_t build = Position(source, "BuildUrNetworkSpace(*spaceManager_)");
  UR_EXPECT_TRUE_MSG("the daemon creates its own manager", create != std::string::npos);
  UR_EXPECT_TRUE_MSG("the daemon runs the legacy move", migrate != std::string::npos);
  UR_EXPECT_TRUE_MSG("the move follows the manager's creation", create < migrate);
  // the GUI's space json materializes the current key; a move after that
  // would be a no-op and strand the daemon's local state under the old key
  UR_EXPECT_TRUE_MSG("the move precedes the import of the GUI's space", migrate < import);
  UR_EXPECT_TRUE_MSG("the move precedes the default build", migrate < build);
}

UR_TEST(noSourceNamesTheRetiredHostOutsideTheBootstrap) {
  // "ur.network" as a string literal belongs to NetworkSpaceBootstrap.hpp
  // alone; any other occurrence is a leftover of the cancelled migration
  // (a help text, a derived url, an official-host check)
  const std::filesystem::path src(UR_SRC_DIR);
  if (!std::filesystem::is_directory(src)) {
    UR_FAIL("UR_SRC_DIR is not a directory");
    return;
  }
  int scanned = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
    if (!entry.is_regular_file()) continue;
    const std::string ext = entry.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp") continue;
    if (entry.path().filename() == "NetworkSpaceBootstrap.hpp") continue;
    ++scanned;
    const std::string text = ReadSource(std::filesystem::relative(entry.path(), src).string());
    UR_EXPECT_TRUE_MSG(entry.path().filename().string() + " still names \"ur.network\"",
                       !Has(text, "\"ur.network\""));
    UR_EXPECT_TRUE_MSG(entry.path().filename().string() + " still names \"bringyour.com\" directly",
                       !Has(text, "\"bringyour.com\""));
  }
  UR_EXPECT_TRUE_MSG("the scan saw the sources", scanned > 10);
}
