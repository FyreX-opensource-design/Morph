#!/bin/sh
# This helper script defines common functions for logging and launching services,
# used by both startup.sh and shutdown.sh.
#
# It relies on the environment variable CURRENT_LOG_FILE to determine where to
# write logs, which should be set by the caller (startup.sh or shutdown.sh) before
# sourcing this helper.

# Function writes flexibly to the file defined in $CURRENT_LOG_FILE
log_message() {
    level="$1"
    shift
    msg="$*"
    
    # Fallback in case defining the log file was forgotten
    : "${CURRENT_LOG_FILE:?Error: CURRENT_LOG_FILE is not set!}"
    
    printf '[%s] %s: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$level" "$msg" >> "$CURRENT_LOG_FILE"
}

# Wrappers for function names used in startup.sh and shutdown.sh.
# Add new wrappers if needed.
log_startup() {
    log_message "$@"
}

log_shutdown() {
    log_message "$@"
}


# 1. LAUNCH (with registration for automatic kill)
launch() {
    cmd_name="$1"
    log_startup INFO "starting $cmd_name (registered for automatic shutdown)"
    
    # Save the plain program name (without path) to the file, if defined
    if [ -n "$STACKCOMP_SHUTDOWN_LIST" ]; then
        # basename ensures /usr/bin/sfwbar becomes just sfwbar
        basename "$cmd_name" >> "$STACKCOMP_SHUTDOWN_LIST"
    fi

    stdbuf -oL -eL "$@" 2>&1 | while IFS= read -r line; do
        log_startup INFO "[$cmd_name] $line"
    done &
}

# 2. LAUNCH_NOKILL (without registration, e.g. for core components)
launch_nokill() {
    cmd_name="$1"
    log_startup INFO "starting $cmd_name (NOT registered for shutdown)"

    stdbuf -oL -eL "$@" 2>&1 | while IFS= read -r line; do
        log_startup INFO "[$cmd_name] $line"
    done &
}