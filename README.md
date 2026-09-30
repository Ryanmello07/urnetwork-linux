# URnetwork for Linux

Native Linux desktop client for amd64 and arm64. It is built from two
programs: a privileged daemon, `urnetworkd`, and an unprivileged GTK4 GUI,
`urnetwork-gui`. Releases are attached to the GitHub release on
[urnetwork/build](https://github.com/urnetwork/build/releases). There is no
store listing. The Snap was dropped on 2026-08-05 in favour of native packages
plus an AppImage (see `APPIMAGE.md`, `MIGRATION.md` and `PLAN.md` §4, M4).

## Architecture

The GUI and the daemon are separate processes, which mirrors the Windows
app's service split. `MIGRATION.md` is the normative contract for paths,
binary names and artifact names.

```
  unprivileged desktop user              root, systemd (urnetworkd.service)
  urnetwork-gui                          urnetworkd
    gtkmm4 + libadwaita, SNI tray          no GUI dependencies
    urnet::DeviceRemote ── device RPC ──▶  urnet::DeviceLocal (rpc=true)
                   (127.0.0.1:12025, mTLS, while the tunnel is up)
    control client ──── unix socket ────▶  control server
                   (/run/urnetwork/control.sock, SO_PEERCRED)
                                           Tunnel: /dev/net/tun, ip routes and
                                             rules, resolvectl DNS, nftables
                                           urnet::newIoLoop(device, tunFd)
                                           GeoClue /etc/geolocation write
```

- The **daemon** is started by systemd (`Type=notify`). It opens and configures
  the tunnel and holds the SDK's `DeviceLocal`. It keeps its own traffic out of
  the tunnel with a cgroup-BPF socket mark plus nftables, and it refuses to
  start a tunnel when that cannot be proven. It depends only on gio/glib,
  nlohmann_json, threads and the SDK. It is built on Ubuntu 22.04, so it runs
  on glibc 2.35 or newer.
- The **GUI** does the sign-in, the UI and the tray. It has no privilege and
  reaches the daemon over the control socket. Tunnel control is authorized by
  polkit (`packaging/polkit/com.bringyour.network.policy`), or by membership in
  the `urnetwork` group on hosts without polkit.
- `/usr/bin/urnetwork` is a launcher script installed by the daemon package.
  It is the `Exec=` target of the desktop entry, the `urnetwork://` scheme
  handler and autostart. It runs the first GUI it finds, in this order:
  `$URNETWORK_APPIMAGE`, `~/.local/lib/urnetwork/URnetwork.AppImage`,
  `~/Applications/URnetwork*.AppImage`, `/usr/lib/urnetwork/URnetwork.AppImage`,
  `urnetwork-gui` on `$PATH`, then the `com.bringyour.network` Flatpak.

## Stack

**C++17 + GTK4 (gtkmm-4.0) + libadwaita**, on top of the shared **cgo SDK**
(`libURnetworkSdk.so` plus the header-only `urnetwork_sdk.hpp` C++ wrapper).
The Windows app uses the same SDK surface. Build: meson + ninja.

## Features

- Sign in with Google or Apple (in the system browser, returning through
  `urnetwork://`), email or phone and password, a seed phrase, an auth code, a Bittensor or Solana wallet, or an instant (guest)
  account. Also sign-up, verification, password reset, and upgrading a guest
  account.
- Connect: best available or a chosen location or provider, a globe view,
  connection details (contracts, transport, DNS, split rules) and a kill switch.
- Provide, extender share and import (QR), and location override (GeoClue).
- Account, earnings, points and leaderboards, the Solana payout wallet,
  referrals, redeem codes, upgrade (Stripe checkout), support, settings and
  licenses.
- Tray icon (StatusNotifierItem + `com.canonical.dbusmenu` over raw GDBus),
  hide-to-tray, and autostart.
- Localized with gettext. `app/po/` is generated from the shared localization
  store and is never edited by hand.

## Layout

| Path | What |
|---|---|
| `app/src/` | GUI sources (`main.cpp`, `MainWindow`, `HomeShell`, the pages and sheets, `SdkHost`, `ControlClient`, `Tray`, …) |
| `app/src/daemon/` | `urnetworkd`: `main.cpp`, `ControlServer`, `TunnelHost`, `DaemonLog`, `HostMemory` |
| `app/src/Tunnel.{hpp,cpp}` | `/dev/net/tun`, routes, nftables and `resolvectl` configuration (used by the daemon) |
| `app/tests/` | pure-logic unit tests (no GTK, no SDK), run by `meson test` |
| `app/meson.build`, `app/meson_options.txt` | build; this is the single source of truth for the installed layout |
| `app/packaging/` | desktop entry (+ `urnetwork://` scheme), AppStream metainfo, icons, systemd unit, launcher, autostart template, NetworkManager and udev files |
| `app/po/` | generated gettext catalogs |
| `app/scripts/fetch-deps.sh` | vendors the cgo SDK into `app/third_party/urnetwork-sdk/<arch>/` |
| `app/snap/snapcraft.yaml` | **superseded**, not built or shipped. Kept only while "Snap as a secondary channel" is an open question |
| `packaging/make-*.sh` | artifact builders: `deb`, `rpm`, `arch`, `install-tarball`, `appimage`, `flatpak` |
| `packaging/{deb,rpm,arch}/` | nfpm configs and maintainer scripts |
| `packaging/tarball/` | `install.sh` / `uninstall.sh` for distros without a native package |
| `packaging/flatpak/` | Flatpak manifest (GUI only) |
| `packaging/polkit/`, `packaging/selinux/` | polkit actions and SELinux policy module |
| `build.sh` | local smoketest of the full release build |
| `docs/` | distro support matrix, testing guides, parity notes |

## Build (local dev, on Linux)

```bash
cd app

# 1. vendor the SDK (from a local zip or SDK_ZIP_URL); produces
#    third_party/urnetwork-sdk/{amd64,arm64}/{libURnetworkSdk.so,urnetwork_sdk.hpp}
scripts/fetch-deps.sh /path/to/URnetworkSdkLinux.zip

# 2. configure, build, test
meson setup build -Dsdk_arch=$(dpkg --print-architecture)
meson compile -C build     # build/urnetwork-gui and build/urnetworkd
meson test -C build
```

`URnetworkSdkLinux-<version>.zip` is attached to every release on
urnetwork/build. You can also build it from `sdk/cgo` (`make build_linux`).

GUI dev dependencies (Ubuntu 24.04, the oldest Ubuntu that packages GTK4 for
C++): `g++ meson ninja-build pkg-config gettext libgtkmm-4.0-dev
libadwaita-1-dev libsecret-1-dev libzxing-dev libglib2.0-dev
nlohmann-json3-dev`. `libzxing-dev` (zxing-cpp 2.1 or newer) decodes the
extender share QR from an image file; the encoder is vendored in
`app/third_party/qrcodegen`. `libwebkitgtk-6.0-dev` is optional: with it the
upgrade sheet embeds the Stripe checkout, and without it the checkout opens in
the system browser.

The daemon alone builds without any GTK. With the default `-Dgui=auto`, a
machine without gtkmm/libadwaita builds only `urnetworkd`, which needs
`g++ meson ninja-build pkg-config gettext libglib2.0-dev nlohmann-json3-dev`.
Pass `-Dgui=enabled` to make a missing toolkit an error. Other options are in
`app/meson_options.txt` (`app_version`, `glibc_floor`,
`walletconnect_project_id`, and `host_integration=false` for the GUI-only
Flatpak).

The GUI does not run a tunnel itself. To try a dev build, install a released
daemon package (or run `packaging/tarball/install.sh`), then start the GUI
with the vendored SDK on the library path:
`LD_LIBRARY_PATH=third_party/urnetwork-sdk/$(dpkg --print-architecture) ./build/urnetwork-gui`. The GUI reports when the daemon is unreachable or out
of date. For host checks, run `sudo urnetworkd --diagnose` and
`sudo urnetworkd --selftest-egress`. `sudo urnetworkd --revert` lifts the
firewall and routes if a machine is left blocked.

## Release artifacts

`build.sh` runs the same build as the release pipeline
(`build/all/build-linux.sh`, driven by `build/all/run.sh`) on a macOS host.
First the cgo SDK is cross-built with zig. Then, per arch, one Docker image
builds the daemon packages on Ubuntu 22.04 (glibc 2.35 floor) and another
builds the GUI AppImage on 24.04. `build/all/linux/verify.sh` checks the
artifacts in the container. The Flatpak is built afterwards in its own
container (`build/all/linux/build-flatpak.sh`).

Per release (names are normative, see `MIGRATION.md`):

| Artifact | For |
|---|---|
| `urnetwork-daemon_<v>_<amd64\|arm64>.deb` | daemon: Debian, Ubuntu and derivatives |
| `urnetwork-daemon-<v>.<x86_64\|aarch64>.rpm` | daemon: Fedora, RHEL, openSUSE |
| `urnetwork-daemon-<v>-<x86_64\|aarch64>.pkg.tar.zst` | daemon: Arch, CachyOS, EndeavourOS, Manjaro |
| `urnetwork-daemon-<v>-<amd64\|arm64>.install.tar.gz` | daemon: any systemd distro without one of the above (e.g. immutable or read-only `/usr` hosts) |
| `URnetwork-<v>-<amd64\|arm64>.AppImage` (+ `.zsync`) | GUI, every distro |
| `URnetwork-<v>-<arch>.flatpak` | GUI, Flatpak. Built for the build host's architecture only (currently arm64) |

You need a daemon package and one GUI. Publishing to an apt/dnf repository and
self-hosting the AppImage zsync update channel are still manual follow-ups;
today everything ships from the GitHub release.

## Supported distros

The daemon needs systemd, glibc 2.35 or newer, cgroup v2, cgroup-BPF,
`nft`, `ip` and `/dev/net/tun`. systemd-resolved is needed for tunnel DNS and
for the kill switch. The released AppImage takes glibc and libstdc++ from the
host and is gated at glibc 2.39 (it is built on Ubuntu 24.04). The tray needs a
desktop with StatusNotifierItem support. The measured host is Bazzite (Fedora Silverblue base). Other
distros are packaged but, per distro, only inferred to work.
`docs/DISTRO-SUPPORT.md` lists every host assumption and how to test it.
`docs/TESTING-CACHYOS.md` is a step-by-step test guide for Arch-based systems.
