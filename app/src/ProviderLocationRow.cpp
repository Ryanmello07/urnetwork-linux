// SPDX-License-Identifier: MPL-2.0
#include "ProviderLocationRow.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace urnw {

std::string PlaceLabel(const ProviderLocationRow& row) {
  std::string label;
  for (const std::string* part : {&row.city, &row.region, &row.country}) {
    if (part->empty()) continue;
    if (!label.empty()) label += ", ";
    label += *part;
  }
  return label;
}

std::string CoordinatesLabel(const ProviderLocationRow& row) {
  if (!row.hasCoordinates) return "\xE2\x80\x94";  // em dash
  char buf[64];
  // always the C locale's '.' decimal point, matching the other apps
  std::snprintf(buf, sizeof(buf), "%.4f, %.4f", row.lat, row.lon);
  return buf;
}

std::string IpFamilyTag(const ProviderLocationRow& row) {
  // the SDK's three labels (sdk ip_family.go); anything else is the v4 rule
  if (row.ipFamilyLabel == "both" || row.ipFamilyLabel == "v6") return row.ipFamilyLabel;
  return "v4";
}

namespace {

// ASCII case-insensitive equality: client ids are canonical lowercase uuids,
// but an id that went through another surface may not be
bool SameClientId(const std::string& a, const std::string& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

std::string Trimmed(const std::string& s) {
  const auto first = s.find_first_not_of(" \t");
  if (first == std::string::npos) return std::string();
  const auto last = s.find_last_not_of(" \t");
  return s.substr(first, last - first + 1);
}

}  // namespace

StayOnExitState StayOnExitStateFor(const ProviderLocationRow& row,
                                   const std::string& selectedClientId,
                                   const std::string& stayingClientId) {
  if (row.clientId.empty()) return StayOnExitState::None;
  if (SameClientId(row.clientId, stayingClientId)) return StayOnExitState::Staying;
  if (SameClientId(row.clientId, selectedClientId)) return StayOnExitState::Offer;
  return StayOnExitState::None;
}

std::string ShortClientId(const std::string& clientId) {
  const std::string id = Trimmed(clientId);
  if (id.size() <= 12) return id;
  return id.substr(0, 4) + "\xE2\x80\xA6" + id.substr(id.size() - 4);  // ellipsis
}

std::string StayOnExitName(const ProviderLocationRow& row) {
  const std::string shortId = ShortClientId(row.clientId);
  std::string place;
  for (const std::string* part : {row.city.empty() ? &row.region : &row.city, &row.country}) {
    if (part->empty()) continue;
    if (!place.empty()) place += ", ";
    place += *part;
  }
  if (place.empty()) return shortId;
  return shortId + " \xC2\xB7 " + place;  // middle dot
}

std::optional<StayOnExitTarget> MakeStayOnExitTarget(const ProviderLocationRow& row) {
  const std::string clientId = Trimmed(row.clientId);
  if (clientId.empty()) return std::nullopt;
  StayOnExitTarget target;
  target.clientId = clientId;
  target.name = StayOnExitName(row);
  target.city = row.city;
  target.region = row.region;
  target.country = row.country;
  target.countryCode = row.countryCode;
  return target;
}

int OldestPlottableIndex(const std::vector<ProviderLocationRow>& rows) {
  // Searched by STAMP, not by position: the rows arrive in the view
  // controller's display order (west to east), which says nothing about how
  // long anything has been connected. A zero stamp is "unknown" -- the sdk
  // sorts those last, so one only wins when nothing else is plottable.
  int best = -1;
  int64_t bestSince = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (!rows[i].plottable()) continue;
    const int64_t since = rows[i].connectedSinceMillis;
    const bool better = best < 0 ||                        // nothing yet
                        (bestSince == 0 && since != 0) ||  // any stamp beats none
                        (bestSince != 0 && since != 0 && since < bestSince);
    if (better) {
      best = static_cast<int>(i);
      bestSince = since;
    }
  }
  return best;
}

}  // namespace urnw
