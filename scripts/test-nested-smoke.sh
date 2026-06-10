#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/.." && pwd)"
launcher="${root_dir}/testing/stackcomp_run"
binary="${root_dir}/${build_dir}/stackcomp"

if [[ ! -x "${launcher}" ]]; then
  echo "[nested-smoke] launcher not executable: ${launcher}" >&2
  exit 1
fi

if [[ ! -x "${binary}" ]]; then
  echo "[nested-smoke] compositor binary not found: ${binary}" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
log_dir="${tmp_dir}/logs"
mkdir -p "${log_dir}"
trap 'rm -rf "${tmp_dir}"' EXIT

has_display=0
if [[ -n "${DISPLAY:-}" ]]; then
  if command -v xdpyinfo >/dev/null 2>&1 && DISPLAY="${DISPLAY}" xdpyinfo >/dev/null 2>&1; then
    has_display=1
  elif command -v xset >/dev/null 2>&1 && DISPLAY="${DISPLAY}" xset q >/dev/null 2>&1; then
    has_display=1
  fi
fi

run_cmd=(env STACKCOMP_LOG_DIR="${log_dir}" STACKCOMP_DBG=1 STACKCOMP_X11=0)
if [[ "${has_display}" -eq 1 ]]; then
  echo "[nested-smoke] using existing DISPLAY=${DISPLAY}"
else
  if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "[nested-smoke] SKIP: no reachable DISPLAY and xvfb-run is missing"
    exit 0
  fi
  echo "[nested-smoke] using xvfb-run"
  run_cmd+=(xvfb-run -a -s "-screen 0 1280x720x24")
fi
run_cmd+=("${launcher}")

"${run_cmd[@]}" >"${tmp_dir}/runner.out" 2>&1 &
launcher_pid=$!

startup_log="${log_dir}/stackcomp-nested-startup.log"
shutdown_log="${log_dir}/stackcomp-nested-shutdown.log"

for _ in {1..50}; do
  if [[ -s "${startup_log}" ]]; then
    break
  fi
  sleep 0.2
done

if [[ ! -s "${startup_log}" ]]; then
  echo "[nested-smoke] startup log not created: ${startup_log}" >&2
  kill "${launcher_pid}" >/dev/null 2>&1 || true
  wait "${launcher_pid}" >/dev/null 2>&1 || true
  exit 1
fi

stackcomp_pid="$(pgrep -P "${launcher_pid}" -x stackcomp | head -n1 || true)"
if [[ -z "${stackcomp_pid}" ]]; then
  echo "[nested-smoke] could not find stackcomp child process" >&2
  kill "${launcher_pid}" >/dev/null 2>&1 || true
  wait "${launcher_pid}" >/dev/null 2>&1 || true
  exit 1
fi

kill -TERM "${stackcomp_pid}" >/dev/null 2>&1 || true
set +e
wait "${launcher_pid}"
rc=$?
set -e

echo "[nested-smoke] launcher exit code: ${rc}"

if ! rg -n "Selected session mode:\s+nested-|WLR_BACKENDS:\s+x11|WLR_BACKENDS:\s+wayland" "${startup_log}" >/dev/null; then
  echo "[nested-smoke] expected nested mode markers missing in startup log" >&2
  tail -n 120 "${startup_log}" >&2 || true
  exit 1
fi

if ! rg -n "Compositor exited with rc=|Compositor crashed or exited with an error" "${startup_log}" >/dev/null; then
  echo "[nested-smoke] expected compositor error/crash marker missing in startup log" >&2
  tail -n 120 "${startup_log}" >&2 || true
  exit 1
fi

if [[ ! -f "${shutdown_log}" ]]; then
  echo "[nested-smoke] shutdown log missing: ${shutdown_log}" >&2
  exit 1
fi

if ! rg -n "Nested mode \(x11\)|Nested mode \(wayland\)|Session cleanup completed \(nested mode\)" "${shutdown_log}" >/dev/null; then
  echo "[nested-smoke] expected nested shutdown markers missing" >&2
  tail -n 120 "${shutdown_log}" >&2 || true
  exit 1
fi

echo "[nested-smoke] ok"
