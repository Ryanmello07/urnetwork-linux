#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# The in-container half of the Linux insufficient-balance driver (main.go owns
# the decisions). Runs as root inside the disposable driver container:
#
#   start          install the .deb, run urnetworkd in a child cgroup (as
#                  build/all/acceptance/run-linux.sh does), then a user session:
#                  Xvfb, session bus, Secret Service, the fake notification
#                  server and the GUI AppImage, and wait for the GUI's AT-SPI
#                  application
#   egress         one public-address probe from this (unmarked) cgroup:
#                  "ip <addr>" or "error <why>", always exit 0
#   traffic        bounded HTTPS requests through the system path
#   tunnel         "up" when the urnet0 tunnel link exists, else "down"
#   notifications  the notification server's JSON-lines log
#   atspi ...      atspi.py as the GUI user, in its session
set -euo pipefail
umask 077

run=/run/ib
user=urtester
here="$(cd "$(dirname "$0")" && pwd)"
probe_urls=(https://checkip.amazonaws.com/ https://api.ipify.org/)

as_user() {
  # session.env holds only fixed, space-free values written by start
  # shellcheck disable=SC2046
  runuser -u "$user" -- env $(cat "$run/session.env") "$@"
}

wait_for() {
  local what="$1" tries="$2"; shift 2
  for _ in $(seq 1 "$tries"); do
    if "$@" >/dev/null 2>&1; then return 0; fi
    sleep 0.5
  done
  echo "timed out waiting for $what" >&2
  return 1
}

start() {
  : "${UR_IB_DEB:?}" "${UR_IB_APPIMAGE:?}" "${UR_IB_VERSION:?}"
  mkdir -p "$run" /artifacts
  chmod 755 "$run"
  dpkg -i "$UR_IB_DEB" >/artifacts/install.log 2>&1
  [ "$(dpkg-query -W -f='${Version}' urnetwork-daemon)" = "$UR_IB_VERSION" ] || {
    echo "installed daemon version does not match $UR_IB_VERSION" >&2; exit 1; }
  getent group urnetwork >/dev/null || { echo "the package did not create the urnetwork group" >&2; exit 1; }
  id "$user" >/dev/null 2>&1 || useradd -m -G urnetwork "$user"
  install -o "$user" -m 0400 /opt/ib-private/credentials "$run/credentials"

  # The daemon's cgroup-BPF mark covers its whole cgroup: run only it in a
  # child, so the GUI and every probe stay unmarked and must use the tunnel.
  local container_cgroup daemon_cgroup
  container_cgroup="$(sed -n 's/^0::\///p' /proc/self/cgroup)"
  [ -n "$container_cgroup" ] || { echo "the container has no named cgroup-v2 path" >&2; exit 1; }
  daemon_cgroup="/sys/fs/cgroup/$container_cgroup/urnetworkd-ib-$$"
  mkdir "$daemon_cgroup"
  # shellcheck disable=SC2016  # $$ and $1 belong to the child shell
  setsid -f bash -c 'printf "%s\n" "$$" >"$1/cgroup.procs"; exec /usr/lib/urnetwork/urnetworkd --foreground' \
    _ "$daemon_cgroup" >/artifacts/daemon.log 2>&1
  wait_for "the daemon control socket" 150 test -S /run/urnetwork/control.sock

  local uid runtime
  uid="$(id -u "$user")"
  runtime="/run/user/$uid"
  mkdir -p "$runtime"
  chown "$user" "$runtime"
  chmod 700 "$runtime"
  setsid -f Xvfb :99 -screen 0 1440x1000x24 -nolisten tcp >/artifacts/xvfb.log 2>&1
  wait_for "the X display" 40 test -S /tmp/.X11-unix/X99

  # C locale: the GUI shows its English gettext sources, which atspi names
  # are matched against. Software rendering: there is no GPU.
  cat >"$run/session.env" <<EOF
HOME=/home/$user
XDG_RUNTIME_DIR=$runtime
DISPLAY=:99
DBUS_SESSION_BUS_ADDRESS=unix:path=$runtime/bus
LC_ALL=C.UTF-8
LANG=C.UTF-8
LANGUAGE=
GDK_BACKEND=x11
GSK_RENDERER=cairo
GTK_A11Y=atspi
APPIMAGE_EXTRACT_AND_RUN=1
URNETWORK_IB_CREDENTIALS=$run/credentials
URNETWORK_IB_NOTIFICATIONS=$run/notifications.jsonl
EOF
  chmod 644 "$run/session.env"
  runuser -u "$user" -- setsid -f dbus-daemon --session --nofork --address="unix:path=$runtime/bus" \
    >/artifacts/dbus.log 2>&1
  wait_for "the session bus" 40 test -S "$runtime/bus"
  # an empty-password login keyring, unlocked for this disposable session
  printf '' | as_user gnome-keyring-daemon --unlock --components=secrets >/artifacts/keyring.log 2>&1
  touch "$run/notifications.jsonl"
  chown "$user" "$run/notifications.jsonl"
  as_user setsid -f python3 "$here/notifications.py" >/artifacts/notifications.log 2>&1
  wait_for "the notification server" 40 as_user python3 "$here/atspi.py" has-name org.freedesktop.Notifications
  as_user setsid -f "$UR_IB_APPIMAGE" >/artifacts/gui.log 2>&1
  as_user python3 "$here/atspi.py" wait-app 120
}

egress() {
  local url out why=""
  for url in "${probe_urls[@]}"; do
    # a fresh process per probe: no reused physical socket
    if out="$(runuser -u "$user" -- curl -sS --max-time 15 --no-keepalive "$url" 2>&1)"; then
      out="$(printf '%s' "$out" | tr -d '[:space:]')"
      case "$out" in
        *[!0-9a-fA-F.:]*|'') why="$url: unexpected response" ;;
        *) echo "ip $out"; return 0 ;;
      esac
    else
      why="$url: $(printf '%s' "$out" | tail -1 | tr -d '\r')"
    fi
  done
  echo "error $why"
}

traffic() {
  # bounded: at most 8 requests of 10 s each to the official site
  for _ in $(seq 1 8); do
    runuser -u "$user" -- curl -sS --max-time 10 --max-filesize 8000000 -o /dev/null https://ur.io/ >/dev/null 2>&1 || true
  done
}

cmd="${1:-}"
[ $# -gt 0 ] && shift
case "$cmd" in
  start) start ;;
  egress) egress ;;
  traffic) traffic ;;
  tunnel) if ip link show urnet0 >/dev/null 2>&1; then echo up; else echo down; fi ;;
  notifications) cat "$run/notifications.jsonl" 2>/dev/null || true ;;
  atspi) as_user python3 "$here/atspi.py" "$@" ;;
  *) echo "usage: container.sh start|egress|traffic|tunnel|notifications|atspi ..." >&2; exit 2 ;;
esac
