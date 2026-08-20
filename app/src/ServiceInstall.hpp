// What to tell a user whose urnetworkd is missing or out of date, and how to
// tell them the truth for THEIR distribution.
//
// WHY THERE IS NO INSTALL BUTTON. A sandboxed app cannot install a system
// service, and it should not be able to. Reaching the host would need
// --talk-name=org.freedesktop.Flatpak, i.e. permission to run arbitrary
// commands as the user on their host: Flathub's linter flags it and reviewers
// reject it, correctly. Measured in our own bundle:
//     flatpak-spawn --host true
//     Portal call failed: org.freedesktop.DBus.Error.ServiceUnknown
// So the honest maximum is: detect the distribution, hand over the exact
// command, and make it one click to copy. That is most of the value of a
// button without asking for a permission that would cost us the listing.
//
// WHAT WE CAN SEE FROM INSIDE THE SANDBOX, verified rather than assumed:
//   /run/host/os-release      the HOST's distribution (the sandbox's own
//                             /etc/os-release says ID=org.gnome.Platform,
//                             which is the runtime and useless here)
//   /run/urnetwork/control.sock  whether the service is installed at all
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

namespace urnw::svc {

// How the service is installed on this host family. Chosen from os-release ID
// then ID_LIKE, so derivatives inherit their parent's answer (Bazzite -> fedora,
// Mint -> ubuntu/debian, CachyOS/EndeavourOS -> arch).
enum class Family {
  Debian,     // .deb
  Fedora,     // .rpm, and rpm-ostree on the immutable variants
  Immutable,  // ostree/bootc: rpm-ostree, or the tarball which handles /usr ro
  Arch,       // no native package: tarball
  Suse,       // .rpm
  Unknown,    // tarball
};

struct HostInfo {
  Family family = Family::Unknown;
  std::string id;          // os-release ID, verbatim, for the log line
  std::string prettyName;  // os-release PRETTY_NAME, for the notice
  bool immutable = false;  // /usr is a read-only ostree/bootc image
};

// Reads /run/host/os-release when sandboxed, /etc/os-release otherwise.
HostInfo DetectHost();

// The artifact this family should be offered, given the release assets a
// channel actually publishes. RETURNS AN EMPTY FORMAT when the selected
// channel has no artifact for this family — the caller must then offer the
// portable installer rather than print a command that 404s. urnetwork/build
// publishes no .rpm today, so a Fedora user on the upstream channel takes
// exactly that path.
enum class Format { Deb, Rpm, Tarball, None };

struct Offer {
  Format format = Format::None;
  std::string assetSuffix;  // matched against a release asset name
  std::string command;      // exactly what the user should run
  std::string note;         // one line, only when something is not obvious
};

// available: asset names from the selected channel's latest release.
Offer OfferFor(const HostInfo& host, const std::vector<std::string>& available,
               const std::string& downloadUrl);

}  // namespace urnw::svc
