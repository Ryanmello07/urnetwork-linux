// Which install command each distribution family is offered, given the assets a
// channel actually publishes.
//
// This is worth a test for one reason above the others: OfferFor's answer
// depends on TWO independent facts about the host -- its family and whether its
// /usr is a read-only image -- and the two combine DIFFERENTLY per family.
// Fedora + immutable still gets its native package (rpm-ostree layers one);
// Arch + immutable (SteamOS) must NOT, because pacman there needs
// `steamos-readonly disable` and the next system update reverts the whole /usr
// partition. That asymmetry is invisible in either fact alone, and it is the
// kind of thing a refactor silently flattens.
//
// The asset names below are the real ones the packaging scripts emit --
// packaging/make-deb.sh, make-rpm.sh, make-arch.sh and make-install-tarball.sh
// -- so a rename in any of them fails here rather than in a user's hands.
//
// SPDX-License-Identifier: MPL-2.0
#include "ServiceInstall.hpp"

#include "TestHarness.hpp"

using namespace urnw::svc;

namespace {

// One release of the fork's beta line, with every daemon asset attached.
std::vector<std::string> FullRelease() {
  return {
      "urnetwork-daemon-2026.8.20-1024376890-beta-amd64.install.tar.gz",
      "urnetwork-daemon_2026.8.20-1024376890-beta_amd64.deb",
      "urnetwork-daemon-2026.8.20-1024376890-beta.x86_64.rpm",
      "urnetwork-daemon-2026.8.20-1024376890-beta-x86_64.pkg.tar.zst",
      "URnetwork-2026.8.20-1024376890-beta-amd64.AppImage",
  };
}

// urnetwork/build, the upstream channel: no native Linux packages at all.
std::vector<std::string> TarballOnly() {
  return {"urnetwork-daemon-2026.8.20-1024376890-beta-amd64.install.tar.gz"};
}

HostInfo Host(Family f, bool immutable = false, const char* id = "") {
  HostInfo h;
  h.family = f;
  h.immutable = immutable;
  h.id = id;
  return h;
}

const char* kUrl = "https://example.invalid/urnetwork-daemon.tar.gz";

}  // namespace

UR_TEST(ServiceInstall_ArchGetsThePacmanPackage) {
  const Offer o = OfferFor(Host(Family::Arch, false, "cachyos"), FullRelease(), kUrl);
  UR_EXPECT_TRUE(o.format == Format::ArchPkg);
  UR_EXPECT_TRUE(o.assetSuffix == "-x86_64.pkg.tar.zst");
  UR_EXPECT_TRUE(o.command == "sudo pacman -U ./urnetwork-daemon.pkg.tar.zst");
  // The suffix must actually select an asset in the release it was derived
  // from; a suffix that matches nothing is a download that 404s.
  bool matched = false;
  for (const std::string& a : FullRelease()) {
    if (a.size() >= o.assetSuffix.size() &&
        a.compare(a.size() - o.assetSuffix.size(), o.assetSuffix.size(), o.assetSuffix) == 0) {
      matched = true;
    }
  }
  UR_EXPECT_TRUE(matched);
}

UR_TEST(ServiceInstall_ArchWithoutAPackageFallsBackToTheTarball) {
  // The upstream channel publishes no .pkg.tar.zst. Offering `pacman -U` there
  // would print a command for a file that does not exist.
  const Offer o = OfferFor(Host(Family::Arch, false, "arch"), TarballOnly(), kUrl);
  UR_EXPECT_TRUE(o.format == Format::Tarball);
  UR_EXPECT_TRUE(o.assetSuffix.empty());
}

UR_TEST(ServiceInstall_SteamOsGetsTheTarballEvenWhenThePackageExists) {
  // THE ASYMMETRY WITH FEDORA. SteamOS is Arch-derived AND immutable; pacman
  // there needs `steamos-readonly disable` and the next SteamOS update reverts
  // /usr, taking the package with it. The tarball installs under /usr/local.
  const Offer o = OfferFor(Host(Family::Arch, /*immutable=*/true, "steamos"), FullRelease(), kUrl);
  UR_EXPECT_TRUE(o.format == Format::Tarball);
  UR_EXPECT_TRUE(!o.note.empty());
}

UR_TEST(ServiceInstall_ImmutableFedoraStillGetsItsRpm) {
  // The other half of the asymmetry, asserted so nobody "simplifies" the Arch
  // branch by making every immutable host take the tarball.
  const Offer o = OfferFor(Host(Family::Fedora, /*immutable=*/true, "bazzite"), FullRelease(), kUrl);
  UR_EXPECT_TRUE(o.format == Format::Rpm);
  UR_EXPECT_TRUE(o.command == "sudo rpm-ostree install ./urnetwork-daemon.rpm");
}

UR_TEST(ServiceInstall_TheOtherFamiliesAreUnchanged) {
  const Offer deb = OfferFor(Host(Family::Debian, false, "ubuntu"), FullRelease(), kUrl);
  UR_EXPECT_TRUE(deb.format == Format::Deb);
  UR_EXPECT_TRUE(deb.command == "sudo apt install ./urnetwork-daemon.deb");

  const Offer rpm = OfferFor(Host(Family::Fedora, false, "fedora"), FullRelease(), kUrl);
  UR_EXPECT_TRUE(rpm.format == Format::Rpm);
  UR_EXPECT_TRUE(rpm.command == "sudo dnf install ./urnetwork-daemon.rpm");

  const Offer suse = OfferFor(Host(Family::Suse, false, "opensuse-tumbleweed"), FullRelease(), kUrl);
  UR_EXPECT_TRUE(suse.format == Format::Rpm);
  UR_EXPECT_TRUE(suse.command == "sudo zypper install ./urnetwork-daemon.rpm");

  // An unknown distribution, and an Arch package sitting right there, must not
  // tempt anyone into a pacman command.
  const Offer unknown = OfferFor(Host(Family::Unknown, false, "void"), FullRelease(), kUrl);
  UR_EXPECT_TRUE(unknown.format == Format::Tarball);
}

UR_TEST(ServiceInstall_AReleaseWithNoDaemonAssetOffersNothing) {
  // Format::None is the signal the caller needs to say "no artifact for your
  // system on this channel" instead of printing a command that cannot work.
  const std::vector<std::string> guiOnly = {"URnetwork-2026.8.20-1024376890-beta-amd64.AppImage"};
  UR_EXPECT_TRUE(OfferFor(Host(Family::Arch, false, "arch"), guiOnly, kUrl).format == Format::None);
  UR_EXPECT_TRUE(OfferFor(Host(Family::Debian, false, "debian"), guiOnly, kUrl).format == Format::None);
}
