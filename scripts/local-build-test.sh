#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"

echo "[local-build-test] configure: ${build_dir}"
meson setup "${build_dir}" --reconfigure

echo "[local-build-test] compile: ${build_dir}"
meson compile -C "${build_dir}"

echo "[local-build-test] test: ${build_dir}"
meson test -C "${build_dir}" --print-errorlogs

echo "[local-build-test] nested smoke"
bash ./scripts/test-nested-smoke.sh "${build_dir}"

echo "[local-build-test] ok"
