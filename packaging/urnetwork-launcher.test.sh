#!/usr/bin/env bash
# Deterministic regression for /usr/bin/urnetwork (app/packaging/urnetwork-launcher),
# the Exec= target of the desktop entry, the urnetwork:// handler and autostart.
#
# The autostart entry's --autostart must reach the GUI as URNETWORK_AUTOSTART=1
# and never as an option, which a GUI from before the flag refuses to start on;
# every other launch reaches the GUI without the flag, whatever the environment
# held, with its arguments unchanged.
#
# Nothing on the host is touched: the environment is emptied, HOME is a
# temporary directory, the GUI is a fake named by URNETWORK_APPIMAGE, and the
# Flatpak fallback runs a fake flatpak.
set -euo pipefail
umask 077

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
launcher="$here/../app/packaging/urnetwork-launcher"

run_dir="$(mktemp -d "${TMPDIR:-/tmp}/urnetwork-launcher.test.XXXXXX")"
trap 'rm -rf "$run_dir"' EXIT

failures=0
fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

# ---- fakes -----------------------------------------------------------------

bin="$run_dir/bin"
mkdir -p "$bin" "$run_dir/home"
log="$run_dir/log"

# the GUI: logs its arguments and the flag
cat >"$run_dir/gui" <<'EOF'
#!/bin/sh
printf 'gui args=[%s] autostart=[%s]\n' "$*" "${URNETWORK_AUTOSTART-unset}" >>"$FAKE_LOG"
EOF

# flatpak: has the app installed, and logs what it is asked to run
cat >"$bin/flatpak" <<'EOF'
#!/bin/sh
case "$1" in
    info) exit 0 ;;
    run)
        shift
        printf 'flatpak run args=[%s] autostart=[%s]\n' "$*" "${URNETWORK_AUTOSTART-unset}" >>"$FAKE_LOG"
        ;;
esac
EOF
chmod 0755 "$run_dir/gui" "$bin/flatpak"

# ---- harness ---------------------------------------------------------------

# expect_launch <name> <expected log line> [env...] -- [launcher args...]
expect_launch() {
    local name="$1" expected="$2"
    shift 2
    local envs=()
    while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do envs+=("$1"); shift; done
    shift
    : >"$log"
    set +e
    env -i PATH="/usr/bin:/bin" HOME="$run_dir/home" FAKE_LOG="$log" \
        "${envs[@]+"${envs[@]}"}" \
        sh "$launcher" "$@" >"$run_dir/out" 2>&1
    local rc=$?
    set -e
    [ "$rc" = 0 ] || fail "$name: the launcher exited $rc: $(cat "$run_dir/out")"
    local got
    got="$(cat "$log")"
    [ "$got" = "$expected" ] || fail "$name: expected '$expected', got '$got'"
}

gui="URNETWORK_APPIMAGE=$run_dir/gui"

# ---- cases -----------------------------------------------------------------

# 1. The autostart entry: the flag, not the option.
expect_launch autostart "gui args=[] autostart=[1]" "$gui" -- --autostart

# 2. The menu, and a link: no flag, the arguments as they came.
expect_launch menu "gui args=[] autostart=[unset]" "$gui" --
expect_launch link "gui args=[urnetwork://callback.example/wallet] autostart=[unset]" \
    "$gui" -- urnetwork://callback.example/wallet

# 3. A flag already in the environment does not reach the GUI: only the
#    autostart entry's argument sets it.
expect_launch inherited "gui args=[] autostart=[unset]" "$gui" URNETWORK_AUTOSTART=1 --

# 4. Only the first argument is the launcher's: anywhere else it is the GUI's.
expect_launch later "gui args=[urnetwork://callback.example/wallet --autostart] autostart=[unset]" \
    "$gui" -- urnetwork://callback.example/wallet --autostart

# 5. The Flatpak fallback carries the flag the same way. Skipped on a host
#    that has a GUI where the launcher looks before the Flatpak.
if [ -e /usr/lib/urnetwork/URnetwork.AppImage ] ||
    PATH="/usr/bin:/bin" command -v urnetwork-gui >/dev/null 2>&1; then
    echo "SKIP: flatpak: this host has a GUI the launcher prefers"
else
    expect_launch flatpak "flatpak run args=[com.bringyour.network] autostart=[1]" \
        PATH="$bin:/usr/bin:/bin" -- --autostart
fi

if [ "$failures" -ne 0 ]; then
    echo "urnetwork-launcher: $failures failure(s)" >&2
    exit 1
fi
echo "urnetwork-launcher: OK"
