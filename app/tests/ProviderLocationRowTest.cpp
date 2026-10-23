// Label composition, override-target selection and "Stay on this exit" -- the
// pure logic behind the provider-locations list and the location override.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ProviderLocationRow.hpp"

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

using urnw::ProviderLocationRow;

namespace {

ProviderLocationRow Row(const std::string& clientId, const std::string& city,
                        const std::string& region, const std::string& country,
                        bool hasCoordinates = true, double lat = 0, double lon = 0,
                        int64_t connectedSinceMillis = 0) {
  ProviderLocationRow row;
  row.clientId = clientId;
  row.city = city;
  row.region = region;
  row.country = country;
  row.hasLocation = !city.empty() || !region.empty() || !country.empty();
  row.hasCoordinates = hasCoordinates;
  row.lat = lat;
  row.lon = lon;
  row.connectedSinceMillis = connectedSinceMillis;
  return row;
}

}  // namespace

UR_TEST(placeLabelJoinsTheKnownPartsCityFirst) {
  UR_EXPECT_TRUE(urnw::PlaceLabel(Row("a", "Tokyo", "Tokyo", "Japan")) == "Tokyo, Tokyo, Japan");
  // whichever parts the server does not know are simply omitted
  UR_EXPECT_TRUE(urnw::PlaceLabel(Row("a", "", "California", "United States")) ==
                 "California, United States");
  UR_EXPECT_TRUE(urnw::PlaceLabel(Row("a", "Paris", "", "France")) == "Paris, France");
  UR_EXPECT_TRUE(urnw::PlaceLabel(Row("a", "", "", "Brazil")) == "Brazil");
}

UR_TEST(placeLabelIsEmptyWhenNothingIsKnown) {
  // the caller substitutes the localized "Location unknown"
  UR_EXPECT_TRUE(urnw::PlaceLabel(Row("a", "", "", "")).empty());
}

UR_TEST(coordinatesLabelUses4DecimalPlaces) {
  UR_EXPECT_TRUE(urnw::CoordinatesLabel(Row("a", "", "", "", true, 37.7749295, -122.4194155)) ==
                 "37.7749, -122.4194");
  // a legitimate zero coordinate still renders as a number, not an em dash
  UR_EXPECT_TRUE(urnw::CoordinatesLabel(Row("a", "", "", "", true, 0, 0)) == "0.0000, 0.0000");
}

UR_TEST(coordinatesLabelIsAnEmDashWithoutCoordinates) {
  UR_EXPECT_TRUE(urnw::CoordinatesLabel(Row("a", "Somewhere", "", "", false)) == "\xE2\x80\x94");
}

UR_TEST(oldestPlottableSkipsProvidersWithNoCoordinates) {
  const std::vector<ProviderLocationRow> rows{
      Row("no-coords", "", "", "", false, 0, 0, 1000),
      Row("also-none", "Nowhere", "", "", false, 0, 0, 2000),
      Row("target", "Tokyo", "", "Japan", true, 35.6762, 139.6503, 3000),
      Row("younger", "Paris", "", "France", true, 48.8566, 2.3522, 4000),
  };
  const int index = urnw::OldestPlottableIndex(rows);
  UR_EXPECT_EQ(2, index);
  UR_EXPECT_TRUE(rows[static_cast<size_t>(index)].clientId == "target");
}

// The rows arrive in DISPLAY order (west to east), which says nothing about how
// long anything has been connected, so the target is found by stamp rather than
// by position -- the oldest provider can be anywhere in the list.
UR_TEST(oldestPlottableIsFoundByStampNotByPosition) {
  const std::vector<ProviderLocationRow> rows{
      Row("west-but-newest", "Los Angeles", "", "United States", true, 34.05, -118.24, 9000),
      Row("target", "Tokyo", "", "Japan", true, 35.6762, 139.6503, 1000),
      Row("east-and-middling", "Sydney", "", "Australia", true, -33.87, 151.21, 5000),
  };
  const int index = urnw::OldestPlottableIndex(rows);
  UR_EXPECT_EQ(1, index);
  UR_EXPECT_TRUE(rows[static_cast<size_t>(index)].clientId == "target");
}

// An unknown stamp (0, an older device peer) sorts LAST in the sdk, so it only
// wins when nothing else can be plotted.
UR_TEST(oldestPlottablePrefersAKnownStampOverAnUnknownOne) {
  const std::vector<ProviderLocationRow> mixed{
      Row("unknown-stamp", "Tokyo", "", "Japan", true, 35.6762, 139.6503, 0),
      Row("target", "Paris", "", "France", true, 48.8566, 2.3522, 7000),
  };
  UR_EXPECT_EQ(1, urnw::OldestPlottableIndex(mixed));

  // with only unknown stamps the first plottable row is as good as any
  const std::vector<ProviderLocationRow> allUnknown{
      Row("no-coords", "", "", "", false, 0, 0, 0),
      Row("first", "Tokyo", "", "Japan", true, 35.6762, 139.6503, 0),
      Row("second", "Paris", "", "France", true, 48.8566, 2.3522, 0),
  };
  UR_EXPECT_EQ(1, urnw::OldestPlottableIndex(allUnknown));
}

// The address-family tag is exactly one of the SDK's three labels; an older
// peer that sends none, or a label this build cannot name, reads as v4 -- what
// such a provider carries -- never as nothing.
UR_TEST(ipFamilyTagIsOneOfTheThreeLabelsAndDefaultsToV4) {
  ProviderLocationRow row = Row("a", "Tokyo", "", "Japan");
  row.ipFamilyLabel = "both";
  UR_EXPECT_TRUE(urnw::IpFamilyTag(row) == "both");
  row.ipFamilyLabel = "v6";
  UR_EXPECT_TRUE(urnw::IpFamilyTag(row) == "v6");
  row.ipFamilyLabel = "v4";
  UR_EXPECT_TRUE(urnw::IpFamilyTag(row) == "v4");
  row.ipFamilyLabel = "";
  UR_EXPECT_TRUE(urnw::IpFamilyTag(row) == "v4");
  row.ipFamilyLabel = "dualstack";  // the category value, not the label: unknown here
  UR_EXPECT_TRUE(urnw::IpFamilyTag(row) == "v4");
}

// A category change on a live provider (the SDK downgrades an exit that lost
// v6) must read as a changed row, or the list would keep the stale tag.
UR_TEST(ipFamilyLabelParticipatesInRowEquality) {
  ProviderLocationRow a = Row("a", "Tokyo", "", "Japan");
  ProviderLocationRow b = a;
  UR_EXPECT_TRUE(a == b);
  b.ipFamilyLabel = "both";
  UR_EXPECT_TRUE(a != b);
  a.ipFamilyLabel = "both";
  UR_EXPECT_TRUE(a == b);
}

UR_TEST(oldestPlottableReportsNoneWhenNothingIsLocated) {
  UR_EXPECT_EQ(-1, urnw::OldestPlottableIndex(std::vector<ProviderLocationRow>{}));
  const std::vector<ProviderLocationRow> unlocated{Row("a", "", "", "", false)};
  UR_EXPECT_EQ(-1, urnw::OldestPlottableIndex(unlocated));
}

// The list's display order and the globe's clamped stepping over it are the
// SDK's ProviderLocationsViewController (provider_locations_view_controller.go,
// tested there), so there is nothing to order here.

// ---- "Stay on this exit": which rows offer it, and what it connects to -------

namespace {

const std::string kClientId = "018f2b6e-3c4d-7a8b-9c0d-1e2f3a4b5c6d";
const std::string kOtherClientId = "0192aaaa-bbbb-7ccc-8ddd-eeeeffff0001";
// "018f…5c6d": the ellipsis and the middle dot are utf-8
const std::string kShortId = "018f\xE2\x80\xA6" "5c6d";
const std::string kDot = " \xC2\xB7 ";

std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string ReadProviderSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

UR_TEST(shortClientIdKeepsTheFirstAndLastFour) {
  UR_EXPECT_TRUE(urnw::ShortClientId(kClientId) == kShortId);
  UR_EXPECT_TRUE(urnw::ShortClientId(" " + kClientId + " ") == kShortId);
  UR_EXPECT_TRUE(urnw::ShortClientId("abcd1234") == "abcd1234");
}

UR_TEST(stayOnExitNameIsTheShortIdThenCityAndCountry) {
  const std::string name = urnw::StayOnExitName(Row(kClientId, "Berlin", "Land Berlin", "Germany"));
  UR_EXPECT_TRUE_MSG(name, name == kShortId + kDot + "Berlin, Germany");
}

UR_TEST(stayOnExitNameUsesTheRegionWhenTheCityIsUnknown) {
  const std::string region = urnw::StayOnExitName(Row(kClientId, "", "California", "United States"));
  UR_EXPECT_TRUE_MSG(region, region == kShortId + kDot + "California, United States");
  const std::string country = urnw::StayOnExitName(Row(kClientId, "", "", "Iceland"));
  UR_EXPECT_TRUE_MSG(country, country == kShortId + kDot + "Iceland");
}

UR_TEST(stayOnExitNameIsTheShortIdWhenTheLocationIsUnknown) {
  const std::string name = urnw::StayOnExitName(Row(kClientId, "", "", ""));
  UR_EXPECT_TRUE_MSG(name, name == kShortId);
}

UR_TEST(stayOnExitTargetIsTheProviderClientIdWithItsLocation) {
  ProviderLocationRow row = Row(kClientId, "Osaka", "Osaka", "Japan");
  row.countryCode = "jp";
  const std::optional<urnw::StayOnExitTarget> target = urnw::MakeStayOnExitTarget(row);
  UR_EXPECT_TRUE(target.has_value());
  if (!target) return;
  urnw::StayOnExitTarget expected;
  expected.clientId = kClientId;
  expected.name = kShortId + kDot + "Osaka, Japan";
  expected.city = "Osaka";
  expected.region = "Osaka";
  expected.country = "Japan";
  expected.countryCode = "jp";
  UR_EXPECT_TRUE_MSG(target->name, *target == expected);
}

UR_TEST(noStayOnExitTargetWithoutAClientId) {
  UR_EXPECT_FALSE(urnw::MakeStayOnExitTarget(Row("", "Osaka", "", "Japan")).has_value());
  UR_EXPECT_FALSE(urnw::MakeStayOnExitTarget(Row("  ", "Osaka", "", "Japan")).has_value());
}

UR_TEST(onlyTheSelectedRowOffersToStay) {
  using urnw::StayOnExitState;
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kClientId, "", "", ""), kClientId, "") ==
                 StayOnExitState::Offer);
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kOtherClientId, "", "", ""), kClientId, "") ==
                 StayOnExitState::None);
  // nothing selected (no providers) offers nothing
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kClientId, "", "", ""), "", "") ==
                 StayOnExitState::None);
}

UR_TEST(theProviderAlreadyStayedOnSaysSoInsteadOfOffering) {
  using urnw::StayOnExitState;
  // selected or not, the stayed provider never offers itself again
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kClientId, "", "", ""), kClientId, kClientId) ==
                 StayOnExitState::Staying);
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kClientId, "", "", ""), kOtherClientId,
                                          kClientId) == StayOnExitState::Staying);
  // another selected provider can still be stayed on instead
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kOtherClientId, "", "", ""), kOtherClientId,
                                          kClientId) == StayOnExitState::Offer);
}

UR_TEST(stayOnExitClientIdsMatchIgnoringCase) {
  using urnw::StayOnExitState;
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kClientId, "", "", ""), "", Upper(kClientId)) ==
                 StayOnExitState::Staying);
  UR_EXPECT_TRUE(urnw::StayOnExitStateFor(Row(kClientId, "", "", ""), Upper(kClientId), "") ==
                 StayOnExitState::Offer);
}

// The sheet needs GTK and the SDK, so this reads its source: the row's offer
// follows StayOnExitStateFor, and the action connects to the one provider as a
// public exit through the gated SdkHost::Connect.
UR_TEST(providerLocationsSheetStaysOnTheExitThroughTheGatedConnect) {
  const std::string source = ReadProviderSource("ProviderLocationsSheet.cpp");
  UR_EXPECT_TRUE(!source.empty());
  UR_EXPECT_TRUE(source.find("StayOnExitStateFor(rows_[i], selectedClientId_, stayingClientId_)") !=
                 std::string::npos);
  const size_t stay = source.find("void ProviderLocationsSheet::StayOnExit(");
  UR_EXPECT_TRUE(stay != std::string::npos);
  if (stay == std::string::npos) return;
  const std::string body = source.substr(stay, source.find("\n}\n", stay) - stay);
  UR_EXPECT_TRUE(body.find("MakeStayOnExitTarget(row)") != std::string::npos);
  UR_EXPECT_TRUE(body.find("id.client_id = target->clientId;") != std::string::npos);
  UR_EXPECT_TRUE(body.find("location.network_peer = false;") != std::string::npos);
  UR_EXPECT_TRUE(body.find("host_.Connect(location);") != std::string::npos);
}

// ---- extenders: the SDK's pairs, the row's value, the row dot's rings -------
// connect/EXTENDER.md K9, apple's ProviderExtenderTests case for case.

namespace {

using urnw::ProviderExtender;
namespace extender = urnw::extender;

const ProviderExtender kFirstExtender{"203.0.113.7", "AABBCC"};
const ProviderExtender kSecondExtender{"2001:db8::5", "DDEEFF"};
const ProviderExtender kThirdExtender{"198.51.100.2", "112233"};
const ProviderExtender kFourthExtender{"192.0.2.9", "445566"};

ProviderLocationRow ExtendedRow(const std::vector<ProviderExtender>& extenders) {
  ProviderLocationRow row = Row(kClientId, "Osaka", "", "Japan");
  row.extenders = extenders;
  return row;
}

}  // namespace

// The SDK's two comma-separated lists pair by index, in its order, and each
// color is the SDK's string as given (K3): nothing here computes one.
UR_TEST(providerExtendersPairTheSdkListsByIndex) {
  const std::vector<ProviderExtender> extenders =
      urnw::PairProviderExtenders("203.0.113.7,2001:db8::5", "AABBCC,DDEEFF");
  UR_EXPECT_TRUE(extenders == std::vector<ProviderExtender>({kFirstExtender, kSecondExtender}));
  UR_EXPECT_TRUE(urnw::ExtenderColorHexes(ExtendedRow(extenders)) ==
                 std::vector<std::string>({"AABBCC", "DDEEFF"}));
}

// A direct or p2p route carries no extenders: no pairs and a bare dot.
UR_TEST(aDirectRouteHasNoExtenders) {
  UR_EXPECT_TRUE(urnw::PairProviderExtenders("", "").empty());
  const extender::Rings rings = urnw::RowDotRings(ExtendedRow({}));
  UR_EXPECT_TRUE(rings.empty());
  UR_EXPECT_NEAR(urnw::kRowDotDiameter / 2.0, rings.dotRadius, 1e-9);
}

// The SDK keeps the two lists the same length; an unpaired tail on either side
// is dropped rather than drawn in a made-up color.
UR_TEST(anUnpairedExtenderTailIsDropped) {
  UR_EXPECT_TRUE(urnw::PairProviderExtenders("203.0.113.7,198.51.100.2", "AABBCC") ==
                 std::vector<ProviderExtender>({kFirstExtender}));
  UR_EXPECT_TRUE(urnw::PairProviderExtenders("203.0.113.7", "AABBCC,DDEEFF") ==
                 std::vector<ProviderExtender>({kFirstExtender}));
}

// A transport migration changes only the extenders, so they are part of the
// row's value: an equal row would not be re-rendered.
UR_TEST(extendersParticipateInRowEquality) {
  const ProviderLocationRow extended = ExtendedRow({kFirstExtender});
  UR_EXPECT_TRUE(ExtendedRow({}) != extended);
  UR_EXPECT_TRUE(ExtendedRow({kFirstExtender, kSecondExtender}) != extended);
  UR_EXPECT_TRUE(ExtendedRow({kSecondExtender}) != extended);
  UR_EXPECT_TRUE(ExtendedRow({kFirstExtender}) == extended);
}

// The row dot (20 px) has room for two rings at the connect grid's fixed
// geometry (K2): the outermost ring's outer edge is the dot's edge and each
// ring costs the fill 4 px of radius. A third extender dashes the second ring
// instead of eating the dot, as a fourth does.
UR_TEST(theRowDotHoldsTwoRings) {
  const double edge = urnw::kRowDotDiameter / 2.0;
  UR_EXPECT_NEAR(10.0, edge, 1e-9);

  const extender::Rings one = urnw::RowDotRings(ExtendedRow({kFirstExtender}));
  UR_EXPECT_EQ(1, static_cast<int>(one.rings.size()));
  if (one.rings.size() != 1) return;
  UR_EXPECT_FALSE(one.rings[0].dashed);
  UR_EXPECT_TRUE(one.rings[0].colorHex == kFirstExtender.colorHex);
  UR_EXPECT_NEAR(extender::kRingStroke, one.rings[0].lineWidth, 1e-9);
  UR_EXPECT_NEAR(edge, one.rings[0].radius + one.rings[0].lineWidth / 2.0, 1e-9);
  UR_EXPECT_NEAR(edge - extender::kRingPitch, one.dotRadius, 1e-9);

  const extender::Rings two = urnw::RowDotRings(ExtendedRow({kFirstExtender, kSecondExtender}));
  UR_EXPECT_EQ(2, static_cast<int>(two.rings.size()));
  if (two.rings.size() != 2) return;
  UR_EXPECT_FALSE(two.rings[0].dashed);
  UR_EXPECT_FALSE(two.rings[1].dashed);
  UR_EXPECT_TRUE(two.rings[1].colorHex == kSecondExtender.colorHex);
  UR_EXPECT_NEAR(edge - 2 * extender::kRingPitch, two.dotRadius, 1e-9);

  for (const auto& extenders :
       {std::vector<ProviderExtender>{kFirstExtender, kSecondExtender, kThirdExtender},
        std::vector<ProviderExtender>{kFirstExtender, kSecondExtender, kThirdExtender,
                                      kFourthExtender}}) {
    const extender::Rings more = urnw::RowDotRings(ExtendedRow(extenders));
    UR_EXPECT_EQ(2, static_cast<int>(more.rings.size()));
    if (more.rings.size() != 2) continue;
    UR_EXPECT_FALSE(more.rings[0].dashed);
    UR_EXPECT_TRUE(more.rings[1].dashed);
    UR_EXPECT_TRUE(more.rings[1].colorHex == kSecondExtender.colorHex);
    UR_EXPECT_EQ(static_cast<int>(extenders.size()) - 2, more.collapsedCount);
    // the fill still has a visible core
    UR_EXPECT_NEAR(edge - 2 * extender::kRingPitch, more.dotRadius, 1e-9);
    UR_EXPECT_TRUE(more.dotRadius > 0);
  }
}

// The sheet needs GTK and the SDK, so this reads its sources: the SDK's lists
// are paired into the rows, the row dot and the globe's dots wear their rings,
// and each extender's line sits under the connected time and before the "Stay
// on this exit" offer, copying its address with the store's confirmation.
UR_TEST(providerLocationsSheetListsTheExtendersUnderTheConnectedTime) {
  const std::string source = ReadProviderSource("ProviderLocationsSheet.cpp");
  UR_EXPECT_TRUE(!source.empty());
  UR_EXPECT_TRUE(source.find("row.extenders = PairProviderExtenders(location.ExtenderIps, "
                             "location.ExtenderColorHexes);") != std::string::npos);
  UR_EXPECT_TRUE(source.find("dot->SetRings(RowDotRings(row));") != std::string::npos);
  UR_EXPECT_TRUE(source.find("DrawProviderDot(cr, cx, cy, color_, rings_);") != std::string::npos);

  const size_t duration = source.find("column->append(*out.duration);");
  const size_t lines = source.find("for (const ProviderExtender& onRoute : row.extenders) {");
  const size_t stay = source.find("column->append(*out.stayOffer);");
  UR_EXPECT_TRUE(duration != std::string::npos && lines != std::string::npos &&
                 stay != std::string::npos);
  UR_EXPECT_TRUE(duration < lines && lines < stay);
  // the address takes the client id's faint color off the selected row
  UR_EXPECT_TRUE(source.find("for (Gtk::Label* address : rowWidgets_[i].extenderAddresses) {") !=
                 std::string::npos);

  const size_t copy = source.find("void ProviderLocationsSheet::CopyExtenderAddress(");
  UR_EXPECT_TRUE(copy != std::string::npos);
  if (copy == std::string::npos) return;
  const std::string body = source.substr(copy, source.find("\n}\n", copy) - copy);
  UR_EXPECT_TRUE(body.find("get_clipboard()->set_text(ip);") != std::string::npos);
  UR_EXPECT_TRUE(body.find("T_(\"extender_address_copied\", \"Extender address copied\")") !=
                 std::string::npos);

  const std::string globe = ReadProviderSource("ProviderGlobe.cpp");
  UR_EXPECT_TRUE(globe.find("globe::DotRings(ExtenderColorHexes(row), unit)") != std::string::npos);
  UR_EXPECT_TRUE(globe.find("DrawProviderDot(cr, center.x, center.y, color, rings);") !=
                 std::string::npos);
  UR_EXPECT_TRUE(globe.find("DrawProviderDot(cr, selectedCenter.x, selectedCenter.y, "
                            "DarkenForSelection(selectedColor),") != std::string::npos);
}
