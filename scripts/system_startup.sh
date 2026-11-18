#!/bin/sh
# Managed startup runtime for morph startup hooks.
# - Activates shared startup helpers and logging.
# - Prepares nested client environment before user services run.
# - Starts managed portal runtime for native sessions.
################################################################################

# Activate helper functions for startup logging and managed launch helpers.
CURRENT_LOG_FILE="${MORPH_STARTUP_LOG_FILE:?MORPH_STARTUP_LOG_FILE is not set}"
HELPER_LIB="${MORPH_HELPER_LIB:-$(dirname "$(readlink -f "$0")")/shell-helpers.sh}"
. "$HELPER_LIB"

if morph_session_is_nested; then
    # Nested helper clients must target the compositor socket selected by the launcher.
    if [ -n "${WLR_WL_SOCKET:-}" ]; then
        export WAYLAND_DISPLAY="$WLR_WL_SOCKET"
    elif [ -z "${WAYLAND_DISPLAY:-}" ]; then
        # Keep a deterministic fallback name for tests and for launchers that
        # already decided on nested mode before setting a socket explicitly.
        export WAYLAND_DISPLAY="wayland-nested"
    fi

    log_startup INFO "Nested mode detected. Running startup hook with nested helpers."
    log_startup INFO "Nested clients use WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-unset}."
else
    log_startup INFO "Native Wayland mode detected. Starting managed session services."
    # Treat portal loading failures as a managed runtime error, but keep the
    # user startup hook alive so session components can still start.
    if morph_source_portals; then
        # Portals must come up before user autostarts so toolkit clients see
        # the intended desktop integration from their first connection onward.
        morph_start_portals
    else
        log_startup ERROR "Portal startup skipped because portal configuration failed to load."
    fi
fi

# The managed runtime prepares the session first, then hands control to the
# configured user startup hook or its conventional XDG fallback.
morph_run_optional_user_hook startup "${MORPH_USER_STARTUP_HOOK_CMD:-}"
