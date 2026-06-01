#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"

echo "[ci-local] configure: ${build_dir}"
meson setup "${build_dir}" --reconfigure

echo "[ci-local] compile: ${build_dir}"
meson compile -C "${build_dir}"

echo "[ci-local] test: ${build_dir}"
meson test -C "${build_dir}" --print-errorlogs

echo "[ci-local] ok"
