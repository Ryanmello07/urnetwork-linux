// Which connect location is selected (LocationSelection.hpp), asked one way by
// the chooser, the Network page and the Connect page's provider row: no
// selection and a best-available one are both best available, a peer is
// selected by its client id, and a location by any of its ids. The pages need
// GTK and the SDK, so the wiring case reads their sources.
// SPDX-License-Identifier: MPL-2.0
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "LocationSelection.hpp"
#include "TestHarness.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

// The fields of urnet::NetworkPeer, urnet::ConnectLocationId and
// urnet::ConnectLocation the predicates read, by the generated
// urnetwork_sdk.hpp's names and types.
struct SelectionPeer {
  std::optional<std::string> ClientId;
  std::string DeviceName{};
};

struct SelectionId {
  std::optional<std::string> client_id;
  std::optional<std::string> location_id;
  std::optional<std::string> location_group_id;
  std::optional<bool> best_available;
};

struct SelectionLocation {
  std::optional<SelectionId> connect_location_id;
  std::optional<std::string> name;
};

SelectionLocation LocationWithId(const std::string& locationId) {
  SelectionLocation location;
  SelectionId id;
  id.location_id = locationId;
  location.connect_location_id = id;
  return location;
}

SelectionLocation BestAvailable() {
  SelectionLocation location;
  SelectionId id;
  id.best_available = true;
  location.connect_location_id = id;
  return location;
}

std::string ReadSelectionSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

UR_TEST(LocationSelection_NoneAndTheFlagAreBestAvailable) {
  UR_EXPECT_TRUE(urnw::IsBestAvailableSelected(std::optional<SelectionLocation>()));
  UR_EXPECT_TRUE(urnw::IsBestAvailableSelected(std::optional<SelectionLocation>(BestAvailable())));
  UR_EXPECT_FALSE(
      urnw::IsBestAvailableSelected(std::optional<SelectionLocation>(LocationWithId("jp"))));
  // a location with no id at all is not a choice of best available
  UR_EXPECT_FALSE(
      urnw::IsBestAvailableSelected(std::optional<SelectionLocation>(SelectionLocation{})));
  SelectionLocation unflagged = LocationWithId("jp");
  unflagged.connect_location_id->best_available = false;
  UR_EXPECT_FALSE(urnw::IsBestAvailableSelected(std::optional<SelectionLocation>(unflagged)));
}

UR_TEST(LocationSelection_APeerIsSelectedByItsClientId) {
  SelectionPeer peer;
  peer.ClientId = "client-a";
  SelectionLocation selected;
  SelectionId id;
  id.client_id = "client-a";
  selected.connect_location_id = id;
  UR_EXPECT_TRUE(urnw::IsPeerSelected(std::optional<SelectionLocation>(selected), peer));
  peer.ClientId = "client-b";
  UR_EXPECT_FALSE(urnw::IsPeerSelected(std::optional<SelectionLocation>(selected), peer));
  UR_EXPECT_FALSE(urnw::IsPeerSelected(std::optional<SelectionLocation>(), peer));
  // two empty ids are no match
  peer.ClientId = "";
  selected.connect_location_id->client_id = "";
  UR_EXPECT_FALSE(urnw::IsPeerSelected(std::optional<SelectionLocation>(selected), peer));
}

UR_TEST(LocationSelection_ALocationIsSelectedByAnyOfItsIds) {
  const std::optional<SelectionLocation> japan = LocationWithId("jp");
  UR_EXPECT_TRUE(urnw::IsLocationSelected(japan, LocationWithId("jp")));
  UR_EXPECT_FALSE(urnw::IsLocationSelected(japan, LocationWithId("de")));
  UR_EXPECT_FALSE(
      urnw::IsLocationSelected(std::optional<SelectionLocation>(), LocationWithId("jp")));

  SelectionLocation byClient;
  SelectionId clientId;
  clientId.client_id = "client-a";
  byClient.connect_location_id = clientId;
  UR_EXPECT_TRUE(urnw::IsLocationSelected(std::optional<SelectionLocation>(byClient), byClient));

  SelectionLocation byGroup;
  SelectionId groupId;
  groupId.location_group_id = "group-a";
  byGroup.connect_location_id = groupId;
  UR_EXPECT_TRUE(urnw::IsLocationSelected(std::optional<SelectionLocation>(byGroup), byGroup));
  UR_EXPECT_FALSE(urnw::IsLocationSelected(std::optional<SelectionLocation>(byGroup), byClient));

  // best available is no location: two of them share no id
  UR_EXPECT_FALSE(
      urnw::IsLocationSelected(std::optional<SelectionLocation>(BestAvailable()), BestAvailable()));
}

// The chooser and the Network page ask the shared predicates and keep no copy.
UR_TEST(LocationSelection_ThePagesKeepNoCopyOfThePredicates) {
  for (const char* file : {"LocationsSheet.cpp", "NetworkPage.cpp", "ConnectPage.cpp"}) {
    const std::string source = ReadSelectionSource(file);
    UR_EXPECT_TRUE(!source.empty());
    UR_EXPECT_TRUE(source.find("#include \"LocationSelection.hpp\"") != std::string::npos);
    UR_EXPECT_TRUE(source.find("bool IsBestAvailableSelected(") == std::string::npos);
    UR_EXPECT_TRUE(source.find("bool IsLocationSelected(") == std::string::npos);
    UR_EXPECT_TRUE(source.find("bool IsPeerSelected(") == std::string::npos);
  }
  // the provider row asks the predicates rather than reading the ids itself
  const std::string page = ReadSelectionSource("ConnectPage.cpp");
  const size_t row = page.find("void ConnectPage::ApplyLocationRow() {");
  UR_EXPECT_TRUE(row != std::string::npos);
  const std::string body = page.substr(row, page.find("\n}\n", row) - row);
  UR_EXPECT_TRUE(body.find("IsBestAvailableSelected(location)") != std::string::npos);
  UR_EXPECT_TRUE(body.find("IsPeerSelected(location, peer)") != std::string::npos);
  UR_EXPECT_TRUE(body.find("->best_available") == std::string::npos);
}
