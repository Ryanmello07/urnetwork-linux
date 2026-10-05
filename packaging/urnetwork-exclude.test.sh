#!/usr/bin/env bash
# Deterministic regression for /usr/bin/urnetwork-exclude
# (app/packaging/urnetwork-exclude), the per-app split tunnel launcher.
#
# The command must run in the user's urnetwork-exclude.slice, created first and
# let out by urnetworkd before the command starts; a host that is not on the
# cgroup v2 unified hierarchy must be refused before anything runs.
#
# Nothing on the host is touched: systemd-run and sleep are fakes on PATH, and
# the cgroup root, /proc/self/cgroup and urnetworkd's slice list are files under
# a temporary directory (URNETWORK_CGROUP_ROOT, URNETWORK_PROC_CGROUP,
# URNETWORK_EXCLUDE_STATE).
set -euo pipefail
umask 077

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
launcher="$here/../app/packaging/urnetwork-exclude"

run_dir="$(mktemp -d "${TMPDIR:-/tmp}/urnetwork-exclude.test.XXXXXX")"
trap 'rm -rf "$run_dir"' EXIT

failures=0
fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

uid="$(id -u)"
slice="user.slice/user-${uid}.slice/user@${uid}.service/urnetwork.slice/urnetwork-exclude.slice"

# ---- fakes -----------------------------------------------------------------

bin="$run_dir/bin"
mkdir -p "$bin"

# systemd-run --user --scope ... --slice=urnetwork-exclude.slice -- <command>:
# logs its arguments, puts the "scope" in the slice, and execs the command. With
# FAKE_ARM_ON_RUN the daemon lets the slice out as soon as the slice exists.
cat >"$bin/systemd-run" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$FAKE_LOG"
mkdir -p "$URNETWORK_CGROUP_ROOT/$FAKE_SLICE"
if [ -n "${FAKE_ARM_ON_RUN:-}" ]; then printf '%s\n' "$FAKE_SLICE" >>"$URNETWORK_EXCLUDE_STATE"; fi
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do shift; done
shift
exec "$@"
EOF

# sleep: instant. With FAKE_ARM_ON_SLEEP the daemon's next tick lets the slice
# out during the first wait.
cat >"$bin/sleep" <<'EOF'
#!/bin/sh
printf 'sleep %s\n' "$*" >>"$FAKE_LOG"
if [ -n "${FAKE_ARM_ON_SLEEP:-}" ]; then printf '%s\n' "$FAKE_SLICE" >>"$URNETWORK_EXCLUDE_STATE"; fi
EOF
chmod 0755 "$bin/systemd-run" "$bin/sleep"

# ---- harness ---------------------------------------------------------------

# new_host <name> <cgroup2: yes|no> <proc cgroup text>
new_host() {
    host="$run_dir/$1"
    mkdir -p "$host/cgroup"
    if [ "$2" = yes ]; then : >"$host/cgroup/cgroup.controllers"; fi
    printf '%s' "$3" >"$host/proc-self-cgroup"
    : >"$host/log"
}

# run_launcher <host> [env...] -- <launcher args...>; sets out, rc.
run_launcher() {
    local h="$1"
    shift
    local envs=()
    while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do envs+=("$1"); shift; done
    shift
    set +e
    out="$(env PATH="$bin:$PATH" \
        URNETWORK_CGROUP_ROOT="$h/cgroup" \
        URNETWORK_PROC_CGROUP="$h/proc-self-cgroup" \
        URNETWORK_EXCLUDE_STATE="$h/excluded-slices" \
        FAKE_LOG="$h/log" FAKE_SLICE="$slice" \
        "${envs[@]+"${envs[@]}"}" \
        sh "$launcher" "$@" 2>&1)"
    rc=$?
    set -e
}

expect_scope_run() {
    local h="$1" what="$2"
    grep -Fqx -- "--user --scope --quiet --collect --slice=urnetwork-exclude.slice -- $what" "$h/log" ||
        fail "$h: systemd-run was not asked to run '$what' in the slice: $(cat "$h/log")"
}

v2='0::/user.slice/user-1000.slice/session-2.scope
'

# ---- cases -----------------------------------------------------------------

# 1. The first exclusion of a login with no tunnel up: the slice is created
#    first (a no-op scope), then the command runs in it, with its arguments and
#    its exit status. No ruleset is installed, so nothing is waited for.
new_host first yes "$v2"
run_launcher "$host" -- sh -c 'echo "ran:$*"; exit 7' sh one 'two words'
[ "$rc" = 7 ] || fail "first: exit status $rc, want the command's 7 ($out)"
case "$out" in *"ran:one two words"*) ;; *) fail "first: the command did not run with its arguments: $out" ;; esac
expect_scope_run "$host" "true"
expect_scope_run "$host" "sh -c echo \"ran:\$*\"; exit 7 sh one two words"
[ "$(head -n1 "$host/log")" = "--user --scope --quiet --collect --slice=urnetwork-exclude.slice -- true" ] ||
    fail "first: the slice was not created before the command: $(cat "$host/log")"
[ -d "$host/cgroup/$slice" ] || fail "first: the slice is not at $slice"
grep -q '^sleep' "$host/log" && fail "first: waited although no ruleset is installed"

# 2. The slice exists and urnetworkd already lets it out: one scope, no wait.
new_host armed yes "$v2"
mkdir -p "$host/cgroup/$slice"
printf '%s\n' "$slice" >"$host/excluded-slices"
run_launcher "$host" -- echo armed-ran
[ "$rc" = 0 ] || fail "armed: exit status $rc ($out)"
case "$out" in *armed-ran*) ;; *) fail "armed: the command did not run: $out" ;; esac
[ "$(wc -l <"$host/log" | tr -d ' ')" = 1 ] || fail "armed: expected one systemd-run, got: $(cat "$host/log")"
expect_scope_run "$host" "echo armed-ran"

# 3. A tunnel is up and the slice is new: the command starts only after the
#    daemon has let the slice out (here on its first tick), with no warning.
new_host waits yes "$v2"
: >"$host/excluded-slices"
run_launcher "$host" FAKE_ARM_ON_SLEEP=1 -- echo waited-ran
[ "$rc" = 0 ] || fail "waits: exit status $rc ($out)"
[ "$(grep -c '^sleep' "$host/log")" = 1 ] || fail "waits: expected one wait, got: $(cat "$host/log")"
[ "$(tail -n1 "$host/log")" = "--user --scope --quiet --collect --slice=urnetwork-exclude.slice -- echo waited-ran" ] ||
    fail "waits: the command did not start after the wait: $(cat "$host/log")"
case "$out" in *"not excluding"*) fail "waits: warned although the slice was let out: $out" ;; esac

# 4. A tunnel is up and the slice is never let out (another user owns it): the
#    command still runs, in the tunnel, after a bounded wait and a warning.
new_host other yes "$v2"
mkdir -p "$host/cgroup/$slice"
printf '%s\n' "user.slice/user-4242.slice/user@4242.service/urnetwork.slice/urnetwork-exclude.slice" >"$host/excluded-slices"
run_launcher "$host" -- echo other-ran
[ "$rc" = 0 ] || fail "other: exit status $rc ($out)"
case "$out" in *"URnetwork is not excluding this command"*) ;; *) fail "other: no warning: $out" ;; esac
case "$out" in *other-ran*) ;; *) fail "other: the command did not run: $out" ;; esac
[ "$(grep -c '^sleep' "$host/log")" = 5 ] || fail "other: expected five one-second waits, got: $(cat "$host/log")"

# 5. cgroup v1 only (no unified hierarchy): refused, nothing runs.
new_host v1 no '12:pids:/user.slice/user-1000.slice/session-2.scope
1:name=systemd:/user.slice/user-1000.slice/session-2.scope
'
run_launcher "$host" -- echo v1-ran
[ "$rc" = 1 ] || fail "v1: exit status $rc, want 1 ($out)"
case "$out" in *"cgroup v2 unified hierarchy"*) ;; *) fail "v1: no refusal message: $out" ;; esac
case "$out" in *v1-ran*) fail "v1: the command ran: $out" ;; esac
[ -s "$host/log" ] && fail "v1: systemd-run was called: $(cat "$host/log")"

# 6. Hybrid (cgroup2 mounted at the root here, but a v1 hierarchy alongside):
#    refused too, because nft and urnetworkd do not see that slice.
new_host hybrid yes '1:name=systemd:/user.slice/user-1000.slice/session-2.scope
0::/user.slice/user-1000.slice/session-2.scope
'
run_launcher "$host" -- echo hybrid-ran
[ "$rc" = 1 ] || fail "hybrid: exit status $rc, want 1 ($out)"
case "$out" in *hybrid-ran*) fail "hybrid: the command ran: $out" ;; esac
[ -s "$host/log" ] && fail "hybrid: systemd-run was called: $(cat "$host/log")"

# 7. No command: usage, exit 2, nothing runs.
new_host usage yes "$v2"
run_launcher "$host" --
[ "$rc" = 2 ] || fail "usage: exit status $rc, want 2 ($out)"
case "$out" in *"usage: urnetwork-exclude"*) ;; *) fail "usage: no usage line: $out" ;; esac
[ -s "$host/log" ] && fail "usage: systemd-run was called: $(cat "$host/log")"

if [ "$failures" -ne 0 ]; then
    echo "urnetwork-exclude: $failures failure(s)" >&2
    exit 1
fi
echo "urnetwork-exclude: OK"
