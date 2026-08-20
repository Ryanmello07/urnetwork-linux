// A release as the GitHub API reports it, and the ordering over versions.
//
// PURE ON PURPOSE. Everything here is parsing and comparison over data the
// caller already has, with no I/O and no GTK, so the test binary can link it
// against nlohmann_json alone. The network half lives in UpdateChecker.cpp,
// which the tests never link. This is the same split ControlProtocol.hpp uses.
//
// THE TWO CHANNELS ARE INTERCHANGEABLE. Beta and upstream publish the same
// artifacts under the same naming; the only difference is which repository the
// releases come from. So the "-beta" suffix is NOT part of the ordering: it
// says where a build came from, not how new it is. An upstream install can
// take a beta release and a beta install can go back to upstream, and in both
// directions "newer" has to mean the same thing. Ordering is therefore
// (year, month, day, run id) and nothing else.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace urnw::update {

struct Asset {
  std::string name;          // urnetwork-daemon-2026.8.20-...-amd64.install.tar.gz
  std::string downloadUrl;   // browser_download_url
  std::uint64_t size = 0;
};

struct Release {
  std::string tag;          // v2026.8.20-1024376890-beta
  std::string version;      // the tag without a leading 'v'
  std::string publishedAt;  // ISO-8601, verbatim from the API
  bool prerelease = false;
  std::vector<Asset> assets;

  // Asset names only — what ServiceInstall::OfferFor expects.
  std::vector<std::string> AssetNames() const {
    std::vector<std::string> names;
    names.reserve(assets.size());
    for (const auto& a : assets) names.push_back(a.name);
    return names;
  }

  // First asset whose name ends with `suffix`, or nullptr. Suffix rather than
  // exact match because every asset name embeds the version.
  const Asset* FindBySuffix(const std::string& suffix) const {
    for (const auto& a : assets) {
      if (a.name.size() >= suffix.size() &&
          a.name.compare(a.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return &a;
      }
    }
    return nullptr;
  }
};

// <year, month, day, run id>. Anything unparseable becomes 0, which sorts a
// malformed version below every real one rather than throwing.
struct VersionKey {
  long year = 0;
  long month = 0;
  long day = 0;
  long run = 0;
};

namespace detail {

inline long ToLong(const std::string& s) {
  if (s.empty()) return 0;
  for (char c : s) {
    if (c < '0' || c > '9') return 0;
  }
  try {
    return std::stol(s);
  } catch (...) {
    return 0;
  }
}

}  // namespace detail

// "2026.8.20-1024376890-beta" -> {2026, 8, 20, 1024376890}
// A leading 'v' is tolerated so a tag can be passed straight in.
inline VersionKey ParseVersion(std::string v) {
  VersionKey k;
  if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);

  // Drop the channel marker before splitting: it is not part of the ordering.
  const std::string beta = "-beta";
  if (v.size() >= beta.size() &&
      v.compare(v.size() - beta.size(), beta.size(), beta) == 0) {
    v.erase(v.size() - beta.size());
  }

  // <base>-<run>. Split at the LAST hyphen so a base containing one is safe.
  std::string base = v;
  const std::string::size_type dash = v.rfind('-');
  if (dash != std::string::npos) {
    base = v.substr(0, dash);
    k.run = detail::ToLong(v.substr(dash + 1));
  }

  std::string::size_type start = 0;
  long* fields[3] = {&k.year, &k.month, &k.day};
  for (int i = 0; i < 3; ++i) {
    const std::string::size_type dot = base.find('.', start);
    const std::string part = base.substr(start, dot == std::string::npos ? dot : dot - start);
    *fields[i] = detail::ToLong(part);
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return k;
}

// <0 if a is older, 0 if equal, >0 if a is newer.
inline int CompareVersions(const std::string& a, const std::string& b) {
  const VersionKey ka = ParseVersion(a);
  const VersionKey kb = ParseVersion(b);
  if (ka.year != kb.year) return ka.year < kb.year ? -1 : 1;
  if (ka.month != kb.month) return ka.month < kb.month ? -1 : 1;
  if (ka.day != kb.day) return ka.day < kb.day ? -1 : 1;
  if (ka.run != kb.run) return ka.run < kb.run ? -1 : 1;
  return 0;
}

inline bool IsBetaVersion(const std::string& v) {
  const std::string beta = "-beta";
  return v.size() >= beta.size() &&
         v.compare(v.size() - beta.size(), beta.size(), beta) == 0;
}

// Parse the array GET /repos/<owner>/<repo>/releases returns.
//
// TOLERANT BY DESIGN: a release missing a field is skipped, not fatal. The
// upstream repo publishes releases for every platform, and one malformed or
// unfamiliar entry must not blank the whole version list.
inline std::vector<Release> ParseReleases(const std::string& body) {
  std::vector<Release> out;
  nlohmann::json j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_array()) return out;

  for (const auto& item : j) {
    if (!item.is_object()) continue;
    if (item.value("draft", false)) continue;  // drafts are not installable

    Release r;
    r.tag = item.value("tag_name", std::string());
    if (r.tag.empty()) continue;
    r.version = r.tag;
    if (!r.version.empty() && (r.version[0] == 'v' || r.version[0] == 'V')) {
      r.version.erase(0, 1);
    }
    r.publishedAt = item.value("published_at", std::string());
    r.prerelease = item.value("prerelease", false);

    if (item.contains("assets") && item["assets"].is_array()) {
      for (const auto& a : item["assets"]) {
        if (!a.is_object()) continue;
        Asset asset;
        asset.name = a.value("name", std::string());
        asset.downloadUrl = a.value("browser_download_url", std::string());
        if (asset.name.empty() || asset.downloadUrl.empty()) continue;
        asset.size = a.value("size", std::uint64_t{0});
        r.assets.push_back(std::move(asset));
      }
    }
    out.push_back(std::move(r));
  }
  return out;
}

// Newest first. Stable ordering for the version list in Settings.
inline void SortNewestFirst(std::vector<Release>& releases) {
  std::sort(releases.begin(), releases.end(), [](const Release& a, const Release& b) {
    const int c = CompareVersions(a.version, b.version);
    if (c != 0) return c > 0;
    return a.tag > b.tag;  // deterministic tie-break
  });
}

// THE ONLY RELEASES WORTH OFFERING are those carrying an artifact this install
// can actually use. A release with no Linux assets at all -- upstream publishes
// several, e.g. v2026.8.16 which shipped only ipa/pkg/sdk artifacts -- must not
// appear in the picker as an installable version.
inline bool HasAnyOf(const Release& r, const std::vector<std::string>& suffixes) {
  for (const auto& s : suffixes) {
    if (r.FindBySuffix(s) != nullptr) return true;
  }
  return false;
}

}  // namespace urnw::update
