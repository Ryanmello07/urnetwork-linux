# The Linux insufficient-balance driver's runtime image: the existing GUI
# build image (Ubuntu 24.04, the AppImage's ABI floor, already carrying Xvfb,
# dbus-x11 and the GTK runtime) plus what a headless user session needs to be
# driven through accessibility:
# - at-spi2-core, python3-gi, gir1.2-atspi-2.0: the AT-SPI bus, registry and
#   the client library atspi.py uses
# - gnome-keyring: the session's Secret Service, where the GUI keeps its RPC
#   client credentials (a required boundary, never a plaintext fallback)
# - xdotool: the window origin for focusing an entry by its extents
# - curl: the non-app egress probe and bounded traffic
# - iproute2, nftables, libfuse2t64: the daemon package's dependencies, so the
#   locally built .deb installs offline with dpkg -i
#
# polkit is deliberately absent: the daemon then authorizes the GUI user by the
# documented `urnetwork` group fallback, which needs no system bus or seat.
#
# SPDX-License-Identifier: MPL-2.0
ARG BASE=urnetwork-linux-builder-gui:arm64
FROM ${BASE}

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      at-spi2-core \
      python3-gi \
      gir1.2-atspi-2.0 \
      gir1.2-glib-2.0 \
      dbus \
      dbus-x11 \
      xvfb \
      xdotool \
      gnome-keyring \
      curl \
      ca-certificates \
      iproute2 \
      nftables \
      libfuse2t64 \
    && rm -rf /var/lib/apt/lists/* \
    && if [ -e /usr/bin/pkcheck ] || [ -e /usr/lib/polkit-1/polkitd ]; then \
         echo "polkit must not be installed in the driver image" >&2; exit 1; \
       fi
