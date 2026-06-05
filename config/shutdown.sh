#!/bin/sh
# This is the shutdown script for stackcomp. It is called automatically when the
# compositor exits, to clean up any remaining processes and services that were
# started in the main session. 
#
# The script performs a series of cleanup steps, including stopping desktop
# components, panels, and XDG desktop portals. It also includes a gentle escalation
# mechanism to ensure that all processes are terminated, first with SIGTERM and then with SIGKILL if necessary.
#
# The script relies on environment variables to determine the mode (nested or native)
# and to configure logging. Make sure to set these variables correctly before running.
#
# Additional helper functions for logging and launching services are defined in
# scripts/shell-helpers.sh and sourced here. This allows both startup.sh and
# shutdown.sh to use the same logging functions and maintain consistent log formatting.
#
# log_shutdown <level> <message> is used for logging, and the messages will
# be written to the shutdown log file defined in $STACKCOMP_SHUTDOWN_LOG_FILE.
################################################################################

# Activate Helper functions for logging and launching services
CURRENT_LOG_FILE="${STACKCOMP_SHUTDOWN_LOG_FILE:?STACKCOMP_SHUTDOWN_LOG_FILE is not set}"
source "$HOME/scratch-compositor/scripts/shell-helpers.sh"

log_shutdown INFO "Starting session cleanup"

# Nested Mode
# ==============================================================================
# Are we in X11/Wayland windowed mode? If so, global cleanup steps will be skipped
# because only test clients were started.
if [ "$WLR_BACKENDS" = "x11" ] || [ "$WLR_BACKENDS" = "wayland" ]; then
    log_shutdown INFO "Nested mode ($WLR_BACKENDS) detected. Skipping global component cleanup."
    
    # If specific test clients were started in startup.sh
    # (for example alacritty), they can be terminated explicitly here, e.g. with:
    # pkill -f "alacritty.*$WAYLAND_DISPLAY"
    
    log_shutdown INFO "Session cleanup completed (nested mode)"
    exit 0
fi

# Native Mode
# ==============================================================================
log_shutdown INFO "Native Wayland mode detected. Performing full session cleanup."

# Prüfen, ob die Datei existiert und gefüllt ist
if [ ! -s "$STACKCOMP_SHUTDOWN_LIST" ]; then
    log_shutdown INFO "No programs registered to kill."
else
    log_shutdown INFO "Performing dynamic session cleanup."

    # Sanfter Kill (SIGTERM) über alle registrierten Programme
    while IFS= read -r prog || [ -n "$prog" ]; do
        # Leere Zeilen überspringen
        [ -z "$prog" ] && continue
        
        if pkill -x "$prog" >/dev/null 2>&1; then
            log_shutdown INFO "Sent SIGTERM to dynamically registered: $prog"
        fi
    done < "$STACKCOMP_SHUTDOWN_LIST"

    # Portale zusätzlich beenden (falls nicht via launch gestartet)
    log_shutdown INFO "stopping xdg-desktop-portal processes"
    pkill -x "xdg-desktop-portal-wlr|xdg-desktop-portal-gtk|xdg-desktop-portal" 2>/dev/null

    # Gentle escalation (SIGTERM -> SIGKILL)
    sleep 1

    # Harter Nachdruck (SIGKILL)
    while IFS= read -r prog || [ -n "$prog" ]; do
        [ -z "$prog" ] && continue
        
        if pkill -0 -x "$prog" >/dev/null 2>&1; then
            log_shutdown ERROR "$prog did not exit after SIGTERM; forcing SIGKILL"
            pkill -9 -x "$prog" 2>/dev/null
        fi
    done < "$STACKCOMP_SHUTDOWN_LIST"
    
    # Sicherheitshalber nochmal die Portale prüfen
    for portal in xdg-desktop-portal-wlr xdg-desktop-portal-gtk xdg-desktop-portal; do
        if pkill -0 -x "$portal" >/dev/null 2>&1; then
            pkill -9 -x "$portal" 2>/dev/null
        fi
    done
fi

# ==============================================================================
log_shutdown INFO "session cleanup completed"
