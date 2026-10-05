// The country of the mobile network this machine is on (P052), which the
// daemon reports to the sdk with urnet::setNetworkCountryCode.
//
// The sdk fronts its extender dials with a name from the spoof list of the
// country the device is in, which the extender hint from the api tells it. On
// a whitelist-only mobile network no URnetwork address is routable, so the
// hint cannot be fetched, and the dials fall back to the network country the
// host reports (connect ExtenderDirectory.SpoofCountryCode). Android reports
// TelephonyManager.networkCountryIso while the default network is cellular and
// nothing on any other network; the sdk documents the value as exactly that,
// not as a weaker hint, and it outranks the hint's last country. So this
// reports the same thing and nothing else:
//
//   the default route leaves through the data interface of a ModemManager
//   modem -> the country of the operator that modem is registered to, read
//   from its 3GPP operator code (Modem3gpp.OperatorCode, "MCCMNC").
//
// Everything else reports no country: Wi-Fi, ethernet, a phone tethered over
// USB or Wi-Fi, a router-mode (HiLink) USB stick, a PPP modem (pppd owns the
// interface, which ModemManager does not name), and a modem that is not
// registered. The locale and the timezone are never used: they describe the
// user, not the network, and a wrong guess would outrank the operator's own
// last answer. The value never leaves the machine: the daemon applies it to its
// devices and passes it to the GUI over the control socket (status
// network_country_code), which applies it to its own dials.
//
// In practice this is a minority of Linux machines: those with a built-in or
// USB WWAN modem that ModemManager drives. Most mobile data on a Linux machine
// arrives through a tethered phone or a router-mode stick, and there the
// carrier is invisible to this machine, so there is no honest country to report.
//
// Pure, so the daemon's watcher (daemon/NetworkCountryWatcher.cpp) and the
// dependency-free unit suite run the same rules.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace urnw {

// ---- ITU-T E.212 mobile country codes --------------------------------------

struct MccCountry {
  int mcc;
  const char* country;  // ISO 3166-1 alpha-2, lower case
};

// Every assigned MCC and its country, ascending. The same mapping Android's
// MccTable gives TelephonyManager.networkCountryIso, so a Linux machine and an
// Android phone on one network report one country. Where one code covers
// several territories (340, 647) it names the one Android names.
inline constexpr MccCountry kMccCountries[] = {
    {202, "gr"},  // Greece
    {204, "nl"},  // Netherlands
    {206, "be"},  // Belgium
    {208, "fr"},  // France
    {212, "mc"},  // Monaco
    {213, "ad"},  // Andorra
    {214, "es"},  // Spain
    {216, "hu"},  // Hungary
    {218, "ba"},  // Bosnia and Herzegovina
    {219, "hr"},  // Croatia
    {220, "rs"},  // Serbia
    {221, "xk"},  // Kosovo
    {222, "it"},  // Italy
    {225, "va"},  // Vatican City State
    {226, "ro"},  // Romania
    {228, "ch"},  // Switzerland
    {230, "cz"},  // Czechia
    {231, "sk"},  // Slovakia
    {232, "at"},  // Austria
    {234, "gb"},  // United Kingdom
    {235, "gb"},  // United Kingdom
    {238, "dk"},  // Denmark
    {240, "se"},  // Sweden
    {242, "no"},  // Norway
    {244, "fi"},  // Finland
    {246, "lt"},  // Lithuania
    {247, "lv"},  // Latvia
    {248, "ee"},  // Estonia
    {250, "ru"},  // Russia
    {255, "ua"},  // Ukraine
    {257, "by"},  // Belarus
    {259, "md"},  // Moldova
    {260, "pl"},  // Poland
    {262, "de"},  // Germany
    {266, "gi"},  // Gibraltar
    {268, "pt"},  // Portugal
    {270, "lu"},  // Luxembourg
    {272, "ie"},  // Ireland
    {274, "is"},  // Iceland
    {276, "al"},  // Albania
    {278, "mt"},  // Malta
    {280, "cy"},  // Cyprus
    {282, "ge"},  // Georgia
    {283, "am"},  // Armenia
    {284, "bg"},  // Bulgaria
    {286, "tr"},  // Turkey
    {288, "fo"},  // Faroe Islands
    {289, "ge"},  // Abkhazia (Georgia)
    {290, "gl"},  // Greenland
    {292, "sm"},  // San Marino
    {293, "si"},  // Slovenia
    {294, "mk"},  // North Macedonia
    {295, "li"},  // Liechtenstein
    {297, "me"},  // Montenegro
    {302, "ca"},  // Canada
    {308, "pm"},  // Saint Pierre and Miquelon
    {310, "us"},  // United States
    {311, "us"},  // United States
    {312, "us"},  // United States
    {313, "us"},  // United States
    {314, "us"},  // United States
    {315, "us"},  // United States
    {316, "us"},  // United States
    {330, "pr"},  // Puerto Rico
    {332, "vi"},  // United States Virgin Islands
    {334, "mx"},  // Mexico
    {338, "jm"},  // Jamaica
    {340, "gp"},  // Guadeloupe, Martinique
    {342, "bb"},  // Barbados
    {344, "ag"},  // Antigua and Barbuda
    {346, "ky"},  // Cayman Islands
    {348, "vg"},  // British Virgin Islands
    {350, "bm"},  // Bermuda
    {352, "gd"},  // Grenada
    {354, "ms"},  // Montserrat
    {356, "kn"},  // Saint Kitts and Nevis
    {358, "lc"},  // Saint Lucia
    {360, "vc"},  // Saint Vincent and the Grenadines
    {362, "cw"},  // Curacao
    {363, "aw"},  // Aruba
    {364, "bs"},  // Bahamas
    {365, "ai"},  // Anguilla
    {366, "dm"},  // Dominica
    {368, "cu"},  // Cuba
    {370, "do"},  // Dominican Republic
    {372, "ht"},  // Haiti
    {374, "tt"},  // Trinidad and Tobago
    {376, "tc"},  // Turks and Caicos Islands
    {400, "az"},  // Azerbaijan
    {401, "kz"},  // Kazakhstan
    {402, "bt"},  // Bhutan
    {404, "in"},  // India
    {405, "in"},  // India
    {406, "in"},  // India
    {410, "pk"},  // Pakistan
    {412, "af"},  // Afghanistan
    {413, "lk"},  // Sri Lanka
    {414, "mm"},  // Myanmar
    {415, "lb"},  // Lebanon
    {416, "jo"},  // Jordan
    {417, "sy"},  // Syria
    {418, "iq"},  // Iraq
    {419, "kw"},  // Kuwait
    {420, "sa"},  // Saudi Arabia
    {421, "ye"},  // Yemen
    {422, "om"},  // Oman
    {423, "ps"},  // Palestine
    {424, "ae"},  // United Arab Emirates
    {425, "il"},  // Israel
    {426, "bh"},  // Bahrain
    {427, "qa"},  // Qatar
    {428, "mn"},  // Mongolia
    {429, "np"},  // Nepal
    {430, "ae"},  // United Arab Emirates
    {431, "ae"},  // United Arab Emirates
    {432, "ir"},  // Iran
    {434, "uz"},  // Uzbekistan
    {436, "tj"},  // Tajikistan
    {437, "kg"},  // Kyrgyzstan
    {438, "tm"},  // Turkmenistan
    {440, "jp"},  // Japan
    {441, "jp"},  // Japan
    {450, "kr"},  // South Korea
    {452, "vn"},  // Vietnam
    {454, "hk"},  // Hong Kong
    {455, "mo"},  // Macao
    {456, "kh"},  // Cambodia
    {457, "la"},  // Laos
    {460, "cn"},  // China
    {461, "cn"},  // China
    {466, "tw"},  // Taiwan
    {467, "kp"},  // North Korea
    {470, "bd"},  // Bangladesh
    {472, "mv"},  // Maldives
    {502, "my"},  // Malaysia
    {505, "au"},  // Australia
    {510, "id"},  // Indonesia
    {514, "tl"},  // Timor-Leste
    {515, "ph"},  // Philippines
    {520, "th"},  // Thailand
    {525, "sg"},  // Singapore
    {528, "bn"},  // Brunei Darussalam
    {530, "nz"},  // New Zealand
    {534, "mp"},  // Northern Mariana Islands
    {535, "gu"},  // Guam
    {536, "nr"},  // Nauru
    {537, "pg"},  // Papua New Guinea
    {539, "to"},  // Tonga
    {540, "sb"},  // Solomon Islands
    {541, "vu"},  // Vanuatu
    {542, "fj"},  // Fiji
    {543, "wf"},  // Wallis and Futuna
    {544, "as"},  // American Samoa
    {545, "ki"},  // Kiribati
    {546, "nc"},  // New Caledonia
    {547, "pf"},  // French Polynesia
    {548, "ck"},  // Cook Islands
    {549, "ws"},  // Samoa
    {550, "fm"},  // Micronesia
    {551, "mh"},  // Marshall Islands
    {552, "pw"},  // Palau
    {553, "tv"},  // Tuvalu
    {554, "tk"},  // Tokelau
    {555, "nu"},  // Niue
    {602, "eg"},  // Egypt
    {603, "dz"},  // Algeria
    {604, "ma"},  // Morocco
    {605, "tn"},  // Tunisia
    {606, "ly"},  // Libya
    {607, "gm"},  // Gambia
    {608, "sn"},  // Senegal
    {609, "mr"},  // Mauritania
    {610, "ml"},  // Mali
    {611, "gn"},  // Guinea
    {612, "ci"},  // Cote d'Ivoire
    {613, "bf"},  // Burkina Faso
    {614, "ne"},  // Niger
    {615, "tg"},  // Togo
    {616, "bj"},  // Benin
    {617, "mu"},  // Mauritius
    {618, "lr"},  // Liberia
    {619, "sl"},  // Sierra Leone
    {620, "gh"},  // Ghana
    {621, "ng"},  // Nigeria
    {622, "td"},  // Chad
    {623, "cf"},  // Central African Republic
    {624, "cm"},  // Cameroon
    {625, "cv"},  // Cabo Verde
    {626, "st"},  // Sao Tome and Principe
    {627, "gq"},  // Equatorial Guinea
    {628, "ga"},  // Gabon
    {629, "cg"},  // Republic of the Congo
    {630, "cd"},  // Democratic Republic of the Congo
    {631, "ao"},  // Angola
    {632, "gw"},  // Guinea-Bissau
    {633, "sc"},  // Seychelles
    {634, "sd"},  // Sudan
    {635, "rw"},  // Rwanda
    {636, "et"},  // Ethiopia
    {637, "so"},  // Somalia
    {638, "dj"},  // Djibouti
    {639, "ke"},  // Kenya
    {640, "tz"},  // Tanzania
    {641, "ug"},  // Uganda
    {642, "bi"},  // Burundi
    {643, "mz"},  // Mozambique
    {645, "zm"},  // Zambia
    {646, "mg"},  // Madagascar
    {647, "re"},  // Reunion, Mayotte
    {648, "zw"},  // Zimbabwe
    {649, "na"},  // Namibia
    {650, "mw"},  // Malawi
    {651, "ls"},  // Lesotho
    {652, "bw"},  // Botswana
    {653, "sz"},  // Eswatini
    {654, "km"},  // Comoros
    {655, "za"},  // South Africa
    {657, "er"},  // Eritrea
    {658, "sh"},  // Saint Helena
    {659, "ss"},  // South Sudan
    {702, "bz"},  // Belize
    {704, "gt"},  // Guatemala
    {706, "sv"},  // El Salvador
    {708, "hn"},  // Honduras
    {710, "ni"},  // Nicaragua
    {712, "cr"},  // Costa Rica
    {714, "pa"},  // Panama
    {716, "pe"},  // Peru
    {722, "ar"},  // Argentina
    {724, "br"},  // Brazil
    {730, "cl"},  // Chile
    {732, "co"},  // Colombia
    {734, "ve"},  // Venezuela
    {736, "bo"},  // Bolivia
    {738, "gy"},  // Guyana
    {740, "ec"},  // Ecuador
    {742, "gf"},  // French Guiana
    {744, "py"},  // Paraguay
    {746, "sr"},  // Suriname
    {748, "uy"},  // Uruguay
    {750, "fk"},  // Falkland Islands
};

// The country of a mobile country code, "" for one that is not assigned to a
// country (the 001 test network, the 9xx international codes).
inline std::string CountryCodeForMcc(int mcc) {
  size_t low = 0;
  size_t high = sizeof(kMccCountries) / sizeof(kMccCountries[0]);
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (kMccCountries[middle].mcc < mcc) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low < sizeof(kMccCountries) / sizeof(kMccCountries[0]) && kMccCountries[low].mcc == mcc) {
    return kMccCountries[low].country;
  }
  return std::string();
}

// The country of a 3GPP operator code: "MCCMNC", the three-digit MCC and a two-
// or three-digit MNC (ModemManager's Modem3gpp.OperatorCode). "" for anything
// else, which includes the empty code of a modem that is not registered.
inline std::string CountryCodeForOperatorCode(const std::string& operatorCode) {
  if (operatorCode.size() != 5 && operatorCode.size() != 6) return std::string();
  for (const char c : operatorCode) {
    if (c < '0' || c > '9') return std::string();
  }
  return CountryCodeForMcc((operatorCode[0] - '0') * 100 + (operatorCode[1] - '0') * 10 +
                           (operatorCode[2] - '0'));
}

// ---- the default route -----------------------------------------------------

// The interface of the lowest-metric IPv6 default route in /proc/net/ipv6_route
// ("" without one), skipping `excluded` (the tun), loopback and reject routes.
// The IPv4 twin, over /proc/net/route, is LinuxDefaultRouteInterface in
// NetworkQuality.hpp. ipv6_route lists every routing table, so the tun's own
// capture routes appear in it; none of them is a default route, and the tun is
// skipped by name besides.
inline std::string LinuxDefaultRouteInterfaceV6(const std::string& routes,
                                                const std::string& excluded) {
  constexpr std::uint64_t kRejectFlag = 0x0200;  // RTF_REJECT: unreachable/prohibit
  constexpr std::uint64_t kUpFlag = 0x0001;      // RTF_UP
  std::istringstream lines(routes);
  std::string line;
  std::string best;
  std::uint64_t bestMetric = std::numeric_limits<std::uint64_t>::max();
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string destination, destinationLength, source, sourceLength, nextHop, metricText,
        refCount, use, flagsText, iface;
    if (!(fields >> destination >> destinationLength >> source >> sourceLength >> nextHop >>
          metricText >> refCount >> use >> flagsText >> iface)) {
      continue;
    }
    if (destination != std::string(32, '0') || destinationLength != "00") continue;
    std::uint64_t flags = 0;
    std::uint64_t metric = 0;
    std::istringstream(flagsText) >> std::hex >> flags;
    std::istringstream(metricText) >> std::hex >> metric;
    if (iface == excluded || iface == "lo") continue;
    if ((flags & kUpFlag) == 0 || (flags & kRejectFlag) != 0) continue;
    if (metric < bestMetric || (metric == bestMetric && iface < best)) {
      best = iface;
      bestMetric = metric;
    }
  }
  return best;
}

// ---- the reading -----------------------------------------------------------

// One ModemManager modem, as far as the country needs it.
struct ModemReading {
  // The interfaces its data leaves through: its network ports
  // (Modem.Ports, MM_MODEM_PORT_TYPE_NET) and the interface of each connected
  // bearer (Bearer.Interface), which differs from the port on a multiplexed
  // bearer.
  std::vector<std::string> data_interfaces;
  // Modem3gpp.OperatorCode: the operator the modem is registered to, "" when
  // it is not registered or is not a 3GPP modem.
  std::string operator_code;
};

// Where the reading came from, so a support log can tell "no modem" from "a
// modem that is not the default route" (DiagnosticLines.hpp writes it).
enum class NetworkCountrySource {
  ModemManagerAbsent,      // ModemManager does not run on this machine
  ModemManagerUnreadable,  // it runs and did not answer
  NoDefaultRoute,          // no network at all
  NotCellular,             // the default route leaves through no modem
  ModemNotRegistered,      // it leaves through a modem with no operator
  UnknownMcc,              // ...whose operator code names no country
  Modem,                   // the country of that modem's operator
};

inline const char* ToString(NetworkCountrySource source) {
  switch (source) {
    case NetworkCountrySource::ModemManagerAbsent: return "modemmanager-absent";
    case NetworkCountrySource::ModemManagerUnreadable: return "modemmanager-unreadable";
    case NetworkCountrySource::NoDefaultRoute: return "no-default-route";
    case NetworkCountrySource::NotCellular: return "not-cellular";
    case NetworkCountrySource::ModemNotRegistered: return "modem-not-registered";
    case NetworkCountrySource::UnknownMcc: return "unknown-mcc";
    case NetworkCountrySource::Modem: return "modem";
  }
  return "unknown";
}

struct NetworkCountryReading {
  std::string country_code;  // lower case alpha-2, "" for none
  NetworkCountrySource source = NetworkCountrySource::ModemManagerAbsent;
  std::string interface_name;  // the default route's interface, "" without one

  bool operator==(const NetworkCountryReading& other) const {
    return country_code == other.country_code && source == other.source &&
           interface_name == other.interface_name;
  }
  bool operator!=(const NetworkCountryReading& other) const { return !(*this == other); }
};

// The rule. `modemManagerRunning`: its bus name has an owner. `modemManagerAnswered`:
// the last read of its modems succeeded (`modems` is that read).
inline NetworkCountryReading ReadNetworkCountry(const std::string& defaultRouteInterface,
                                                bool modemManagerRunning,
                                                bool modemManagerAnswered,
                                                const std::vector<ModemReading>& modems) {
  NetworkCountryReading reading;
  reading.interface_name = defaultRouteInterface;
  if (!modemManagerRunning) {
    reading.source = NetworkCountrySource::ModemManagerAbsent;
    return reading;
  }
  if (!modemManagerAnswered) {
    reading.source = NetworkCountrySource::ModemManagerUnreadable;
    return reading;
  }
  if (defaultRouteInterface.empty()) {
    reading.source = NetworkCountrySource::NoDefaultRoute;
    return reading;
  }
  for (const ModemReading& modem : modems) {
    bool carriesDefaultRoute = false;
    for (const std::string& iface : modem.data_interfaces) {
      if (iface == defaultRouteInterface) carriesDefaultRoute = true;
    }
    if (!carriesDefaultRoute) continue;
    if (modem.operator_code.empty()) {
      reading.source = NetworkCountrySource::ModemNotRegistered;
      return reading;
    }
    reading.country_code = CountryCodeForOperatorCode(modem.operator_code);
    reading.source = reading.country_code.empty() ? NetworkCountrySource::UnknownMcc
                                                  : NetworkCountrySource::Modem;
    return reading;
  }
  reading.source = NetworkCountrySource::NotCellular;
  return reading;
}

}  // namespace urnw
