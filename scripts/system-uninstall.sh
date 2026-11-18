#!/bin/sh
# Conservative uninstall helper for Meson-installed morph artifacts.
# - Reads the current install manifest from `meson introspect --installed`.
# - Prints the exact installed paths by default.
# - Removes only unchanged files when explicitly asked to do so.
################################################################################

set -eu

BUILD_DIR="build"
REMOVE_MODE=0
FORCE_MODE=0

usage() {
    cat <<'EOF'
Usage: scripts/system-uninstall.sh [options]

Options:
  --builddir DIR   Meson build directory to inspect (default: build)
  --print-only     Print installed paths without removing anything (default)
  --remove         Remove only unchanged installed files from the manifest
  --force          With --remove, delete manifest targets without comparing them
  -h, --help       Show this help text

Behavior:
  - The script reads the install manifest from `meson introspect --installed`.
  - `--remove` only deletes targets that still match the recorded source file.
  - `--remove --force` also deletes modified targets or targets installed from
    another build tree. Destination paths still come from the manifest.
  - Without `--force`, modified, missing, or uncomparable files are left in
    place with a warning.
  - Empty directories are pruned afterwards with `rmdir`, so non-empty system
    directories stay untouched automatically.

Typical usage:
  scripts/system-uninstall.sh --builddir build
  sudo scripts/system-uninstall.sh --builddir build --remove
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --builddir)
            shift
            [ $# -gt 0 ] || {
                printf 'Missing value for --builddir\n' >&2
                exit 1
            }
            BUILD_DIR="$1"
            ;;
        --print-only)
            REMOVE_MODE=0
            ;;
        --remove)
            REMOVE_MODE=1
            ;;
        --force)
            FORCE_MODE=1
            REMOVE_MODE=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

manifest_json=$(mktemp "${TMPDIR:-/tmp}/morph-install-manifest.XXXXXX")
manifest_pairs=$(mktemp "${TMPDIR:-/tmp}/morph-install-pairs.XXXXXX")
# Always cleanup temporary manifest files, even on interrupt.
trap 'rm -f "$manifest_json" "$manifest_pairs"' EXIT HUP INT TERM

meson introspect --installed "$BUILD_DIR" > "$manifest_json"

# Meson returns a flat JSON object that maps installed source paths to target
# paths. Extract the pairs once so print and remove mode operate on the same
# manifest snapshot even if the build tree changes during the run.
awk '
    BEGIN {
        while ((getline line < ARGV[1]) > 0) {
            json = json line
        }
        ARGV[1] = ""
        rest = json
        while (match(rest, /"([^"]+)":[[:space:]]*"([^"]+)"/)) {
            pair = substr(rest, RSTART, RLENGTH)
            split(pair, fields, "\"")
            print fields[2] "\t" fields[4]
            rest = substr(rest, RSTART + RLENGTH)
        }
    }
' "$manifest_json" > "$manifest_pairs"

if [ ! -s "$manifest_pairs" ]; then
    printf 'No installed artifacts found in Meson manifest for build dir: %s\n' "$BUILD_DIR" >&2
    exit 1
fi

print_manifest() {
    printf 'Installed artifacts for build dir: %s\n' "$BUILD_DIR"
    while IFS="$(printf '\t')" read -r src dst; do
        printf '  %s -> %s\n' "$src" "$dst"
    done < "$manifest_pairs"
}

remove_target_if_unchanged() {
    src="$1"
    dst="$2"

    if [ ! -e "$dst" ] && [ ! -L "$dst" ]; then
        printf 'WARN: installed path is already absent, leaving it untouched: %s\n' "$dst" >&2
        return 0
    fi

    if [ "$FORCE_MODE" -eq 1 ]; then
        rm -f "$dst"
        printf 'Force-removed installed file: %s\n' "$dst"
        return 0
    fi

    if [ ! -e "$src" ] && [ ! -L "$src" ]; then
        printf 'WARN: install source no longer exists, cannot verify target safely: %s -> %s\n' "$src" "$dst" >&2
        return 0
    fi

    # Compare against the original build/source artifact so local admin changes
    # under /etc or other prefixes are not deleted silently.
    if cmp -s "$src" "$dst"; then
        rm -f "$dst"
        printf 'Removed unchanged installed file: %s\n' "$dst"
        return 0
    fi

    printf 'WARN: installed file differs from current source, leaving it untouched: %s\n' "$dst" >&2
}

prune_empty_parent_dirs() {
    while IFS="$(printf '\t')" read -r _ dst; do
        dir=$(dirname "$dst")
        while [ "$dir" != "/" ]; do
            rmdir "$dir" 2>/dev/null || break
            dir=$(dirname "$dir")
        done
    done < "$manifest_pairs"
}

print_manifest

if [ "$REMOVE_MODE" -eq 0 ]; then
    printf 'Print-only mode active. No files were removed.\n'
    exit 0
fi

while IFS="$(printf '\t')" read -r src dst; do
    remove_target_if_unchanged "$src" "$dst"
done < "$manifest_pairs"

# Final directory pruning is best-effort and only removes empty directories.
prune_empty_parent_dirs
printf 'Conservative uninstall pass completed.\n'
