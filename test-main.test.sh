#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
runner="$root/test-main.sh"
setup_runner="$(dirname "$root")/build/all/linux/setup.sh"

# Docker's default private cgroup namespace exposes the acceptance process as
# cgroup-v2 root (0::/). The daemon intentionally rejects that identity because
# an nft socket rule for it would match every process. Keep the real data-plane
# container in the host cgroup namespace so /proc/self/cgroup names its unique
# docker/<id> directory.
data_plane_command="$({
  sed -n '/docker run --name "$container_name"/,/bash \/opt\/urnetwork-acceptance\/run.sh/p' "$runner"
} || true)"

if [ -z "$data_plane_command" ]; then
  echo "could not locate the Linux acceptance data-plane container command" >&2
  exit 1
fi

count="$(printf '%s\n' "$data_plane_command" | grep -c -- '--cgroupns=host' || true)"
if [ "$count" -ne 1 ]; then
  echo "data-plane container must use exactly one --cgroupns=host flag (found $count)" >&2
  exit 1
fi

count="$(printf '%s\n' "$data_plane_command" | grep -c -- '--privileged' || true)"
if [ "$count" -ne 1 ]; then
  echo "data-plane container must use exactly one --privileged flag (found $count)" >&2
  exit 1
fi

setup_command="$({
  sed -n '/smoke-testing the acceptance tunnel and cgroup-BPF privileges/,/then/p' "$setup_runner"
} || true)"
if [ -z "$setup_command" ]; then
  echo "could not locate the Linux setup privilege smoke test" >&2
  exit 1
fi
for required in '--privileged' '--cgroupns=host' 'cgroup.procs'; do
  count="$(printf '%s\n' "$setup_command" | grep -c -- "$required" || true)"
  if [ "$count" -ne 1 ]; then
    echo "Linux setup smoke must carry exactly one $required boundary (found $count)" >&2
    exit 1
  fi
done

control_agent_marker='echo "[linux acceptance] building the local SDK control agent"'
artifact_build_marker='echo "[linux acceptance] building local Linux artifacts"'
control_agent_line="$(grep -nF "$control_agent_marker" "$runner" | cut -d: -f1 || true)"
artifact_build_line="$(grep -nF "$artifact_build_marker" "$runner" | cut -d: -f1 || true)"
if ! [[ "$control_agent_line" =~ ^[0-9]+$ && "$artifact_build_line" =~ ^[0-9]+$ ]]; then
  echo "could not locate the control-agent and Linux artifact build boundaries" >&2
  exit 1
fi
if [ "$control_agent_line" -ge "$artifact_build_line" ]; then
  echo "the Linux ARM64 control agent must compile before the expensive artifact build" >&2
  exit 1
fi

control_agent_command="$(
  sed -n "${control_agent_line},$((control_agent_line + 3))p" "$runner"
)"
for required in \
  'CGO_ENABLED=0' \
  'GOOS=linux' \
  'GOARCH=arm64' \
  'timeout 600 go build -mod=readonly -trimpath' \
  "-o \"\$run_dir/agent\" ."; do
  count="$(printf '%s\n' "$control_agent_command" | grep -cF -- "$required" || true)"
  if [ "$count" -ne 1 ]; then
    echo "Linux control-agent build must carry exactly one $required boundary (found $count)" >&2
    exit 1
  fi
done
if grep -Eq 'go[[:space:]]+test' "$runner"; then
  echo "Linux TEST-MAIN must not invoke the unit-test suite" >&2
  exit 1
fi

echo "linux acceptance runner regression: PASS"
