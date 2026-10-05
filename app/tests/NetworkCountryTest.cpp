// The network country's rules (P052): the operator code to country table, the
// IPv6 default route, and the reading — a country only while the default route
// leaves through a registered modem, the way Android reports
// TelephonyManager.networkCountryIso only on cellular.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <string>
#include <vector>

#include "NetworkCountry.hpp"

namespace {

urnw::ModemReading Modem(std::vector<std::string> dataInterfaces, std::string operatorCode) {
  urnw::ModemReading modem;
  modem.data_interfaces = std::move(dataInterfaces);
  modem.operator_code = std::move(operatorCode);
  return modem;
}

}  // namespace

UR_TEST(networkCountryMapsTheOperatorCodesMcc) {
  // MCCMNC with a two- and a three-digit MNC; the MNC never decides
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("25001") == "ru");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("250020") == "ru");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("31026") == "us");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("310260") == "us");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("23410") == "gb");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("26201") == "de");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("46000") == "cn");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("43211") == "ir");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("25501") == "ua");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("25701") == "by");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("43801") == "tm");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("40101") == "kz");
  // the first and the last code of the table
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("20201") == "gr");
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("75001") == "fk");
}

UR_TEST(networkCountryRefusesWhatIsNotAnOperatorCode) {
  for (const char* code : {"", "2500", "2500123", "25o01", " 25001", "25001 ", "-2500",
                           "+25001"}) {
    UR_EXPECT_TRUE_MSG(code, urnw::CountryCodeForOperatorCode(code).empty());
  }
  // well-formed, but not a country: the test network and the international codes
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("00101").empty());
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("90101").empty());
  UR_EXPECT_TRUE(urnw::CountryCodeForOperatorCode("99999").empty());
  UR_EXPECT_TRUE(urnw::CountryCodeForMcc(0).empty());
  UR_EXPECT_TRUE(urnw::CountryCodeForMcc(251).empty());  // unassigned, between ru and ua
}

// The binary search needs the table ascending, and every code is two lower case
// letters (the sdk clears anything else).
UR_TEST(networkCountryTableIsAscendingAndWellFormed) {
  const size_t count = sizeof(urnw::kMccCountries) / sizeof(urnw::kMccCountries[0]);
  UR_EXPECT_TRUE(count > 200);
  for (size_t i = 0; i < count; ++i) {
    const urnw::MccCountry& entry = urnw::kMccCountries[i];
    const std::string label = std::to_string(entry.mcc);
    UR_EXPECT_TRUE_MSG(label, 200 <= entry.mcc && entry.mcc <= 799);
    if (i > 0) UR_EXPECT_TRUE_MSG(label, urnw::kMccCountries[i - 1].mcc < entry.mcc);
    const std::string country = entry.country;
    UR_EXPECT_TRUE_MSG(label, country.size() == 2 && 'a' <= country[0] && country[0] <= 'z' &&
                                  'a' <= country[1] && country[1] <= 'z');
    UR_EXPECT_TRUE_MSG(label, urnw::CountryCodeForMcc(entry.mcc) == country);
  }
}

UR_TEST(networkCountryReadsTheModemThatCarriesTheDefaultRoute) {
  const urnw::NetworkCountryReading reading =
      urnw::ReadNetworkCountry("wwan0", true, true, {Modem({"wwan0"}, "25001")});
  UR_EXPECT_TRUE(reading.country_code == "ru");
  UR_EXPECT_TRUE(reading.source == urnw::NetworkCountrySource::Modem);
  UR_EXPECT_TRUE(reading.interface_name == "wwan0");
}

// Android parity: a registered modem that does not carry the default route
// (Wi-Fi or ethernet does) reports nothing, exactly as networkCountryIso is not
// reported on Wi-Fi. So does a tethered phone, which is no modem at all here.
UR_TEST(networkCountryIsNoneOffTheModem) {
  const std::vector<urnw::ModemReading> modems = {Modem({"wwan0"}, "25001")};
  // Wi-Fi, ethernet, and a phone tethered over USB (rndis, cdc_ncm)
  for (const char* iface : {"wlp2s0", "enp0s31f6", "usb0", "enp0s20f0u2"}) {
    const urnw::NetworkCountryReading reading = urnw::ReadNetworkCountry(iface, true, true, modems);
    UR_EXPECT_TRUE_MSG(iface, reading.country_code.empty());
    UR_EXPECT_TRUE_MSG(iface, reading.source == urnw::NetworkCountrySource::NotCellular);
    UR_EXPECT_TRUE_MSG(iface, reading.interface_name == iface);
  }
}

// A multiplexed bearer's data leaves through a link of its own, which only the
// bearer names; the modem's network port is the base interface.
UR_TEST(networkCountryFollowsABearerInterface) {
  const urnw::NetworkCountryReading reading = urnw::ReadNetworkCountry(
      "qmapmux0.0", true, true, {Modem({"wwan0", "qmapmux0.0"}, "25099")});
  UR_EXPECT_TRUE(reading.country_code == "ru");
  UR_EXPECT_TRUE(reading.source == urnw::NetworkCountrySource::Modem);
}

UR_TEST(networkCountryTakesTheModemOnTheRouteOfTwo) {
  const std::vector<urnw::ModemReading> modems = {Modem({"wwan0"}, "25001"),
                                                  Modem({"wwan1"}, "26201")};
  UR_EXPECT_TRUE(urnw::ReadNetworkCountry("wwan1", true, true, modems).country_code == "de");
  UR_EXPECT_TRUE(urnw::ReadNetworkCountry("wwan0", true, true, modems).country_code == "ru");
}

// Every way to have no country says which, for the support log.
UR_TEST(networkCountryNamesWhyThereIsNone) {
  const std::vector<urnw::ModemReading> modems = {Modem({"wwan0"}, "25001")};
  struct Case {
    const char* label;
    urnw::NetworkCountryReading reading;
    urnw::NetworkCountrySource source;
  };
  const Case cases[] = {
      {"no modemmanager", urnw::ReadNetworkCountry("wwan0", false, false, modems),
       urnw::NetworkCountrySource::ModemManagerAbsent},
      {"no answer", urnw::ReadNetworkCountry("wwan0", true, false, modems),
       urnw::NetworkCountrySource::ModemManagerUnreadable},
      {"no route", urnw::ReadNetworkCountry("", true, true, modems),
       urnw::NetworkCountrySource::NoDefaultRoute},
      {"no modem", urnw::ReadNetworkCountry("wwan0", true, true, {}),
       urnw::NetworkCountrySource::NotCellular},
      {"not registered", urnw::ReadNetworkCountry("wwan0", true, true, {Modem({"wwan0"}, "")}),
       urnw::NetworkCountrySource::ModemNotRegistered},
      {"test network", urnw::ReadNetworkCountry("wwan0", true, true, {Modem({"wwan0"}, "00101")}),
       urnw::NetworkCountrySource::UnknownMcc},
  };
  for (const Case& c : cases) {
    UR_EXPECT_TRUE_MSG(c.label, c.reading.country_code.empty());
    UR_EXPECT_TRUE_MSG(c.label, c.reading.source == c.source);
  }
  // the source tokens are what the support log writes
  UR_EXPECT_TRUE(std::string(urnw::ToString(urnw::NetworkCountrySource::Modem)) == "modem");
  UR_EXPECT_TRUE(std::string(urnw::ToString(urnw::NetworkCountrySource::NotCellular)) ==
                 "not-cellular");
}

UR_TEST(networkCountryReadingsCompareByAllThreeFields) {
  const urnw::NetworkCountryReading wwan =
      urnw::ReadNetworkCountry("wwan0", true, true, {Modem({"wwan0"}, "25001")});
  urnw::NetworkCountryReading moved = wwan;
  UR_EXPECT_TRUE(moved == wwan);
  moved.interface_name = "wwan1";
  UR_EXPECT_TRUE(moved != wwan);
  moved = wwan;
  moved.source = urnw::NetworkCountrySource::UnknownMcc;
  UR_EXPECT_TRUE(moved != wwan);
}

// /proc/net/ipv6_route lists every table: the tun's capture routes (2000::/3 in
// table 51821, and a default on the tun skipped by name), the unreachable
// default the kernel keeps on lo, a reject default elsewhere, and the real
// defaults: Wi-Fi at metric 600 and the modem at 1024. The lowest metric wins.
UR_TEST(networkCountryIpv6DefaultRouteSkipsTheTunLoAndRejects) {
  const std::string routes = R"(
20000000000000000000000000000000 03 00000000000000000000000000000000 00 00000000000000000000000000000000 00000400 00000001 00000000 00000001 urnet0
00000000000000000000000000000000 00 00000000000000000000000000000000 00 00000000000000000000000000000000 00000001 00000001 00000000 00000001 urnet0
00000000000000000000000000000000 00 00000000000000000000000000000000 00 00000000000000000000000000000000 ffffffff 00000001 00000000 00200200 lo
00000000000000000000000000000000 00 00000000000000000000000000000000 00 00000000000000000000000000000000 00000002 00000001 00000000 00000201 wwan9
00000000000000000000000000000000 00 00000000000000000000000000000000 00 fe800000000000000000000000000001 00000258 00000001 00000000 00000003 wlp2s0
00000000000000000000000000000000 00 00000000000000000000000000000000 00 fe800000000000000000000000000002 00000400 00000001 00000000 00000003 wwan0
)";
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteInterfaceV6(routes, "urnet0") == "wlp2s0");

  const std::string modemOnly = R"(
00000000000000000000000000000000 00 00000000000000000000000000000000 00 fe800000000000000000000000000002 00000400 00000001 00000000 00000003 wwan0
)";
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteInterfaceV6(modemOnly, "urnet0") == "wwan0");

  // nothing but lo's reject route: no network
  const std::string loOnly = R"(
00000000000000000000000000000000 00 00000000000000000000000000000000 00 00000000000000000000000000000000 ffffffff 00000001 00000000 00200200 lo
)";
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteInterfaceV6(loOnly, "urnet0").empty());
  UR_EXPECT_TRUE(urnw::LinuxDefaultRouteInterfaceV6("", "urnet0").empty());
}
