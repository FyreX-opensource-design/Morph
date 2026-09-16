#!/bin/sh
set -eu

test_mode=release
if [ "${1:-}" = "--dbg" ]; then
    test_mode=debug
    shift
fi

repo_root=${1:?expected repository root as first argument}

if [ "$test_mode" = debug ]; then
    expected_morph_bin="$repo_root/build_dbg/morph"
else
    expected_morph_bin="$repo_root/build/morph"
fi

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

assert_file_contains() {
    file=$1
    expected=$2
    if ! grep -F -- "$expected" "$file" >/dev/null 2>&1; then
        printf 'Missing expected text in %s:\n%s\n' "$file" "$expected" >&2
        if [ -f "$file" ]; then
            printf '%s\n' '--- file contents ---' >&2
            cat "$file" >&2
            printf '%s\n' '---------------------' >&2
        fi
        exit 1
    fi
}

assert_log_field_contains() {
    file=$1
    label=$2
    expected=$3

    if ! grep -F -- "$label:" "$file" | grep -F -- "$expected" >/dev/null 2>&1; then
        printf 'Missing expected log field in %s:\n%s: %s\n' "$file" "$label" "$expected" >&2
        if [ -f "$file" ]; then
            printf '%s\n' '--- file contents ---' >&2
            cat "$file" >&2
            printf '%s\n' '---------------------' >&2
        fi
        exit 1
    fi
}

assert_equals() {
    expected=$1
    actual=$2
    label=$3
    if [ "$expected" != "$actual" ]; then
        fail "$label: expected '$expected', got '$actual'"
    fi
}

make_tmpdir() {
    mktemp -d "${TMPDIR:-/tmp}/morph-shell-test.XXXXXX"
}

assert_command_fails() {
    status=$1
    label=$2
    if [ "$status" -eq 0 ]; then
        fail "$label: expected command failure"
    fi
}

launcher_startup_log_path() {
    state_dir=$1
    nested_log="$state_dir/morph-nested-startup.log"
    default_log="$state_dir/morph-startup.log"

    # The launcher probes fallback X11 sockets even when DISPLAY is unset, so
    # resolve-only tests must accept either native or nested startup logs.
    if [ -f "$nested_log" ]; then
        printf '%s\n' "$nested_log"
        return 0
    fi

    printf '%s\n' "$default_log"
}

run_launcher_resolve_only() {
    if [ "$test_mode" = debug ]; then
        env -u DISPLAY -u WAYLAND_DISPLAY -u MORPH_X11 \
            "$@" \
            "$repo_root/testing/morph-session_dbg"
    else
        env -u DISPLAY -u WAYLAND_DISPLAY -u MORPH_X11 \
            MORPH_SYSTEM_HOOK_DIR="$repo_root/scripts" \
            MORPH_SYSTEM_CONFIG_DIR="$repo_root/config" \
            MORPH_BIN="$expected_morph_bin" \
            "$@" \
            sh "$repo_root/scripts/morph-session"
    fi
}

test_launch_helpers_track_expected_processes() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/bin"
    cat > "$tmpdir/bin/stdbuf" <<'EOF'
#!/bin/sh
shift 2
"$@"
EOF
    cat > "$tmpdir/bin/dummy-service" <<'EOF'
#!/bin/sh
printf 'dummy service output\n'
EOF
    chmod +x "$tmpdir/bin/stdbuf" "$tmpdir/bin/dummy-service"

    export PATH="$tmpdir/bin:$PATH"
    export CURRENT_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export MORPH_SESSION_MODE=native
    : > "$CURRENT_LOG_FILE"
    : > "$MORPH_SHUTDOWN_LIST"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"

    launch "$tmpdir/bin/dummy-service"
    launch_nokill "$tmpdir/bin/dummy-service"
    launch_nested "$tmpdir/bin/dummy-service"

    count=$(grep -c '^dummy-service$' "$MORPH_SHUTDOWN_LIST" || true)
    assert_equals "1" "$count" "launch helper shutdown tracker count"
    assert_file_contains "$CURRENT_LOG_FILE" "Starting $tmpdir/bin/dummy-service (registered for automatic shutdown)."
    assert_file_contains "$CURRENT_LOG_FILE" "Starting $tmpdir/bin/dummy-service (not registered for shutdown)."
    assert_file_contains "$CURRENT_LOG_FILE" "Skipping $tmpdir/bin/dummy-service (launch_nested only runs in nested sessions)."

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_reload_helper_restarts_without_duplicate_shutdown_entries() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/bin"
    cat > "$tmpdir/bin/stdbuf" <<'EOF'
#!/bin/sh
shift 2
"$@"
EOF
    cat > "$tmpdir/bin/dummy-service" <<'EOF'
#!/bin/sh
printf 'dummy reload output\n'
EOF
    chmod +x "$tmpdir/bin/stdbuf" "$tmpdir/bin/dummy-service"

    export PATH="$tmpdir/bin:$PATH"
    export CURRENT_LOG_FILE="$tmpdir/reload.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export PKILL_LOG_FILE="$tmpdir/pkill.log"
    : > "$CURRENT_LOG_FILE"
    : > "$MORPH_SHUTDOWN_LIST"
    : > "$PKILL_LOG_FILE"

    pkill() {
        printf '%s\n' "$*" >> "$PKILL_LOG_FILE"
        return 0
    }

    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"

    launch "$tmpdir/bin/dummy-service"
    reload "$tmpdir/bin/dummy-service"

    count=$(grep -c '^dummy-service$' "$MORPH_SHUTDOWN_LIST" || true)
    assert_equals "1" "$count" "reload helper shutdown tracker count"
    assert_file_contains "$CURRENT_LOG_FILE" "Reloading $tmpdir/bin/dummy-service."
    assert_file_contains "$CURRENT_LOG_FILE" "Stopped running instance for reload: dummy-service"
    assert_file_contains "$PKILL_LOG_FILE" "-x dummy-service"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_reload_once_starts_component_when_missing() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/bin"
    cat > "$tmpdir/bin/stdbuf" <<'EOF'
#!/bin/sh
shift 2
"$@"
EOF
    cat > "$tmpdir/bin/dummy-service" <<'EOF'
#!/bin/sh
printf 'dummy reload_once output\n'
EOF
    chmod +x "$tmpdir/bin/stdbuf" "$tmpdir/bin/dummy-service"

    export PATH="$tmpdir/bin:$PATH"
    export CURRENT_LOG_FILE="$tmpdir/reload-once.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export PKILL_LOG_FILE="$tmpdir/pkill.log"
    : > "$CURRENT_LOG_FILE"
    : > "$MORPH_SHUTDOWN_LIST"
    : > "$PKILL_LOG_FILE"

    pkill() {
        printf '%s\n' "$*" >> "$PKILL_LOG_FILE"
        case "$1" in
            -0)
                return 1
                ;;
            *)
                return 0
                ;;
        esac
    }

    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"

    reload_once "$tmpdir/bin/dummy-service"

    count=$(grep -c '^dummy-service$' "$MORPH_SHUTDOWN_LIST" || true)
    assert_equals "1" "$count" "reload_once shutdown tracker count"
    assert_file_contains "$CURRENT_LOG_FILE" "Starting component through reload_once: $tmpdir/bin/dummy-service"
    assert_file_contains "$CURRENT_LOG_FILE" "[reload_once:$tmpdir/bin/dummy-service] dummy reload_once output"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_reload_once_skips_already_running_component() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    export CURRENT_LOG_FILE="$tmpdir/reload-once.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export PKILL_LOG_FILE="$tmpdir/pkill.log"
    : > "$CURRENT_LOG_FILE"
    : > "$MORPH_SHUTDOWN_LIST"
    : > "$PKILL_LOG_FILE"

    pkill() {
        printf '%s\n' "$*" >> "$PKILL_LOG_FILE"
        case "$1" in
            -0)
                return 0
                ;;
            *)
                return 0
                ;;
        esac
    }

    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"

    reload_once /usr/bin/mutagen

    count=$(grep -c '^mutagen$' "$MORPH_SHUTDOWN_LIST" || true)
    assert_equals "1" "$count" "reload_once running tracker count"
    assert_file_contains "$CURRENT_LOG_FILE" "Skipping reload_once for already running component: mutagen"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_startup_nested_sets_wayland_display() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_STARTUP_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SESSION_MODE=nested
    export WLR_WL_SOCKET="wayland-test-socket"
    export XDG_CONFIG_HOME="$tmpdir/xdg-config"
    # Keep the hook lookup off the host's /etc/morph so this test stays hermetic.
    export MORPH_SYSTEM_CONFIG_DIR="$tmpdir/system-config"
    mkdir -p "$MORPH_SYSTEM_CONFIG_DIR"
    unset WAYLAND_DISPLAY
    : > "$MORPH_STARTUP_LOG_FILE"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_startup.sh"

    assert_equals "wayland-test-socket" "${WAYLAND_DISPLAY:-}" "nested WAYLAND_DISPLAY"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Nested clients use WAYLAND_DISPLAY=wayland-test-socket."

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_reload_runs_user_hook_file_with_helpers() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    cat > "$tmpdir/reload-hook.sh" <<'EOF'
#!/bin/sh
reload "$HOOK_SERVICE"
EOF
    chmod +x "$tmpdir/reload-hook.sh"

    mkdir -p "$tmpdir/bin"
    cat > "$tmpdir/bin/stdbuf" <<'EOF'
#!/bin/sh
shift 2
"$@"
EOF
    cat > "$tmpdir/bin/dummy-service" <<'EOF'
#!/bin/sh
printf 'dummy service reload output\n'
EOF
    chmod +x "$tmpdir/bin/stdbuf" "$tmpdir/bin/dummy-service"

    export PATH="$tmpdir/bin:$PATH"
    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_STARTUP_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export MORPH_USER_RELOAD_HOOK_CMD="$tmpdir/reload-hook.sh"
    export HOOK_SERVICE="$tmpdir/bin/dummy-service"
    export PKILL_LOG_FILE="$tmpdir/pkill.log"
    : > "$MORPH_STARTUP_LOG_FILE"
    : > "$MORPH_SHUTDOWN_LIST"
    : > "$PKILL_LOG_FILE"

    pkill() {
        printf '%s\n' "$*" >> "$PKILL_LOG_FILE"
        return 0
    }

    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_reload.sh"

    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Starting managed reload hook."
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Sourcing user reload hook file from config: $tmpdir/reload-hook.sh"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Reloading $tmpdir/bin/dummy-service."
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Managed reload hook completed."
    assert_file_contains "$PKILL_LOG_FILE" "-x dummy-service"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_startup_native_runs_managed_portals() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/scripts" "$tmpdir/config" "$tmpdir/libexec"
    ln -s "$repo_root/scripts/system_startup.sh" "$tmpdir/scripts/system_startup.sh"
    ln -s "$repo_root/scripts/shell-helpers.sh" "$tmpdir/scripts/shell-helpers.sh"
    for portal_bin in xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk; do
        cat > "$tmpdir/libexec/$portal_bin" <<'EOF'
#!/bin/sh
exit 0
EOF
        chmod +x "$tmpdir/libexec/$portal_bin"
    done
    cat > "$tmpdir/config/portals" <<'EOF'
#!/bin/sh
morph_start_portals() {
    : "${PORTAL_MARKER_FILE:?PORTAL_MARKER_FILE is not set}"
    printf 'managed portals called\n' > "$PORTAL_MARKER_FILE"
    log_startup INFO "Stub portal runtime started."
}
EOF
    chmod +x "$tmpdir/config/portals"

    export COMP_ROOT_DIR="$tmpdir"
    export MORPH_HELPER_LIB="$tmpdir/scripts/shell-helpers.sh"
    export MORPH_STARTUP_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SESSION_MODE=native
    export MORPH_SYSTEM_CONFIG_DIR="$tmpdir/config"
    export MORPH_PORTAL_LIBEXEC_DIR="$tmpdir/libexec"
    export XDG_CONFIG_HOME="$tmpdir/xdg-config"
    export PORTAL_MARKER_FILE="$tmpdir/portal-marker"
    : > "$MORPH_STARTUP_LOG_FILE"

    # Source the managed runtime directly so the test can validate ordering
    # without having to start the full compositor binary.
    # shellcheck disable=SC1091
    . "$tmpdir/scripts/system_startup.sh"

    assert_file_contains "$PORTAL_MARKER_FILE" "managed portals called"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Portal executable directory: $tmpdir/libexec."
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Loaded managed portals file: $tmpdir/config/portals."
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Stub portal runtime started."

    unset MORPH_PORTAL_LIBEXEC_DIR
    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_startup_runs_user_hook_command() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_STARTUP_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SESSION_MODE=nested
    export WLR_WL_SOCKET="wayland-test-socket"
    export XDG_CONFIG_HOME="$tmpdir/xdg-config"
    export USER_STARTUP_MARKER="$tmpdir/user-startup.marker"
    export MORPH_USER_STARTUP_HOOK_CMD='printf "user startup hook\n" > "$USER_STARTUP_MARKER"'
    : > "$MORPH_STARTUP_LOG_FILE"

    # In managed mode the startup runtime receives the resolved config command
    # through the environment rather than reparsing the config itself.
    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_startup.sh"

    assert_file_contains "$USER_STARTUP_MARKER" "user startup hook"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Running user startup hook from config."

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_startup_runs_variable_based_user_hook_file() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/system-config" "$tmpdir/bin"
    cat > "$tmpdir/system-config/startup.sh" <<'EOF'
#!/bin/sh
launch "$HOOK_SERVICE"
log_startup INFO "Variable-based startup hook ran."
EOF
    cat > "$tmpdir/bin/stdbuf" <<'EOF'
#!/bin/sh
shift 2
"$@"
EOF
    cat > "$tmpdir/bin/hook-service" <<'EOF'
#!/bin/sh
printf 'hook service output\n'
EOF
    chmod +x "$tmpdir/system-config/startup.sh" "$tmpdir/bin/stdbuf" "$tmpdir/bin/hook-service"

    export PATH="$tmpdir/bin:$PATH"
    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_STARTUP_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export MORPH_SYSTEM_CONFIG_DIR="$tmpdir/system-config"
    export MORPH_USER_STARTUP_HOOK_CMD='${MORPH_SYSTEM_CONFIG_DIR}/startup.sh'
    export HOOK_SERVICE="$tmpdir/bin/hook-service"
    export XDG_CONFIG_HOME="$tmpdir/xdg-config"
    export MORPH_SESSION_MODE=nested
    export WLR_WL_SOCKET="wayland-test-socket"
    : > "$MORPH_STARTUP_LOG_FILE"
    : > "$MORPH_SHUTDOWN_LIST"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_startup.sh"

    count=$(grep -c '^hook-service$' "$MORPH_SHUTDOWN_LIST" || true)
    assert_equals "1" "$count" "variable-based startup hook shutdown tracker count"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Sourcing user startup hook file from config: $tmpdir/system-config/startup.sh"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Variable-based startup hook ran."

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_morph_resolve_hook_path_expands_tilde() {
    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"

    # An unquoted ~ in a case pattern is tilde-expanded by the shell, which used
    # to make literal "~/..." hook values resolve to themselves and never load.
    resolved=$(morph_resolve_hook_path '~/.config/morph/startup.sh')
    assert_equals "$HOME/.config/morph/startup.sh" "$resolved" "tilde hook path"

    resolved=$(morph_resolve_hook_path '~')
    assert_equals "$HOME" "$resolved" "bare tilde hook path"

    resolved=$(morph_resolve_hook_path '/etc/morph/startup.sh')
    assert_equals "/etc/morph/startup.sh" "$resolved" "absolute hook path"

    saved_user_config_dir=${MORPH_USER_CONFIG_DIR:-}
    MORPH_USER_CONFIG_DIR="$HOME/.config/morph"
    resolved=$(morph_resolve_hook_path '${MORPH_USER_CONFIG_DIR}/startup.sh')
    MORPH_USER_CONFIG_DIR=$saved_user_config_dir
    assert_equals "$HOME/.config/morph/startup.sh" "$resolved" "variable hook path"
}

test_system_startup_sources_tilde_user_hook() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    # HOME drives tilde expansion, so point it at the sandbox for this case and
    # restore it afterwards; later tests still expect the caller's real HOME.
    saved_home=$HOME
    trap 'HOME=$saved_home; export HOME; rm -rf "$tmpdir"' EXIT HUP INT TERM
    mkdir -p "$tmpdir/home/.config/morph" "$tmpdir/system-config"
    cat > "$tmpdir/home/.config/morph/startup.sh" <<'EOF'
#!/bin/sh
log_startup INFO "Tilde user startup hook ran."
EOF
    chmod +x "$tmpdir/home/.config/morph/startup.sh"

    export HOME="$tmpdir/home"
    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_STARTUP_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SYSTEM_CONFIG_DIR="$tmpdir/system-config"
    export MORPH_USER_STARTUP_HOOK_CMD='~/.config/morph/startup.sh'
    export XDG_CONFIG_HOME="$tmpdir/home/.config"
    export MORPH_SESSION_MODE=nested
    export WLR_WL_SOCKET="wayland-test-socket"
    unset MORPH_USER_CONFIG_DIR
    : > "$MORPH_STARTUP_LOG_FILE"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_startup.sh"

    assert_file_contains \
        "$MORPH_STARTUP_LOG_FILE" \
        "Sourcing user startup hook file from config: $tmpdir/home/.config/morph/startup.sh"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Tilde user startup hook ran."

    HOME=$saved_home
    export HOME
    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_startup_falls_back_to_system_hook_when_user_hook_missing() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/system-config"
    cat > "$tmpdir/system-config/startup.sh" <<'EOF'
#!/bin/sh
log_startup INFO "Packaged system startup hook ran."
EOF
    chmod +x "$tmpdir/system-config/startup.sh"

    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_STARTUP_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SYSTEM_CONFIG_DIR="$tmpdir/system-config"
    # The shipped morph.conf points here, but a fresh install has no user config
    # directory yet, so the packaged hook must take over.
    export MORPH_USER_CONFIG_DIR="$tmpdir/user-config/morph"
    export MORPH_USER_STARTUP_HOOK_CMD='${MORPH_USER_CONFIG_DIR}/startup.sh'
    export XDG_CONFIG_HOME="$tmpdir/xdg-config"
    export MORPH_SESSION_MODE=nested
    export WLR_WL_SOCKET="wayland-test-socket"
    : > "$MORPH_STARTUP_LOG_FILE"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_startup.sh"

    assert_file_contains \
        "$MORPH_STARTUP_LOG_FILE" \
        "Configured user startup hook file is not readable: $tmpdir/user-config/morph/startup.sh"
    assert_file_contains \
        "$MORPH_STARTUP_LOG_FILE" \
        "Sourcing system startup hook: $tmpdir/system-config/startup.sh"
    assert_file_contains "$MORPH_STARTUP_LOG_FILE" "Packaged system startup hook ran."

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_morph_config_hook_from_file_reads_hooks_section() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    cat > "$tmpdir/config.conf" <<'EOF'
[hooks]
startup = /tmp/startup-hook
shutdown = echo shutdown
reload = /tmp/reload-hook
EOF

    export CURRENT_LOG_FILE="$tmpdir/helpers.log"
    : > "$CURRENT_LOG_FILE"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"

    startup_hook=$(morph_config_hook_from_file startup "$tmpdir/config.conf")
    shutdown_hook=$(morph_config_hook_from_file shutdown "$tmpdir/config.conf")
    reload_hook=$(morph_config_hook_from_file reload "$tmpdir/config.conf")

    assert_equals "/tmp/startup-hook" "$startup_hook" "startup hook command"
    assert_equals "echo shutdown" "$shutdown_hook" "shutdown hook command"
    assert_equals "/tmp/reload-hook" "$reload_hook" "reload hook command"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_morph_source_portals_reports_missing_executables() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/config" "$tmpdir/libexec"
    cat > "$tmpdir/libexec/xdg-desktop-portal" <<'EOF'
#!/bin/sh
exit 0
EOF
    chmod +x "$tmpdir/libexec/xdg-desktop-portal"
    cat > "$tmpdir/config/portals" <<'EOF'
#!/bin/sh
morph_start_portals() {
    :
}
EOF

    export CURRENT_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SESSION_MODE=native
    export MORPH_SYSTEM_CONFIG_DIR="$tmpdir/config"
    export MORPH_PORTAL_LIBEXEC_DIR="$tmpdir/libexec"
    export XDG_CONFIG_HOME="$tmpdir/xdg-config"
    : > "$CURRENT_LOG_FILE"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"

    set +e
    morph_source_portals
    status=$?
    set -e

    if [ "$status" -eq 0 ]; then
        fail "morph_source_portals must fail when the configured portal libexec directory is incomplete"
    fi

    assert_file_contains \
        "$CURRENT_LOG_FILE" \
        "MORPH_PORTAL_LIBEXEC_DIR was set but does not contain the complete default portal set: $tmpdir/libexec"
    assert_file_contains \
        "$CURRENT_LOG_FILE" \
        "Portal executable candidate is incomplete: $tmpdir/libexec (missing: xdg-desktop-portal-wlr, xdg-desktop-portal-gtk)"
    assert_file_contains \
        "$CURRENT_LOG_FILE" \
        "No complete xdg-desktop-portal default set found in MORPH_PORTAL_LIBEXEC_DIR or known libexec paths."

    unset MORPH_PORTAL_LIBEXEC_DIR
    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_portals_log_effective_runtime_values() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    export CURRENT_LOG_FILE="$tmpdir/startup.log"
    export MORPH_SESSION_MODE=native
    export MORPH_PORTAL_LIBEXEC_DIR="$tmpdir/libexec"
    export XDG_CURRENT_DESKTOP="GNOME"
    : > "$CURRENT_LOG_FILE"

    # shellcheck disable=SC1090
    . "$repo_root/scripts/shell-helpers.sh"
    # shellcheck disable=SC1090
    . "$repo_root/config/portals"

    launch_nokill() {
        :
    }
    pkill() {
        return 0
    }
    sleep() {
        :
    }

    morph_start_portals

    assert_file_contains \
        "$CURRENT_LOG_FILE" \
        "Set portal variables (QT_QPA_PLATFORM=wayland;xcb, GDK_BACKEND=wayland,x11, GTK_USE_PORTAL=1, XDG_CURRENT_DESKTOP=GNOME)."

    unset MORPH_PORTAL_LIBEXEC_DIR
    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_shutdown_cleans_registered_processes() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_SHUTDOWN_LOG_FILE="$tmpdir/shutdown.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export MORPH_SESSION_MODE=native
    export XDG_CONFIG_HOME="$tmpdir/xdg-config"
    export SHUTDOWN_ORDER_FILE="$tmpdir/shutdown-order.log"
    export MORPH_USER_SHUTDOWN_HOOK_CMD='printf "user hook\n" >> "$SHUTDOWN_ORDER_FILE"'
    : > "$MORPH_SHUTDOWN_LOG_FILE"
    printf '%s\n' dummy-service > "$MORPH_SHUTDOWN_LIST"
    export PKILL_LOG_FILE="$tmpdir/pkill.log"
    : > "$PKILL_LOG_FILE"

    pkill() {
        printf '%s\n' "cleanup $*" >> "$SHUTDOWN_ORDER_FILE"
        printf '%s\n' "$*" >> "$PKILL_LOG_FILE"
        case "$1" in
            -0)
                return 1
                ;;
            *)
                return 0
                ;;
        esac
    }
    sleep() {
        return 0
    }

    # Source the shutdown runtime so the shell overrides above can capture the
    # exact user-hook-before-cleanup ordering contract.
    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_shutdown.sh"

    first_line=$(sed -n '1p' "$SHUTDOWN_ORDER_FILE")
    assert_equals "user hook" "$first_line" "shutdown hook ordering"
    assert_file_contains "$MORPH_SHUTDOWN_LOG_FILE" "Sent SIGTERM to registered process: dummy-service."
    assert_file_contains "$MORPH_SHUTDOWN_LOG_FILE" "Stopping xdg-desktop-portal processes."
    assert_file_contains "$MORPH_SHUTDOWN_LOG_FILE" "Running user shutdown hook from config."
    assert_file_contains "$PKILL_LOG_FILE" "-x dummy-service"
    assert_file_contains "$PKILL_LOG_FILE" "-x xdg-desktop-portal-wlr|xdg-desktop-portal-gtk|xdg-desktop-portal-hyprland|xdg-desktop-portal-gnome|xdg-desktop-portal-kde|xdg-desktop-portal-lxqt|xdg-desktop-portal"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_shutdown_skips_missing_configured_hook_file() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    export COMP_ROOT_DIR="$repo_root"
    export MORPH_HELPER_LIB="$repo_root/scripts/shell-helpers.sh"
    export MORPH_SHUTDOWN_LOG_FILE="$tmpdir/shutdown.log"
    export MORPH_SHUTDOWN_LIST="$tmpdir/shutdown_list.nfo"
    export MORPH_SESSION_MODE=native
    export MORPH_USER_CONFIG_DIR="$tmpdir/user-config/morph"
    # Point the system dir at an empty location so this case stays hermetic and
    # covers "no user hook and no packaged hook" rather than the fallback path.
    export MORPH_SYSTEM_CONFIG_DIR="$tmpdir/system-config"
    mkdir -p "$MORPH_SYSTEM_CONFIG_DIR"
    export MORPH_USER_SHUTDOWN_HOOK_CMD='${MORPH_USER_CONFIG_DIR}/shutdown.sh'
    : > "$MORPH_SHUTDOWN_LOG_FILE"
    : > "$MORPH_SHUTDOWN_LIST"

    pkill() {
        return 0
    }
    sleep() {
        return 0
    }

    # Source the shutdown runtime directly to verify the no-install path where
    # the config points at the conventional user hook, but that file is absent.
    # shellcheck disable=SC1090
    . "$repo_root/scripts/system_shutdown.sh"

    assert_file_contains \
        "$MORPH_SHUTDOWN_LOG_FILE" \
        "Configured user shutdown hook file is not readable: $tmpdir/user-config/morph/shutdown.sh"
    assert_file_contains \
        "$MORPH_SHUTDOWN_LOG_FILE" \
        "No readable shutdown hook found"
    if grep -F -- "Running user shutdown hook from config." "$MORPH_SHUTDOWN_LOG_FILE" >/dev/null 2>&1; then
        fail "missing configured shutdown hook file must not be executed as a shell command"
    fi

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launcher_resolves_user_config_before_system_fallback() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/user-config/morph" "$tmpdir/system-config" "$tmpdir/state"
    cat > "$tmpdir/user-config/morph/morph.conf" <<'EOF'
[bind]
mods = Super
key = Return
action = exec
command = foot
EOF
    cat > "$tmpdir/system-config/morph.conf" <<'EOF'
[bind]
mods = Super
key = Q
action = quit
EOF

    run_launcher_resolve_only \
        XDG_CONFIG_HOME="$tmpdir/user-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_CONFIG_FILE="$tmpdir/system-config/morph.conf" \
        MORPH_RESOLVE_ONLY=1

    assert_log_field_contains \
        "$(launcher_startup_log_path "$tmpdir/state/morph")" \
        "Config file path" \
        "$tmpdir/user-config/morph/morph.conf (user config fallback)"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launcher_uses_system_config_when_user_config_is_missing() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/system-config" "$tmpdir/state" "$tmpdir/empty-config"
    cat > "$tmpdir/system-config/morph.conf" <<'EOF'
[bind]
mods = Super
key = Q
action = quit
EOF

    run_launcher_resolve_only \
        XDG_CONFIG_HOME="$tmpdir/empty-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_CONFIG_FILE="$tmpdir/system-config/morph.conf" \
        MORPH_RESOLVE_ONLY=1

    assert_log_field_contains \
        "$(launcher_startup_log_path "$tmpdir/state/morph")" \
        "Config file path" \
        "$tmpdir/system-config/morph.conf (system config fallback)"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launcher_fails_when_no_config_exists() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/empty-config" "$tmpdir/state"
    if run_launcher_resolve_only \
        XDG_CONFIG_HOME="$tmpdir/empty-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_CONFIG_FILE="$tmpdir/missing-system-config" \
        MORPH_ALLOW_BUILTIN_FALLBACK=0 \
        MORPH_RESOLVE_ONLY=1 >/dev/null 2>"$tmpdir/stderr.log"; then
        fail "launcher missing config path unexpectedly succeeded"
    else
        status=$?
    fi

    assert_command_fails "$status" "launcher missing config path"
    assert_file_contains "$tmpdir/stderr.log" "No readable morph config found in user or system locations."

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launcher_can_opt_into_builtin_fallback() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/empty-config" "$tmpdir/state"
    run_launcher_resolve_only \
        XDG_CONFIG_HOME="$tmpdir/empty-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_CONFIG_FILE="$tmpdir/missing-system-config" \
        MORPH_ALLOW_BUILTIN_FALLBACK=1 \
        MORPH_RESOLVE_ONLY=1

    assert_log_field_contains \
        "$(launcher_startup_log_path "$tmpdir/state/morph")" \
        "Builtin fallback enabled" \
        "1"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launcher_restores_caller_environment_over_file_layers() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/user-config/morph" "$tmpdir/system-config" "$tmpdir/state"
    cat > "$tmpdir/user-config/morph/morph.conf" <<'EOF'
[bind]
mods = Super
key = Return
action = exec
command = foot
EOF
    cat > "$tmpdir/system-config/environment" <<'EOF'
MORPH_DBG=0
EOF
    cat > "$tmpdir/user-config/morph/environment" <<'EOF'
MORPH_DBG=1
EOF

    run_launcher_resolve_only \
        XDG_CONFIG_HOME="$tmpdir/user-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_CONFIG_DIR="$tmpdir/system-config" \
        MORPH_RESOLVE_ONLY=1 \
        MORPH_DBG=2

    assert_log_field_contains \
        "$(launcher_startup_log_path "$tmpdir/state/morph")" \
        "MORPH_DBG" \
        "2"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launcher_uses_expected_mode_binary() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/empty-config" "$tmpdir/system-config" "$tmpdir/state"
    cat > "$tmpdir/system-config/morph.conf" <<'EOF'
[bind]
mods = Super
key = Q
action = quit
EOF

    run_launcher_resolve_only \
        XDG_CONFIG_HOME="$tmpdir/empty-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_CONFIG_FILE="$tmpdir/system-config/morph.conf" \
        MORPH_RESOLVE_ONLY=1

    assert_log_field_contains \
        "$(launcher_startup_log_path "$tmpdir/state/morph")" \
        "Compositor binary" \
        "$expected_morph_bin"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launcher_enables_x11_bridge_by_default_in_nested_mode() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/bin" "$tmpdir/empty-config" "$tmpdir/state"
    cat > "$tmpdir/bin/xdpyinfo" <<'EOF'
#!/bin/sh
exit 0
EOF
    chmod +x "$tmpdir/bin/xdpyinfo"

    run_launcher_resolve_only \
        PATH="$tmpdir/bin:$PATH" \
        DISPLAY=:42 \
        XDG_CONFIG_HOME="$tmpdir/empty-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_CONFIG_FILE="$repo_root/config/morph.conf" \
        MORPH_RESOLVE_ONLY=1

    startup_log="$tmpdir/state/morph/morph-nested-startup.log"
    assert_log_field_contains "$startup_log" "Selected session mode" "nested-x11"
    assert_log_field_contains "$startup_log" "MORPH_X11" "1"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_production_wrapper_resolves_system_config_with_repo_overrides() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    mkdir -p "$tmpdir/system-config" "$tmpdir/state" "$tmpdir/empty-config"
    cat > "$tmpdir/system-config/morph.conf" <<'EOF'
[bind]
mods = Super
key = Q
action = quit
EOF

    env -u DISPLAY -u WAYLAND_DISPLAY \
        XDG_CONFIG_HOME="$tmpdir/empty-config" \
        XDG_STATE_HOME="$tmpdir/state" \
        MORPH_SYSTEM_HOOK_DIR="$repo_root/scripts" \
        MORPH_SYSTEM_CONFIG_DIR="$repo_root/config" \
        MORPH_SYSTEM_CONFIG_FILE="$tmpdir/system-config/morph.conf" \
        MORPH_BIN=/bin/true \
        MORPH_RESOLVE_ONLY=1 \
        sh "$repo_root/scripts/morph-session"

    assert_log_field_contains \
        "$(launcher_startup_log_path "$tmpdir/state/morph")" \
        "Managed system hook directory" \
        "$repo_root/scripts"
    assert_log_field_contains \
        "$(launcher_startup_log_path "$tmpdir/state/morph")" \
        "Config file path" \
        "$tmpdir/system-config/morph.conf (system config fallback)"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_meson_install_manifest_lists_runtime_artifacts() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    manifest="$tmpdir/installed.json"
    meson introspect --installed "$repo_root/build" > "$manifest"

    assert_file_contains "$manifest" "/usr/bin/morph-session"
    assert_file_contains "$manifest" "/etc/morph/morph.conf"
    assert_file_contains "$manifest" "/etc/morph/system_startup.sh"
    assert_file_contains "$manifest" "/usr/share/wayland-sessions/morph.desktop"
    assert_file_contains "$manifest" "/usr/share/doc/morph/docs/CONFIG.md"
    assert_file_contains "$manifest" "/usr/share/doc/morph/docs/morph.conf.example"

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_system_uninstall_prints_manifest_without_removing() {
    tmpdir=$(make_tmpdir)
    trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

    output_file="$tmpdir/system-uninstall.out"
    sh "$repo_root/scripts/system-uninstall.sh" --builddir "$repo_root/build" --print-only > "$output_file"

    assert_file_contains "$output_file" "Installed artifacts for build dir: $repo_root/build"
    assert_file_contains "$output_file" "/usr/bin/morph-session"
    assert_file_contains "$output_file" "/etc/morph/morph.conf"
    assert_file_contains "$output_file" "Print-only mode active. No files were removed."

    rm -rf "$tmpdir"
    trap - EXIT HUP INT TERM
}

test_launch_helpers_track_expected_processes
test_reload_helper_restarts_without_duplicate_shutdown_entries
test_reload_once_starts_component_when_missing
test_reload_once_skips_already_running_component
test_system_startup_nested_sets_wayland_display
test_system_reload_runs_user_hook_file_with_helpers
test_system_startup_native_runs_managed_portals
test_system_startup_runs_user_hook_command
test_system_startup_runs_variable_based_user_hook_file
test_morph_resolve_hook_path_expands_tilde
test_system_startup_sources_tilde_user_hook
test_system_startup_falls_back_to_system_hook_when_user_hook_missing
test_morph_config_hook_from_file_reads_hooks_section
test_morph_source_portals_reports_missing_executables
test_portals_log_effective_runtime_values
test_system_shutdown_cleans_registered_processes
test_system_shutdown_skips_missing_configured_hook_file
test_launcher_resolves_user_config_before_system_fallback
test_launcher_uses_system_config_when_user_config_is_missing
test_launcher_fails_when_no_config_exists
test_launcher_can_opt_into_builtin_fallback
test_launcher_restores_caller_environment_over_file_layers
test_launcher_uses_expected_mode_binary
test_launcher_enables_x11_bridge_by_default_in_nested_mode
test_production_wrapper_resolves_system_config_with_repo_overrides
test_meson_install_manifest_lists_runtime_artifacts
test_system_uninstall_prints_manifest_without_removing
