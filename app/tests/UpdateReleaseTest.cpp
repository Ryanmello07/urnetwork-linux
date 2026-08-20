// Ordering and parsing over release data from BOTH channels.
//
// The fixtures are real: the tags and asset names below are copied from actual
// releases of Ryanmello07/urnetwork-linux and urnetwork/build, including the
// upstream release that shipped no Linux artifacts at all. That case is the
// reason HasAnyOf exists -- a version with nothing installable must not appear
// in the picker as though it were a choice.
//
// SPDX-License-Identifier: MPL-2.0
#include "UpdateRelease.hpp"

#include "TestHarness.hpp"

using namespace urnw::update;

// A trimmed but structurally faithful GET /releases payload.
namespace {
const char* kReleasesJson = R"([
  {
    "tag_name": "v2026.8.20-1024376890-beta",
    "published_at": "2026-08-20T14:31:00Z",
    "prerelease": true,
    "draft": false,
    "assets": [
      {"name": "URnetwork-2026.8.20-1024376890-beta-amd64.AppImage",
       "browser_download_url": "https://example.invalid/a.AppImage", "size": 72000000},
      {"name": "URnetwork-2026.8.20-1024376890-beta-amd64.flatpak",
       "browser_download_url": "https://example.invalid/a.flatpak", "size": 13000000},
      {"name": "urnetwork-daemon-2026.8.20-1024376890-beta-amd64.install.tar.gz",
       "browser_download_url": "https://example.invalid/a.tar.gz", "size": 19000000},
      {"name": "urnetwork-daemon-2026.8.20-1024376890-beta.x86_64.rpm",
       "browser_download_url": "https://example.invalid/a.rpm", "size": 15000000}
    ]
  },
  {
    "tag_name": "v2026.8.16-1021039260",
    "published_at": "2026-08-16T00:00:00Z",
    "prerelease": false,
    "draft": false,
    "assets": [
      {"name": "URnetwork-2026.8.16-1021039260.ipa",
       "browser_download_url": "https://example.invalid/b.ipa", "size": 1},
      {"name": "URnetworkSdk-2026.8.16-1021039260.aar",
       "browser_download_url": "https://example.invalid/b.aar", "size": 1}
    ]
  },
  {
    "tag_name": "v2026.8.15-1020621320",
    "published_at": "2026-08-16T00:00:00Z",
    "prerelease": false,
    "draft": false,
    "assets": [
      {"name": "URnetwork-2026.8.15-1020621320-amd64.AppImage",
       "browser_download_url": "https://example.invalid/c.AppImage", "size": 70000000},
      {"name": "urnetwork-daemon-2026.8.15-1020621320-amd64.install.tar.gz",
       "browser_download_url": "https://example.invalid/c.tar.gz", "size": 18000000},
      {"name": "urnetwork-daemon_2026.8.15-1020621320_amd64.deb",
       "browser_download_url": "https://example.invalid/c.deb", "size": 15000000}
    ]
  },
  {
    "tag_name": "v2026.9.1-1030000000",
    "published_at": "2026-09-01T00:00:00Z",
    "prerelease": false,
    "draft": true,
    "assets": []
  }
])";
}  // namespace

UR_TEST(UpdateRelease_VersionOrdering) {
  // Date dominates.
  UR_EXPECT_TRUE(CompareVersions("2026.8.20-1", "2026.8.15-999999") > 0);
  UR_EXPECT_TRUE(CompareVersions("2026.9.1-1", "2026.8.31-1") > 0);
  UR_EXPECT_TRUE(CompareVersions("2027.1.1-1", "2026.12.31-1") > 0);
  // Same date: the run id breaks the tie.
  UR_EXPECT_TRUE(CompareVersions("2026.8.20-1024376890", "2026.8.20-1023979520") > 0);
  UR_EXPECT_EQ(0, CompareVersions("2026.8.20-1024376890", "2026.8.20-1024376890"));

  // Single-digit month/day must not compare as strings: "2026.8.9" is NEWER
  // than "2026.10.1" only if you compare lexically, which would be wrong.
  UR_EXPECT_TRUE(CompareVersions("2026.10.1-1", "2026.8.9-1") > 0);
  UR_EXPECT_TRUE(CompareVersions("2026.8.9-1", "2026.8.20-1") < 0);
}

UR_TEST(UpdateRelease_BetaSuffixIsNotOrdering) {
  // THE INTERCHANGEABILITY RULE. The suffix records which repo a build came
  // from; it must not make a build look newer or older than the same version
  // without it, or switching channels would offer phantom up/downgrades.
  UR_EXPECT_EQ(0, CompareVersions("2026.8.20-1024376890-beta", "2026.8.20-1024376890"));
  UR_EXPECT_TRUE(CompareVersions("2026.8.20-1024376890-beta", "2026.8.15-1020621320") > 0);
  UR_EXPECT_TRUE(CompareVersions("2026.8.15-1020621320", "2026.8.20-1024376890-beta") < 0);

  // ...but it is still reportable as a fact about the build.
  UR_EXPECT_TRUE(IsBetaVersion("2026.8.20-1024376890-beta"));
  UR_EXPECT_TRUE(!IsBetaVersion("2026.8.15-1020621320"));
}

UR_TEST(UpdateRelease_LeadingVAndJunk) {
  UR_EXPECT_EQ(0, CompareVersions("v2026.8.20-1", "2026.8.20-1"));
  const VersionKey k = ParseVersion("v2026.8.20-1024376890-beta");
  UR_EXPECT_EQ(2026L, k.year);
  UR_EXPECT_EQ(8L, k.month);
  UR_EXPECT_EQ(20L, k.day);
  UR_EXPECT_EQ(1024376890L, k.run);

  // Unparseable sorts below everything real rather than throwing.
  UR_EXPECT_TRUE(CompareVersions("not-a-version", "2026.8.15-1") < 0);
  const VersionKey bad = ParseVersion("");
  UR_EXPECT_EQ(0L, bad.year);
}

UR_TEST(UpdateRelease_ParseReleases) {
  std::vector<Release> rs = ParseReleases(kReleasesJson);
  // The draft is dropped; the other three survive.
  UR_EXPECT_EQ(size_t{3}, rs.size());
  for (const auto& r : rs) UR_EXPECT_TRUE(r.tag != "v2026.9.1-1030000000");

  SortNewestFirst(rs);
  UR_EXPECT_TRUE(rs[0].tag == "v2026.8.20-1024376890-beta");
  UR_EXPECT_TRUE(rs[1].tag == "v2026.8.16-1021039260");
  UR_EXPECT_TRUE(rs[2].tag == "v2026.8.15-1020621320");

  // version is the tag minus the leading v.
  UR_EXPECT_TRUE(rs[0].version == "2026.8.20-1024376890-beta");
  UR_EXPECT_TRUE(rs[0].prerelease);
  UR_EXPECT_TRUE(!rs[2].prerelease);
}

UR_TEST(UpdateRelease_AssetLookup) {
  std::vector<Release> rs = ParseReleases(kReleasesJson);
  SortNewestFirst(rs);

  const Asset* rpm = rs[0].FindBySuffix(".x86_64.rpm");
  UR_EXPECT_TRUE(rpm != nullptr);
  UR_EXPECT_TRUE(rpm->downloadUrl == "https://example.invalid/a.rpm");

  // Upstream's 8.15 has a .deb but no .rpm and no .flatpak -- exactly the
  // asymmetry ServiceInstall::OfferFor has to cope with.
  UR_EXPECT_TRUE(rs[2].FindBySuffix("_amd64.deb") != nullptr);
  UR_EXPECT_TRUE(rs[2].FindBySuffix(".x86_64.rpm") == nullptr);
  UR_EXPECT_TRUE(rs[2].FindBySuffix("-amd64.flatpak") == nullptr);

  UR_EXPECT_EQ(size_t{4}, rs[0].AssetNames().size());
}

UR_TEST(UpdateRelease_NoLinuxArtifacts) {
  std::vector<Release> rs = ParseReleases(kReleasesJson);
  SortNewestFirst(rs);

  const std::vector<std::string> linux = {
      "-amd64.AppImage", "-amd64.flatpak", "-amd64.install.tar.gz",
      "_amd64.deb", ".x86_64.rpm"};

  UR_EXPECT_TRUE(HasAnyOf(rs[0], linux));   // beta: everything
  UR_EXPECT_TRUE(!HasAnyOf(rs[1], linux));  // upstream 8.16: ipa + aar only
  UR_EXPECT_TRUE(HasAnyOf(rs[2], linux));   // upstream 8.15: AppImage/deb/tarball
}

UR_TEST(UpdateRelease_MalformedBody) {
  UR_EXPECT_TRUE(ParseReleases("").empty());
  UR_EXPECT_TRUE(ParseReleases("not json").empty());
  UR_EXPECT_TRUE(ParseReleases("{\"message\":\"Not Found\"}").empty());  // an API error object
  UR_EXPECT_TRUE(ParseReleases("[]").empty());
  // An entry with no tag is skipped, the valid one survives.
  UR_EXPECT_EQ(size_t{1}, ParseReleases(R"([{"assets":[]},{"tag_name":"v1.2.3-4"}])").size());
}
