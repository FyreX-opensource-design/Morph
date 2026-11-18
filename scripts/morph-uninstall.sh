#!/usr/bin/env bash
# Uninstall helper for runtime and debug install flows.
# Runtime removal delegates to the conservative Meson manifest-based helper.
# Debug removal deletes explicit artifacts installed by morph-install.sh.

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/morph-uninstall.sh [--runtime|--debug|--both] [--force] [--dry]

Uninstall modes:
  --runtime   Remove runtime artifacts via scripts/system-uninstall.sh
  --debug     Remove debug session artifacts installed by scripts/morph-install.sh
  --both      Run runtime uninstall first, then debug uninstall

Options:
  --force     Remove selected artifacts even when modified or from another build
  --dry       Print commands only (no changes)

Debug uninstall targets:
  - ~/.config/morph/* symlinks when they still point at testing/config/*
  - ~/.local/bin/morph-session_dbg when it still points at ./testing/morph-session_dbg
  - /usr/bin/morph_dbg
  - /usr/bin/morph-session_dbg
  - /usr/share/wayland-sessions/morph_dbg.desktop
  - /usr/share/icons/hicolor/scalable/apps/morph_dbg.svg

Notes:
  - Without --force, runtime and debug uninstall keep their conservative
    verification and leave modified or unrelated files untouched.
  - With --force, selected runtime manifest targets and all known debug targets
    are removed without source or link-target verification.
  - With --force, a ~/.config/morph directory containing only symlinks is
    deleted. If it contains any real file or directory, the complete directory
    is renamed to ~/.config/morph_bak (or the next free numbered suffix).
EOF
}

MODE="both"
DRY=0
FORCE=0

# Last mode option wins, matching the install/build helper behavior.
while [ $# -gt 0 ]; do
    case "$1" in
        --runtime) MODE="runtime" ;;
        --debug) MODE="debug" ;;
        --both) MODE="both" ;;
        --force) FORCE=1 ;;
        --dry) DRY=1 ;;
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

run_root() {
    if [ "$DRY" -eq 1 ]; then
        printf '[dry] %s\n' "$*"
        return 0
    fi

    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        printf 'Need root privileges for uninstall command: %s\n' "$*" >&2
        printf 'Run as root or install sudo.\n' >&2
        exit 1
    fi
}

require_dev_install_helper() {
    if [ ! -x "scripts/dev-install.sh" ]; then
        printf 'Missing dev install helper: ./scripts/dev-install.sh\n' >&2
        exit 1
    fi
}

run_as_invoking_user() {
    if [ "$(id -u)" -ne 0 ] || [ -z "${SUDO_USER:-}" ] || [ "${SUDO_USER:-}" = root ]; then
        "$@"
        return $?
    fi

    user_home=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    if [ -z "$user_home" ]; then
        printf 'Unable to resolve home directory for sudo user: %s\n' "$SUDO_USER" >&2
        exit 1
    fi

    # User-local dev links must be removed from the invoking user's home even
    # when the combined uninstall helper itself was started with sudo.
    sudo -u "$SUDO_USER" env HOME="$user_home" XDG_CONFIG_HOME="$user_home/.config" "$@"
}

run_dev_uninstall_user_links() {
    require_dev_install_helper

    if [ "$DRY" -eq 1 ]; then
        run_as_invoking_user ./scripts/dev-install.sh uninstall --link-launcher --dry
    else
        run_as_invoking_user ./scripts/dev-install.sh uninstall --link-launcher
    fi
}

run_dev_uninstall_system_links() {
    require_dev_install_helper

    if [ "$DRY" -eq 1 ]; then
        ./scripts/dev-install.sh uninstall --system-links --skip-user-links --dry
    else
        ./scripts/dev-install.sh uninstall --system-links --skip-user-links
    fi
}

resolve_invoking_user_paths() {
    if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER:-}" != root ]; then
        INVOKING_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
        if [ -z "$INVOKING_HOME" ]; then
            printf 'Unable to resolve home directory for sudo user: %s\n' "$SUDO_USER" >&2
            exit 1
        fi
        INVOKING_CONFIG_HOME="$INVOKING_HOME/.config"
    else
        INVOKING_HOME="$HOME"
        INVOKING_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
    fi
}

next_config_backup_path() {
    backup="$INVOKING_CONFIG_HOME/morph_bak"
    if [ ! -e "$backup" ] && [ ! -L "$backup" ]; then
        printf '%s\n' "$backup"
        return 0
    fi

    suffix=1
    while [ -e "$backup.$suffix" ] || [ -L "$backup.$suffix" ]; do
        suffix=$((suffix + 1))
    done
    printf '%s\n' "$backup.$suffix"
}

force_cleanup_user_config() {
    resolve_invoking_user_paths
    config_dir="$INVOKING_CONFIG_HOME/morph"
    if [ ! -e "$config_dir" ] && [ ! -L "$config_dir" ]; then
        return 0
    fi

    if [ -L "$config_dir" ]; then
        if [ "$DRY" -eq 1 ]; then
            printf '[dry] rm -f %s\n' "$config_dir"
        else
            run_as_invoking_user rm -f -- "$config_dir"
            printf 'Force-removed user config symlink: %s\n' "$config_dir"
        fi
        return 0
    fi

    has_real_entries=0
    if [ ! -d "$config_dir" ]; then
        has_real_entries=1
    elif find "$config_dir" -mindepth 1 ! -type l -print -quit | grep -q .; then
        has_real_entries=1
    fi

    # A mixed tree can contain irreplaceable user edits, so preserve it as a unit.
    if [ "$has_real_entries" -eq 1 ]; then
        backup=$(next_config_backup_path)
        if [ "$DRY" -eq 1 ]; then
            printf '[dry] mv %s %s\n' "$config_dir" "$backup"
        else
            run_as_invoking_user mv -- "$config_dir" "$backup"
            printf 'Backed up user config with real files: %s -> %s\n' "$config_dir" "$backup"
        fi
        return 0
    fi

    if [ "$DRY" -eq 1 ]; then
        printf '[dry] rm -rf %s\n' "$config_dir"
    else
        run_as_invoking_user rm -rf -- "$config_dir"
        printf 'Force-removed symlink-only user config: %s\n' "$config_dir"
    fi
}

force_cleanup_debug_artifacts() {
    resolve_invoking_user_paths
    user_bin="$INVOKING_HOME/.local/bin/morph-session_dbg"
    local_desktop="$INVOKING_HOME/.local/share/wayland-sessions/morph_dbg.desktop"
    local_icon="$INVOKING_HOME/.local/share/icons/hicolor/scalable/apps/morph_dbg.svg"

    for path in "$user_bin" "$local_desktop" "$local_icon"; do
        if [ "$DRY" -eq 1 ]; then
            printf '[dry] rm -f %s\n' "$path"
        else
            run_as_invoking_user rm -f -- "$path"
        fi
    done

    run_root rm -f /usr/bin/morph_dbg
    run_root rm -f /usr/bin/morph-session_dbg
    run_root rm -f /usr/share/wayland-sessions/morph_dbg.desktop
    run_root rm -f /usr/share/icons/hicolor/scalable/apps/morph_dbg.svg
}

uninstall_runtime() {
    printf '[morph-uninstall] runtime phase\n'

    if [ ! -f "scripts/system-uninstall.sh" ]; then
        printf 'Missing runtime uninstall helper: ./scripts/system-uninstall.sh\n' >&2
        exit 1
    fi

    if [ "$DRY" -eq 1 ]; then
        if [ "$FORCE" -eq 1 ]; then
            printf '[dry] sudo ./scripts/system-uninstall.sh --builddir build --remove --force\n'
        else
            printf '[dry] sudo ./scripts/system-uninstall.sh --builddir build --remove\n'
        fi
    elif [ "$FORCE" -eq 1 ]; then
        run_root ./scripts/system-uninstall.sh --builddir build --remove --force
    else
        run_root ./scripts/system-uninstall.sh --builddir build --remove
    fi
}

uninstall_debug() {
    printf '[morph-uninstall] debug phase\n'
    if [ "$FORCE" -eq 1 ]; then
        force_cleanup_debug_artifacts
    else
        run_dev_uninstall_user_links
        run_dev_uninstall_system_links
    fi
}

case "$MODE" in
    runtime)
        uninstall_runtime
        ;;
    debug)
        uninstall_debug
        ;;
    both)
        uninstall_runtime
        uninstall_debug
        ;;
    *)
        printf 'Internal mode error: %s\n' "$MODE" >&2
        exit 1
        ;;
esac

if [ "$FORCE" -eq 1 ]; then
    force_cleanup_user_config
fi

printf '[morph-uninstall] ok (%s)%s%s\n' "$MODE" \
    "$( [ "$FORCE" -eq 1 ] && printf ' [force]' )" \
    "$( [ "$DRY" -eq 1 ] && printf ' [dry]' )"
