#!/bin/sh
# Managed shutdown runtime for morph sessions.
# - Activates shared shutdown logging and runtime checks.
# - Cleans up registered session processes in nested and native sessions.
# - Stops portal runtime infrastructure in native sessions.
################################################################################

# Activate helper functions for shutdown logging and shared runtime checks.
# shellcheck disable=SC2034
CURRENT_LOG_FILE="${MORPH_SHUTDOWN_LOG_FILE:?MORPH_SHUTDOWN_LOG_FILE is not set}"
HELPER_LIB="${MORPH_HELPER_LIB:-$(dirname "$(readlink -f "$0")")/shell-helpers.sh}"
. "$HELPER_LIB"

log_shutdown INFO "Starting session cleanup."

# Run the user shutdown hook before managed cleanup so manual session teardown
# can still observe the original runtime state.
morph_run_optional_user_hook shutdown "${MORPH_USER_SHUTDOWN_HOOK_CMD:-}"

# Stop every process recorded through launch()/launch_nested() and escalate
# only when a registered process ignores the initial SIGTERM.
run_registered_shutdown_cleanup() {
    if [ ! -s "$MORPH_SHUTDOWN_LIST" ]; then
        log_shutdown INFO "No programs registered to kill."
        return 0
    fi

    log_shutdown INFO "Performing dynamic session cleanup."

    # Registered helpers are part of the managed lifecycle in both nested and
    # native sessions, so shutdown must always process the tracker file.
    while IFS= read -r prog || [ -n "$prog" ]; do
        [ -z "$prog" ] && continue

        # The tracker stores basenames so shutdown can match services even when
        # startup used absolute executable paths or wrapper scripts.
        if pkill -x "$prog" >/dev/null 2>&1; then
            log_shutdown INFO "Sent SIGTERM to registered process: $prog."
        fi
    done < "$MORPH_SHUTDOWN_LIST"

    sleep 1

    while IFS= read -r prog || [ -n "$prog" ]; do
        [ -z "$prog" ] && continue

        if pkill -0 -x "$prog" >/dev/null 2>&1; then
            log_shutdown ERROR "$prog did not exit after SIGTERM. Forcing SIGKILL."
            pkill -9 -x "$prog" 2>/dev/null
        fi
    done < "$MORPH_SHUTDOWN_LIST"
}

# Nested Mode
# ==============================================================================
if morph_session_is_nested; then
    log_shutdown INFO "Nested mode detected. Cleaning registered nested helpers only."
    run_registered_shutdown_cleanup
    log_shutdown INFO "Session cleanup completed (nested mode)."
    exit 0
fi

# Native Mode
# ==============================================================================
log_shutdown INFO "Native Wayland mode detected. Performing full session cleanup."
run_registered_shutdown_cleanup

# Portals are native-only runtime plumbing and are not tracked via launch().
log_shutdown INFO "Stopping xdg-desktop-portal processes."
pkill -x "xdg-desktop-portal-wlr|xdg-desktop-portal-gtk|xdg-desktop-portal-hyprland|xdg-desktop-portal-gnome|xdg-desktop-portal-kde|xdg-desktop-portal-lxqt|xdg-desktop-portal" 2>/dev/null

for portal in xdg-desktop-portal-wlr xdg-desktop-portal-gtk xdg-desktop-portal-hyprland xdg-desktop-portal-gnome xdg-desktop-portal-kde xdg-desktop-portal-lxqt xdg-desktop-portal; do
    if pkill -0 -x "$portal" >/dev/null 2>&1; then
        # The broker/backends are runtime infrastructure. Force-stop leftovers so
        # the next session can rebuild a clean portal stack.
        pkill -9 -x "$portal" 2>/dev/null
    fi
done

# ==============================================================================
log_shutdown INFO "Session cleanup completed."
