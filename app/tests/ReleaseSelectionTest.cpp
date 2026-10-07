// The in-app updater's decisions, against the names the release pipeline
// actually publishes (build/all/run.sh require_linux_artifacts) and the one
// repository the updater may ever read from.
//
// The repo is pinned here on purpose, with negative controls: the nightly
// urnetwork/build repo and a personal fork carry the same asset names, and a
// checker that followed either would install whatever they serve. An
// "update source" that is not the official stable repo is a defect this test
// exists to catch before a reviewer has to.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "ReleaseSelection.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace urnw::update;

constexpr const char* kOfficialDownload =
    "https://github.com/urnetwork/linux/releases/download/";

// A release object as the GitHub API serializes it, with every asset of one
// Linux release attached (the names run.sh uploads) unless `assets` says
// otherwise.
nlohmann::json ReleaseJson(const std::string& version, bool draft, bool prerelease,
                           const std::vector<std::string>& assets, const std::string& digest,
                           const std::string& host = kOfficialDownload) {
  nlohmann::json rel = nlohmann::json::object();
  rel["tag_name"] = "v" + version;
  rel["html_url"] = "https://github.com/urnetwork/linux/releases/tag/v" + version;
  rel["draft"] = draft;
  rel["prerelease"] = prerelease;
  rel["assets"] = nlohmann::json::array();
  for (const auto& name : assets) {
    nlohmann::json asset = nlohmann::json::object();
    asset["name"] = name;
    asset["browser_download_url"] = host + "v" + version + "/" + name;
    asset["digest"] = digest;
    rel["assets"].push_back(asset);
  }
  return rel;
}

const std::string kHex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const std::string kDigest = "sha256:" + kHex;

std::vector<std::string> AllAssets(const std::string& v) {
  return {
      "urnetwork-daemon_" + v + "_amd64.deb",
      "urnetwork-daemon_" + v + "_arm64.deb",
      "urnetwork-daemon-" + v + "-amd64.install.tar.gz",
      "urnetwork-daemon-" + v + "-arm64.install.tar.gz",
      "urnetwork-daemon-" + v + ".x86_64.rpm",
      "urnetwork-daemon-" + v + ".aarch64.rpm",
      "urnetwork-daemon-" + v + "-x86_64.pkg.tar.zst",
      "urnetwork-daemon-" + v + "-aarch64.pkg.tar.zst",
      "URnetwork-" + v + "-amd64.AppImage",
      "URnetwork-" + v + "-arm64.AppImage",
      "URnetwork-" + v + "-arm64.flatpak",
  };
}

constexpr std::uint64_t kOwn = 895075980;  // 2026.3.23-895075980, a shipped release

}  // namespace

// ---- the one repository -----------------------------------------------------

UR_TEST(theUpdateSourceIsTheOfficialStableLinuxRepo) {
  UR_EXPECT_TRUE(std::string(kUpdateRepo) == "urnetwork/linux");
  // the id GitHub assigned urnetwork/linux (repos/urnetwork/linux .id); the
  // list is asked for by it, never by a name a rename could redirect
  UR_EXPECT_EQ(std::uint64_t{1297137671}, kUpdateRepoId);
  UR_EXPECT_TRUE(ReleasesApiUrl() ==
                 "https://api.github.com/repositories/1297137671/releases?per_page=15");
  UR_EXPECT_TRUE(ReleasesApiUrl().find("/repos/") == std::string::npos);
  UR_EXPECT_TRUE(ReleasePageUrl("v2026.3.23-895075980") ==
                 "https://github.com/urnetwork/linux/releases/tag/v2026.3.23-895075980");
  UR_EXPECT_TRUE(ReleasePageUrl({}) == "https://github.com/urnetwork/linux/releases");
  // negative controls: the nightly repo and a personal fork are not sources
  UR_EXPECT_TRUE(ReleasesApiUrl().find("urnetwork/build") == std::string::npos);
  UR_EXPECT_TRUE(ReleasePageUrl({}).find("urnetwork/build") == std::string::npos);
}

UR_TEST(onlyTheReleasesOwnDownloadPathMayServeAnAsset) {
  const std::string tag = "v2026.3.23-895075980";
  const std::string asset = "URnetwork-2026.3.23-895075980-amd64.AppImage";
  const std::string exact =
      "https://github.com/urnetwork/linux/releases/download/" + tag + "/" + asset;
  UR_EXPECT_TRUE(FeedAssetUrl(tag, asset) == exact);
  UR_EXPECT_TRUE(IsFeedAssetUrl(tag, asset, exact));
  // the nightly repo, a personal fork of the same name
  UR_EXPECT_FALSE(IsFeedAssetUrl(
      tag, asset, "https://github.com/urnetwork/build/releases/download/" + tag + "/" + asset));
  UR_EXPECT_FALSE(IsFeedAssetUrl(
      tag, asset, "https://github.com/someone/linux/releases/download/" + tag + "/" + asset));
  // a lookalike host, a scheme downgrade, the download path alone, nothing
  UR_EXPECT_FALSE(IsFeedAssetUrl(
      tag, asset,
      "https://github.com.example/urnetwork/linux/releases/download/" + tag + "/" + asset));
  UR_EXPECT_FALSE(IsFeedAssetUrl(
      tag, asset, "http://github.com/urnetwork/linux/releases/download/" + tag + "/" + asset));
  UR_EXPECT_FALSE(
      IsFeedAssetUrl(tag, asset, "https://github.com/urnetwork/linux/releases/download/"));
  UR_EXPECT_FALSE(IsFeedAssetUrl(tag, asset, ""));
  // on the official repo's download path, but not this release's file:
  // another tag, another asset, a query or fragment, a path that climbs out
  UR_EXPECT_FALSE(IsFeedAssetUrl(
      tag, asset,
      "https://github.com/urnetwork/linux/releases/download/v2026.3.1-800000000/" + asset));
  UR_EXPECT_FALSE(IsFeedAssetUrl(
      tag, asset,
      "https://github.com/urnetwork/linux/releases/download/" + tag +
          "/URnetwork-2026.3.23-895075980-arm64.AppImage"));
  UR_EXPECT_FALSE(IsFeedAssetUrl(tag, asset, exact + "?x"));
  UR_EXPECT_FALSE(IsFeedAssetUrl(tag, asset, exact + "#x"));
  UR_EXPECT_FALSE(IsFeedAssetUrl(
      tag, asset,
      "https://github.com/urnetwork/linux/releases/download/" + tag + "/../../../build/" + asset));
  // no tag or no asset names no URL
  UR_EXPECT_FALSE(IsFeedAssetUrl("", asset, FeedAssetUrl("", asset)));
  UR_EXPECT_FALSE(IsFeedAssetUrl(tag, "", FeedAssetUrl(tag, "")));
}

// ---- the tag grammar --------------------------------------------------------

UR_TEST(releaseCodeIsParsedFromThePipelinesTagGrammar) {
  UR_EXPECT_EQ(std::uint64_t{895075980}, ParseReleaseCode("v2026.3.23-895075980"));
  UR_EXPECT_EQ(std::uint64_t{895075980}, ParseReleaseCode("2026.3.23-895075980"));  // v-less
  UR_EXPECT_EQ(std::uint64_t{101076420}, ParseReleaseCode("v2026.8.9-101076420-beta"));
  UR_EXPECT_EQ(std::uint64_t{0}, ParseReleaseCode("0.0.0"));    // the dev sentinel
  UR_EXPECT_EQ(std::uint64_t{0}, ParseReleaseCode("0.0.1"));    // meson's project version
  UR_EXPECT_EQ(std::uint64_t{0}, ParseReleaseCode(""));
  UR_EXPECT_EQ(std::uint64_t{0}, ParseReleaseCode("v2026.13.1-1"));
  UR_EXPECT_EQ(std::uint64_t{0}, ParseReleaseCode("v2026.3.23"));
  UR_EXPECT_EQ(std::uint64_t{0}, ParseReleaseCode("v2026.3.23-895075980-rc1"));
  UR_EXPECT_EQ(std::uint64_t{0}, ParseReleaseCode("v2026.3.23-1234567890123456789"));
  UR_EXPECT_TRUE(VersionFromTag("v2026.3.23-895075980") == "2026.3.23-895075980");
}

UR_TEST(digestHexAcceptsOnlyAFullSha256) {
  UR_EXPECT_TRUE(DigestHexFromAssetDigest(kDigest) == kHex);
  UR_EXPECT_TRUE(DigestHexFromAssetDigest("").empty());
  UR_EXPECT_TRUE(DigestHexFromAssetDigest("md5:" + kHex).empty());
  UR_EXPECT_TRUE(DigestHexFromAssetDigest("sha256:" + kHex.substr(0, 63)).empty());
  UR_EXPECT_TRUE(DigestHexFromAssetDigest("sha256:" + kHex + "0").empty());
  std::string upper = kHex;
  upper[0] = 'A';
  UR_EXPECT_TRUE(DigestHexFromAssetDigest("sha256:" + upper).empty());
  std::string notHex = kHex;
  notHex[10] = 'g';
  UR_EXPECT_TRUE(DigestHexFromAssetDigest("sha256:" + notHex).empty());
}

// ---- the asset names --------------------------------------------------------

UR_TEST(assetNamesAreThePipelinesOwn) {
  const std::string v = "2026.3.23-895075980";
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::AppImage, v, "amd64") ==
                 "URnetwork-2026.3.23-895075980-amd64.AppImage");
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::AppImage, v, "arm64") ==
                 "URnetwork-2026.3.23-895075980-arm64.AppImage");
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::Flatpak, v, "arm64") ==
                 "URnetwork-2026.3.23-895075980-arm64.flatpak");
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::Deb, v, "amd64") ==
                 "urnetwork-daemon_2026.3.23-895075980_amd64.deb");
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::Rpm, v, "amd64") ==
                 "urnetwork-daemon-2026.3.23-895075980.x86_64.rpm");
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::Rpm, v, "arm64") ==
                 "urnetwork-daemon-2026.3.23-895075980.aarch64.rpm");
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::Arch, v, "amd64") ==
                 "urnetwork-daemon-2026.3.23-895075980-x86_64.pkg.tar.zst");
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::Tarball, v, "arm64") ==
                 "urnetwork-daemon-2026.3.23-895075980-arm64.install.tar.gz");
  // every name above is one the pipeline publishes
  const std::vector<std::string> all = AllAssets(v);
  for (const InstallKind kind : {InstallKind::AppImage, InstallKind::Deb, InstallKind::Rpm,
                                 InstallKind::Arch, InstallKind::Tarball}) {
    for (const char* arch : {"amd64", "arm64"}) {
      const std::string name = AssetNameFor(kind, v, arch);
      bool found = false;
      for (const auto& a : all) found = found || a == name;
      UR_EXPECT_TRUE_MSG(name + " is not a pipeline asset", found);
    }
  }
  // no name without a kind, a version or a known arch
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::Unknown, v, "amd64").empty());
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::AppImage, "", "amd64").empty());
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::AppImage, v, "riscv64").empty());
  UR_EXPECT_TRUE(AssetNameFor(InstallKind::AppImage, v, "").empty());
}

UR_TEST(packageManagerCommandsNameTheDownloadedFileAndNeverTheAppImage) {
  UR_EXPECT_TRUE(PackageManagerCommand(InstallKind::Deb, "a.deb") == "sudo apt install ./a.deb");
  UR_EXPECT_TRUE(PackageManagerCommand(InstallKind::Rpm, "a.rpm") == "sudo dnf install ./a.rpm");
  UR_EXPECT_TRUE(PackageManagerCommand(InstallKind::Arch, "a.pkg.tar.zst") ==
                 "sudo pacman -U ./a.pkg.tar.zst");
  UR_EXPECT_TRUE(PackageManagerCommand(InstallKind::Flatpak, "a.flatpak") ==
                 "flatpak install ./a.flatpak");
  UR_EXPECT_TRUE(PackageManagerCommand(InstallKind::Tarball, "a.tar.gz") ==
                 "tar xzf a.tar.gz && sudo urnetwork-daemon/install.sh");
  UR_EXPECT_TRUE(PackageManagerCommand(InstallKind::AppImage, "a.AppImage").empty());
  UR_EXPECT_TRUE(PackageManagerCommand(InstallKind::Unknown, "a").empty());
}

// ---- the selection ----------------------------------------------------------

UR_TEST(aNewerReleaseWithTheOwnArchAppImageAndADigestIsOffered) {
  const std::string v = "2026.4.1-900000000";
  const auto releases = ParseReleases(nlohmann::json::array({
      ReleaseJson(v, false, false, AllAssets(v), kDigest),
  }));
  const Selection s = SelectRelease(releases, kOwn, InstallKind::AppImage, "amd64");
  UR_EXPECT_EQ(std::uint64_t{900000000}, s.newestCode);
  UR_EXPECT_TRUE(s.newestVersion == v);
  UR_EXPECT_EQ(std::uint64_t{900000000}, s.code);
  UR_EXPECT_TRUE(s.version == v);
  UR_EXPECT_TRUE(s.tag == "v" + v);
  UR_EXPECT_TRUE(s.assetName == "URnetwork-" + v + "-amd64.AppImage");
  UR_EXPECT_TRUE(s.assetUrl == std::string(kOfficialDownload) + "v" + v + "/URnetwork-" + v +
                                   "-amd64.AppImage");
  UR_EXPECT_TRUE(s.digestHex == kHex);
  UR_EXPECT_TRUE(s.releasePage == "https://github.com/urnetwork/linux/releases/tag/v" + v);
  UR_EXPECT_TRUE(s.updateAvailable);
  UR_EXPECT_TRUE(s.installable);
  UR_EXPECT_TRUE(s.skipped.empty());

  // the arm64 build takes the arm64 image
  const Selection arm = SelectRelease(releases, kOwn, InstallKind::AppImage, "arm64");
  UR_EXPECT_TRUE(arm.assetName == "URnetwork-" + v + "-arm64.AppImage");
  UR_EXPECT_TRUE(arm.installable);
}

UR_TEST(anOlderOrEqualReleaseIsNotAnUpdate) {
  const std::string older = "2026.3.1-800000000";
  const std::string same = "2026.3.23-895075980";
  const auto releases = ParseReleases(nlohmann::json::array({
      ReleaseJson(older, false, false, AllAssets(older), kDigest),
      ReleaseJson(same, false, false, AllAssets(same), kDigest),
  }));
  const Selection s = SelectRelease(releases, kOwn, InstallKind::AppImage, "amd64");
  UR_EXPECT_EQ(kOwn, s.newestCode);
  UR_EXPECT_EQ(kOwn, s.code);  // verifiable, named -- just not newer
  UR_EXPECT_FALSE(s.updateAvailable);
  UR_EXPECT_FALSE(s.installable);
}

UR_TEST(draftsAndPrereleasesAreSkippedEvenWhenTheyOutrank) {
  const std::string v = "2026.4.1-900000000";
  const std::string draft = "2026.4.2-900000002";
  const std::string pre = "2026.4.3-900000003";
  const auto releases = ParseReleases(nlohmann::json::array({
      ReleaseJson(pre, false, true, AllAssets(pre), kDigest),
      ReleaseJson(draft, true, false, AllAssets(draft), kDigest),
      ReleaseJson(v, false, false, AllAssets(v), kDigest),
  }));
  const Selection s = SelectRelease(releases, kOwn, InstallKind::AppImage, "amd64");
  // the developer line must not name the prerelease as "the newest release"
  UR_EXPECT_TRUE(s.newestVersion == v);
  UR_EXPECT_TRUE(s.version == v);
  UR_EXPECT_TRUE(s.updateAvailable);
  // the draft and the prerelease are neither offered nor logged as broken
  UR_EXPECT_TRUE(s.skipped.empty());

  // only drafts/prereleases: nothing at all
  const auto only = ParseReleases(nlohmann::json::array({
      ReleaseJson(pre, false, true, AllAssets(pre), kDigest),
      ReleaseJson(draft, true, false, AllAssets(draft), kDigest),
  }));
  const Selection none = SelectRelease(only, kOwn, InstallKind::AppImage, "amd64");
  UR_EXPECT_EQ(std::uint64_t{0}, none.newestCode);
  UR_EXPECT_FALSE(none.updateAvailable);
}

UR_TEST(aReleaseMissingTheOwnArchAssetIsNotOfferedAndFallsBackToAnOlderOne) {
  const std::string newer = "2026.4.2-900000002";
  const std::string older = "2026.4.1-900000001";
  // the newer release carries only the arm64 image
  const auto releases = ParseReleases(nlohmann::json::array({
      ReleaseJson(newer, false, false, {"URnetwork-" + newer + "-arm64.AppImage"}, kDigest),
      ReleaseJson(older, false, false, AllAssets(older), kDigest),
  }));
  const Selection s = SelectRelease(releases, kOwn, InstallKind::AppImage, "amd64");
  UR_EXPECT_TRUE(s.newestVersion == newer);  // the honest "something newer exists"
  UR_EXPECT_TRUE(s.version == older);        // ...and the one this build can verify
  UR_EXPECT_TRUE(s.updateAvailable);
  UR_EXPECT_TRUE(s.installable);
  UR_EXPECT_EQ(size_t{1}, s.skipped.size());
  UR_EXPECT_TRUE(s.skipped[0].tag == "v" + newer);
  UR_EXPECT_TRUE(s.skipped[0].reason == "lacks URnetwork-" + newer + "-amd64.AppImage");

  // with nothing older to fall back to, nothing is offered
  const auto alone = ParseReleases(nlohmann::json::array({
      ReleaseJson(newer, false, false, {"URnetwork-" + newer + "-arm64.AppImage"}, kDigest),
  }));
  const Selection n = SelectRelease(alone, kOwn, InstallKind::AppImage, "amd64");
  UR_EXPECT_TRUE(n.newestVersion == newer);
  UR_EXPECT_EQ(std::uint64_t{0}, n.code);
  UR_EXPECT_FALSE(n.updateAvailable);
  UR_EXPECT_FALSE(n.installable);
  UR_EXPECT_TRUE(n.assetUrl.empty());
}

UR_TEST(anAssetWithoutAUsableDigestIsNeverOffered) {
  const std::string v = "2026.4.1-900000000";
  for (const std::string& bad : {std::string(), std::string("md5:" + kHex),
                                 std::string("sha256:" + kHex.substr(0, 40)),
                                 std::string("sha256:not-hex")}) {
    const auto releases = ParseReleases(nlohmann::json::array({
        ReleaseJson(v, false, false, AllAssets(v), bad),
    }));
    const Selection s = SelectRelease(releases, kOwn, InstallKind::AppImage, "amd64");
    UR_EXPECT_TRUE_MSG("digest '" + bad + "' was offered", !s.updateAvailable);
    UR_EXPECT_FALSE(s.installable);
    UR_EXPECT_TRUE(s.assetUrl.empty());
    UR_EXPECT_TRUE(s.digestHex.empty());
    UR_EXPECT_EQ(size_t{1}, s.skipped.size());
    UR_EXPECT_TRUE(s.skipped[0].reason ==
                   "lacks a usable digest for URnetwork-" + v + "-amd64.AppImage");
  }
}

UR_TEST(anAssetHostedOutsideTheOfficialRepoIsRefused) {
  const std::string v = "2026.4.1-900000000";
  for (const char* host : {"https://github.com/urnetwork/build/releases/download/",
                           "https://github.com/someone/linux/releases/download/",
                           "https://example.com/"}) {
    const auto releases = ParseReleases(nlohmann::json::array({
        ReleaseJson(v, false, false, AllAssets(v), kDigest, host),
    }));
    const Selection s = SelectRelease(releases, kOwn, InstallKind::AppImage, "amd64");
    UR_EXPECT_TRUE_MSG(std::string("an asset on ") + host + " was offered",
                       !s.updateAvailable);
    UR_EXPECT_TRUE(s.assetUrl.empty());
    UR_EXPECT_EQ(size_t{1}, s.skipped.size());
    UR_EXPECT_TRUE(s.skipped[0].reason ==
                   "URnetwork-" + v + "-amd64.AppImage is not hosted by urnetwork/linux");
  }
}

UR_TEST(anAssetOnTheOfficialPathButNotItsOwnIsRefusedAndAnOlderOneOffered) {
  const std::string newer = "2026.4.2-900000002";
  const std::string older = "2026.4.1-900000001";
  const std::string name = "URnetwork-" + newer + "-amd64.AppImage";
  const std::string own = std::string(kOfficialDownload) + "v" + newer + "/" + name;
  for (const std::string& url :
       {std::string(kOfficialDownload) + "v" + older + "/" + name,  // another tag's path
        std::string(kOfficialDownload) + "v" + newer + "/URnetwork-" + newer +
            "-arm64.AppImage",  // another asset
        own + "?x", own + "/../x.AppImage"}) {
    nlohmann::json rel = ReleaseJson(newer, false, false, AllAssets(newer), kDigest);
    for (auto& asset : rel["assets"]) {
      if (asset["name"] == name) asset["browser_download_url"] = url;
    }
    const auto releases = ParseReleases(nlohmann::json::array({
        rel,
        ReleaseJson(older, false, false, AllAssets(older), kDigest),
    }));
    const Selection s = SelectRelease(releases, kOwn, InstallKind::AppImage, "amd64");
    UR_EXPECT_TRUE_MSG(url + " was offered", s.version == older);
    UR_EXPECT_TRUE(s.newestVersion == newer);
    UR_EXPECT_TRUE(s.assetUrl ==
                   std::string(kOfficialDownload) + "v" + older + "/URnetwork-" + older +
                       "-amd64.AppImage");
    UR_EXPECT_TRUE(s.updateAvailable);
    UR_EXPECT_EQ(size_t{1}, s.skipped.size());
    UR_EXPECT_TRUE(!s.skipped.empty() && s.skipped[0].tag == "v" + newer &&
                   s.skipped[0].reason == name + " is not hosted by urnetwork/linux");
  }
}

UR_TEST(aDevBuildIsToldAboutTheNewestReleaseButNeverOfferedIt) {
  const std::string v = "2026.4.1-900000000";
  const auto releases = ParseReleases(nlohmann::json::array({
      ReleaseJson(v, false, false, AllAssets(v), kDigest),
  }));
  const Selection s = SelectRelease(releases, /*ownCode=*/0, InstallKind::AppImage, "amd64");
  UR_EXPECT_TRUE(s.newestVersion == v);
  UR_EXPECT_TRUE(s.version == v);  // verifiable...
  UR_EXPECT_FALSE(s.updateAvailable);  // ...but never an update for a dev build
  UR_EXPECT_FALSE(s.installable);
}

UR_TEST(packagedInstallsAreToldTheFileAndNeverInstalled) {
  const std::string v = "2026.4.1-900000000";
  const auto releases = ParseReleases(nlohmann::json::array({
      ReleaseJson(v, false, false, AllAssets(v), kDigest),
  }));
  const struct {
    InstallKind kind;
    const char* arch;
    std::string asset;
  } rows[] = {
      {InstallKind::Deb, "amd64", "urnetwork-daemon_" + v + "_amd64.deb"},
      {InstallKind::Rpm, "arm64", "urnetwork-daemon-" + v + ".aarch64.rpm"},
      {InstallKind::Arch, "amd64", "urnetwork-daemon-" + v + "-x86_64.pkg.tar.zst"},
      {InstallKind::Tarball, "amd64", "urnetwork-daemon-" + v + "-amd64.install.tar.gz"},
      {InstallKind::Flatpak, "arm64", "URnetwork-" + v + "-arm64.flatpak"},
  };
  for (const auto& row : rows) {
    const Selection s = SelectRelease(releases, kOwn, row.kind, row.arch);
    UR_EXPECT_TRUE_MSG(std::string(InstallKindName(row.kind)) + " saw no update",
                       s.updateAvailable);
    UR_EXPECT_FALSE(s.installable);
    UR_EXPECT_TRUE(s.assetName == row.asset);
    UR_EXPECT_TRUE(s.releasePage == "https://github.com/urnetwork/linux/releases/tag/v" + v);
  }
  // the amd64 flatpak is not published today: no asset, no update offered
  const Selection fp = SelectRelease(releases, kOwn, InstallKind::Flatpak, "amd64");
  UR_EXPECT_FALSE(fp.updateAvailable);
  UR_EXPECT_TRUE(fp.newestVersion == v);
  // an install of unknown kind is told nothing it could act on
  const Selection unknown = SelectRelease(releases, kOwn, InstallKind::Unknown, "amd64");
  UR_EXPECT_FALSE(unknown.updateAvailable);
  UR_EXPECT_TRUE(unknown.newestVersion == v);
}

UR_TEST(theReleaseListSurvivesShapeDrift) {
  // an empty list (the stable repo before its first release) is no release
  UR_EXPECT_TRUE(ParseReleases(nlohmann::json::array()).empty());
  // a single object (the /releases/latest shape) parses too
  const std::string v = "2026.4.1-900000000";
  UR_EXPECT_EQ(size_t{1}, ParseReleases(ReleaseJson(v, false, false, AllAssets(v), kDigest)).size());
  // garbage parses to nothing, never throws
  UR_EXPECT_TRUE(ParseReleases(nlohmann::json("a string")).empty());
  UR_EXPECT_TRUE(ParseReleases(nlohmann::json(42)).empty());
  UR_EXPECT_TRUE(ParseReleases(nlohmann::json::array({1, "x", nullptr})).empty());
  // wrong-typed fields read as absent: a null tag is "not a release tag", a
  // string draft flag is "not a draft", a non-array assets is "no assets"
  nlohmann::json odd = nlohmann::json::object();
  odd["tag_name"] = nullptr;
  odd["draft"] = "yes";
  odd["assets"] = "none";
  const auto parsed = ParseReleases(nlohmann::json::array({odd}));
  UR_EXPECT_EQ(size_t{1}, parsed.size());
  UR_EXPECT_TRUE(parsed[0].tag.empty());
  UR_EXPECT_FALSE(parsed[0].draft);
  UR_EXPECT_TRUE(parsed[0].assets.empty());
  const Selection s = SelectRelease(parsed, kOwn, InstallKind::AppImage, "amd64");
  UR_EXPECT_EQ(std::uint64_t{0}, s.newestCode);
  UR_EXPECT_FALSE(s.updateAvailable);
}

// ---- the install kind -------------------------------------------------------

UR_TEST(installKindIsReadOffTheRuntime) {
  InstallProbe p;
  // the AppImage runtime exports both
  p.appimageEnv = "/home/u/.local/lib/urnetwork/URnetwork.AppImage";
  p.appdirEnv = "/tmp/.mount_URnetwXYZ";
  p.exePath = "/tmp/.mount_URnetwXYZ/usr/bin/urnetwork-gui";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::AppImage);
  // ...but a sandbox wins even with inherited variables
  p.flatpakInfo = true;
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Flatpak);
  // APPIMAGE without APPDIR is a stray variable, not an AppImage
  p = InstallProbe{};
  p.appimageEnv = "/x.AppImage";
  p.exePath = "/usr/bin/urnetwork-gui";
  p.osReleaseId = "ubuntu";
  p.osReleaseIdLike = "debian";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Deb);

  // a packaged binary takes the distro's family
  p = InstallProbe{};
  p.exePath = "/usr/bin/urnetwork-gui";
  p.osReleaseId = "fedora";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Rpm);
  p.osReleaseId = "bazzite";
  p.osReleaseIdLike = "fedora";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Rpm);
  p.osReleaseId = "opensuse-tumbleweed";
  p.osReleaseIdLike = "opensuse suse";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Rpm);
  p.osReleaseId = "cachyos";
  p.osReleaseIdLike = "arch";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Arch);
  p.osReleaseId = "debian";
  p.osReleaseIdLike = "";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Deb);
  // "archlinux" is not the word "arch"; an unknown family under /usr is the tarball's audience
  p.osReleaseId = "nixos";
  p.osReleaseIdLike = "";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Tarball);
  p.exePath = "/opt/urnetwork/urnetwork-gui";
  p.osReleaseId = "ubuntu";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Deb);

  // anywhere else on disk is a hand install
  p = InstallProbe{};
  p.exePath = "/usr/local/bin/urnetwork-gui";
  p.osReleaseId = "ubuntu";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Tarball);
  p.exePath = "/home/u/src/linux/app/build/urnetwork-gui";
  UR_EXPECT_TRUE(DetectInstallKind(p) == InstallKind::Tarball);
  // nothing known at all
  UR_EXPECT_TRUE(DetectInstallKind(InstallProbe{}) == InstallKind::Unknown);
}

UR_TEST(ownArchIsOneOfThePipelinesTwo) {
  const std::string arch = OwnArch();
  // this test runs on the developer's machine, which may be neither; the
  // name is then empty, which AssetNameFor turns into "no asset, no offer"
  UR_EXPECT_TRUE(arch.empty() || arch == "amd64" || arch == "arm64");
}

// ---- the launch throttle ----------------------------------------------------

UR_TEST(launchChecksAreThrottledToOncePerSixHours) {
  constexpr std::int64_t now = 1'800'000'000;
  UR_EXPECT_EQ(std::int64_t{6 * 60 * 60}, kCheckIntervalSeconds);
  UR_EXPECT_TRUE(ShouldCheckNow(true, kOwn, 0, now));                            // never checked
  UR_EXPECT_FALSE(ShouldCheckNow(true, kOwn, now - 60, now));                    // a minute ago
  UR_EXPECT_FALSE(ShouldCheckNow(true, kOwn, now - kCheckIntervalSeconds + 1, now));
  UR_EXPECT_TRUE(ShouldCheckNow(true, kOwn, now - kCheckIntervalSeconds, now));  // exactly due
  UR_EXPECT_TRUE(ShouldCheckNow(true, kOwn, now + 3600, now));                   // clock went back
  UR_EXPECT_FALSE(ShouldCheckNow(false, kOwn, 0, now));                          // the setting
  UR_EXPECT_FALSE(ShouldCheckNow(true, 0, 0, now));                              // a dev build
}

// ---- the swap paths ---------------------------------------------------------

UR_TEST(theDownloadIsADotFileBesideTheImageAndTheBackupIsNamedAfterIt) {
  UR_EXPECT_TRUE(PartPath("/home/u/.local/lib/urnetwork/URnetwork.AppImage") ==
                 "/home/u/.local/lib/urnetwork/.URnetwork.AppImage.part");
  // ~/Applications/URnetwork*.AppImage (the launcher's glob) must not match it
  UR_EXPECT_TRUE(PartPath("/home/u/Applications/URnetwork-2026.3.23-895075980-amd64.AppImage") ==
                 "/home/u/Applications/.URnetwork-2026.3.23-895075980-amd64.AppImage.part");
  UR_EXPECT_TRUE(PartPath("URnetwork.AppImage") == ".URnetwork.AppImage.part");
  UR_EXPECT_TRUE(BackupPath("/home/u/.local/lib/urnetwork/URnetwork.AppImage") ==
                 "/home/u/.local/lib/urnetwork/URnetwork.AppImage.bak");
}

// ---- the relaunch environment -----------------------------------------------

UR_TEST(relaunchDropsEverythingThatNamesTheOldMount) {
  const std::string mount = "/tmp/.mount_URnetwABC";
  const EnvList env = {
      {"HOME", "/home/u"},
      {"APPDIR", mount},
      {"APPIMAGE", "/home/u/.local/lib/urnetwork/URnetwork.AppImage"},
      {"OWD", "/home/u"},
      {"ARGV0", "URnetwork.AppImage"},
      {"LD_LIBRARY_PATH", mount + "/usr/lib:" + mount + "/usr/lib/urnetwork:/opt/cuda/lib"},
      {"XDG_DATA_DIRS", mount + "/usr/share:/usr/local/share:/usr/share"},
      {"GSETTINGS_SCHEMA_DIR", mount + "/usr/share/glib-2.0/schemas"},
      {"GIO_MODULE_DIR", mount + "/usr/lib/gio/modules"},
      {"GDK_PIXBUF_MODULE_FILE", "/home/u/.cache/urnetwork/gdk-pixbuf-loaders.cache"},
      {"LANG", "de_DE.UTF-8"},
  };
  const EnvList out = ScrubRelaunchEnv(env, mount);
  const auto get = [&out](const std::string& name) -> std::string {
    for (const auto& [k, v] : out) {
      if (k == name) return v;
    }
    return "<absent>";
  };
  UR_EXPECT_TRUE(get("HOME") == "/home/u");
  UR_EXPECT_TRUE(get("LANG") == "de_DE.UTF-8");
  UR_EXPECT_TRUE(get("APPDIR") == "<absent>");
  UR_EXPECT_TRUE(get("APPIMAGE") == "<absent>");
  UR_EXPECT_TRUE(get("OWD") == "<absent>");
  UR_EXPECT_TRUE(get("ARGV0") == "<absent>");
  UR_EXPECT_TRUE(get("LD_LIBRARY_PATH") == "/opt/cuda/lib");  // the foreign entry survives
  UR_EXPECT_TRUE(get("XDG_DATA_DIRS") == "/usr/local/share:/usr/share");
  UR_EXPECT_TRUE(get("GSETTINGS_SCHEMA_DIR") == "<absent>");
  UR_EXPECT_TRUE(get("GIO_MODULE_DIR") == "<absent>");
  UR_EXPECT_TRUE(get("GDK_PIXBUF_MODULE_FILE") == "<absent>");

  // a list made only of mount entries disappears rather than becoming ""
  const EnvList onlyMount = {{"LD_LIBRARY_PATH", mount + "/usr/lib"}};
  UR_EXPECT_TRUE(ScrubRelaunchEnv(onlyMount, mount).empty());
  // with no mount known, only the runtime's own four go
  const EnvList plain = {{"APPIMAGE", "/x"}, {"LD_LIBRARY_PATH", "/a:/b"}};
  const EnvList kept = ScrubRelaunchEnv(plain, "");
  UR_EXPECT_EQ(size_t{1}, kept.size());
  UR_EXPECT_TRUE(kept[0].first == "LD_LIBRARY_PATH" && kept[0].second == "/a:/b");
}
