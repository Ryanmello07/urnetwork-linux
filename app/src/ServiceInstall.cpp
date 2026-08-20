// SPDX-License-Identifier: MPL-2.0
#include "ServiceInstall.hpp"

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace urnw::svc {
namespace {

std::string Unquote(std::string v) {
  if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) {
    v = v.substr(1, v.size() - 2);
  }
  return v;
}

// os-release is shell-ish: KEY=value, value optionally quoted, ID_LIKE a
// space-separated list. Parsed by hand rather than sourced, because sourcing a
// file from the host inside our own process is a code-execution path.
std::string Field(const std::string& text, const std::string& key) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind(key + "=", 0) == 0) return Unquote(line.substr(key.size() + 1));
  }
  return {};
}

Family FamilyFromId(const std::string& id) {
  if (id == "debian" || id == "ubuntu" || id == "linuxmint" || id == "pop" ||
      id == "elementary" || id == "zorin" || id == "raspbian") {
    return Family::Debian;
  }
  if (id == "fedora" || id == "rhel" || id == "centos" || id == "rocky" ||
      id == "almalinux" || id == "bazzite" || id == "silverblue" || id == "kinoite") {
    return Family::Fedora;
  }
  if (id == "arch" || id == "cachyos" || id == "endeavouros" || id == "manjaro" ||
      id == "steamos") {
    return Family::Arch;
  }
  if (id == "opensuse" || id == "opensuse-tumbleweed" || id == "opensuse-leap" ||
      id == "sles") {
    return Family::Suse;
  }
  return Family::Unknown;
}

}  // namespace

HostInfo DetectHost() {
  HostInfo h;
  // The HOST's file. Inside a Flatpak the sandbox's own /etc/os-release
  // describes the runtime (ID=org.gnome.Platform) and would send every user
  // down the Unknown path.
  // TWO LAYOUTS, and they are not interchangeable. A Flatpak sees the host's
  // file FLAT at /run/host/os-release; a native install reads /etc/os-release.
  // Each is paired with the root its ostree marker would live under, so the
  // distribution and the immutability answer can never come from different
  // machines — an earlier version read "ubuntu" from a container while finding
  // the Bazzite host's marker underneath, and offered a .deb for a host that
  // needs rpm-ostree.
  struct Source { const char* osRelease; const char* markerRoot; };
  static const Source kSources[] = {
      {"/run/host/os-release", "/run/host"},  // inside a Flatpak
      {"/etc/os-release", ""},                // native
  };
  std::string text;
  std::string markerRoot;
  for (const Source& src : kSources) {
    std::ifstream in(src.osRelease);
    if (!in) continue;
    std::stringstream ss;
    ss << in.rdbuf();
    if (ss.str().empty()) continue;
    text = ss.str();
    markerRoot = src.markerRoot;
    break;
  }
  if (text.empty()) return h;

  h.id = Field(text, "ID");
  h.prettyName = Field(text, "PRETTY_NAME");
  h.family = FamilyFromId(h.id);

  // Derivatives that name a parent inherit its answer. Checked ONLY when the
  // ID itself was not recognised, so a derivative we do know (bazzite ->
  // Fedora) is never overridden by a broader ID_LIKE entry.
  if (h.family == Family::Unknown) {
    std::istringstream likes(Field(text, "ID_LIKE"));
    std::string like;
    while (likes >> like) {
      const Family f = FamilyFromId(like);
      if (f != Family::Unknown) {
        h.family = f;
        break;
      }
    }
  }

  // ostree/bootc: /usr is a read-only image, so a native package must go
  // through rpm-ostree and the tarball installer takes its /usr/local path.
  // THE MARKER IS NOT ALWAYS REACHABLE. Measured in our own bundle: a Flatpak
  // can read the host's os-release but sees no /run/ostree-booted under any
  // path, so a marker-only test would call every immutable host mutable and
  // hand a Silverblue user a dnf command that cannot work. Identity is the
  // fallback: these families are ostree/bootc by construction, and Fedora's
  // own immutable variants keep ID=fedora and distinguish themselves with
  // VARIANT_ID.
  const std::string variant = Field(text, "VARIANT_ID");
  const auto isImmutableName = [](const std::string& n) {
    return n == "bazzite" || n == "silverblue" || n == "kinoite" || n == "sericea" ||
           n == "onyx" || n == "steamos" || n == "bluefin" || n == "aurora";
  };
  h.immutable = ::access((markerRoot + std::string("/run/ostree-booted")).c_str(), F_OK) == 0 ||
                isImmutableName(h.id) || isImmutableName(variant);
  return h;
}

namespace {

bool Has(const std::vector<std::string>& assets, const std::string& suffix) {
  return std::any_of(assets.begin(), assets.end(), [&](const std::string& a) {
    return a.size() >= suffix.size() &&
           a.compare(a.size() - suffix.size(), suffix.size(), suffix) == 0;
  });
}

}  // namespace

Offer OfferFor(const HostInfo& host, const std::vector<std::string>& available,
               const std::string& downloadUrl) {
  Offer o;
  const bool haveDeb = Has(available, "_amd64.deb");
  const bool haveRpm = Has(available, ".x86_64.rpm");
  const bool haveTar = Has(available, "-amd64.install.tar.gz");

  // The tarball is the fallback for every family, and the FIRST choice on an
  // immutable host: it detects the read-only /usr, installs under /usr/local,
  // and builds the SELinux policy module that Fedora-family hosts need before
  // the daemon can open /dev/net/tun.
  const auto tarball = [&]() {
    o.format = haveTar ? Format::Tarball : Format::None;
    o.command = "curl -L " + downloadUrl + " | tar xz && sudo ./urnetwork-daemon/install.sh";
    if (host.immutable) {
      o.note = "Your system has a read-only /usr, so the installer places the "
               "service under /usr/local.";
    }
    return o;
  };

  switch (host.family) {
    case Family::Debian:
      if (haveDeb) {
        o.format = Format::Deb;
        o.assetSuffix = "_amd64.deb";
        o.command = "sudo apt install ./urnetwork-daemon.deb";
        return o;
      }
      return tarball();

    case Family::Fedora:
    case Family::Suse:
      if (host.immutable && haveRpm) {
        o.format = Format::Rpm;
        o.assetSuffix = ".x86_64.rpm";
        o.command = "sudo rpm-ostree install ./urnetwork-daemon.rpm";
        o.note = "rpm-ostree layers the service into your next boot; reboot to finish.";
        return o;
      }
      if (haveRpm) {
        o.format = Format::Rpm;
        o.assetSuffix = ".x86_64.rpm";
        o.command = (host.family == Family::Suse ? "sudo zypper install ./urnetwork-daemon.rpm"
                                                 : "sudo dnf install ./urnetwork-daemon.rpm");
        return o;
      }
      // NO RPM ON THIS CHANNEL. urnetwork/build does not publish one yet, so an
      // upstream-channel Fedora user lands here. The tarball genuinely works —
      // including the SELinux module — so offer it rather than a dnf line that
      // would 404.
      return tarball();

    case Family::Immutable:
    case Family::Arch:
    case Family::Unknown:
    default:
      return tarball();
  }
}

}  // namespace urnw::svc
