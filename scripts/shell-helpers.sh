#!/bin/sh
# Shared stackcomp startup/shutdown helpers.
# - Provides unified startup/shutdown logging helpers.
# - Starts background services with optional shutdown registration.
# - Detects reachable X11 displays for nested-mode startup decisions.
# - Runs stackcomp with capture into startup logs.
# - Emits compact per-run error summaries from core/crash logs.
################################################################################

# Logging
# ==============================================================================

# Write one timestamped log line to CURRENT_LOG_FILE.
log_message() {
    level="$1"
    shift
    msg="$*"
    
    # Hard-fail if caller forgot to set destination log file.
    : "${CURRENT_LOG_FILE:?Error: CURRENT_LOG_FILE is not set!}"
    
    printf '[%s] %s: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$level" "$msg" >> "$CURRENT_LOG_FILE"
}

# Startup logger wrapper.
log_startup() {
    log_message "$@"
}

# Shutdown logger wrapper.
log_shutdown() {
    log_message "$@"
}


# Process launch helpers
# ==============================================================================

# Start a service, stream output to startup log, and register it for shutdown cleanup.
launch() {
    cmd_name="$1"
    log_startup INFO "starting $cmd_name (registered for automatic shutdown)"
    
    # Store basename so shutdown can stop the same executable reliably.
    if [ -n "$STACKCOMP_SHUTDOWN_LIST" ]; then
        basename "$cmd_name" >> "$STACKCOMP_SHUTDOWN_LIST"
    fi

    stdbuf -oL -eL "$@" 2>&1 | while IFS= read -r line; do
        log_startup INFO "[$cmd_name] $line"
    done &
}

# Start a service and log output, but do not add it to shutdown cleanup.
launch_nokill() {
    cmd_name="$1"
    log_startup INFO "starting $cmd_name (NOT registered for shutdown)"

    stdbuf -oL -eL "$@" 2>&1 | while IFS= read -r line; do
        log_startup INFO "[$cmd_name] $line"
    done &
}

# Display/session probe helpers
# ==============================================================================

# Return success if the given X11 display can be queried.
x11_display_reachable_on() {
    cand="$1"
    if [ -z "$cand" ]; then
        return 1
    fi
    if command -v xdpyinfo >/dev/null 2>&1; then
        DISPLAY="$cand" xdpyinfo >/dev/null 2>&1
        return $?
    fi
    if command -v xset >/dev/null 2>&1; then
        DISPLAY="$cand" xset q >/dev/null 2>&1
        return $?
    fi
    return 1
}

# Choose reachable X11 display; prefer DISPLAY, then probe /tmp/.X11-unix.
# Exports CHOSEN_X11_DISPLAY and X11_PROBE_NOTE for caller diagnostics.
choose_reachable_x11_display() {
    if [ -n "$DISPLAY" ] && x11_display_reachable_on "$DISPLAY"; then
        CHOSEN_X11_DISPLAY="$DISPLAY"
        X11_PROBE_NOTE="x11 probe succeeded for DISPLAY=$DISPLAY"
        return 0
    fi

    for socket in /tmp/.X11-unix/X*; do
        [ -e "$socket" ] || continue
        base=$(basename "$socket")
        cand=":${base#X}"
        if x11_display_reachable_on "$cand"; then
            CHOSEN_X11_DISPLAY="$cand"
            X11_PROBE_NOTE="x11 probe selected fallback DISPLAY=$cand"
            return 0
        fi
    done

    if [ -n "$DISPLAY" ]; then
        X11_PROBE_NOTE="DISPLAY is set ($DISPLAY) but no reachable X11 display was found"
    else
        X11_PROBE_NOTE="DISPLAY is not set and no reachable X11 display was found"
    fi
    return 1
}

# Print file line count, or 0 if file is missing.
line_count_or_zero() {
    f="$1"
    if [ -f "$f" ]; then
        wc -l < "$f"
    else
        printf '0\n'
    fi
}

# Runtime capture helpers
# ==============================================================================

# Run stackcomp and mirror stdout/stderr into the startup log via FIFO+tee.
# Expects LOG_DIR, STACKCOMP_STARTUP_LOG_FILE, COMP_ROOT_DIR, CONFIG_FILE, LOG_FILE, CRASH_LOG_FILE,
# STACKCOMP_LOG_LEVEL and STACKCOMP_ENABLE_CRASH_HANDLER.
run_stackcomp_with_capture() {
    fifo_path=$(mktemp -u "$LOG_DIR/stackcomp-output.XXXXXX.fifo") || return 1
    if ! mkfifo "$fifo_path"; then
        log_startup ERROR "Failed to create output capture FIFO at $fifo_path"
        return 1
    fi

    tee -a "$STACKCOMP_STARTUP_LOG_FILE" <"$fifo_path" &
    tee_pid=$!

    stackcomp_bin="$COMP_ROOT_DIR/build/stackcomp"
    level="${STACKCOMP_LOG_LEVEL:-error}"
    enable_crash="${STACKCOMP_ENABLE_CRASH_HANDLER:-0}"

    if [ "$enable_crash" = "1" ]; then
        "$stackcomp_bin" -c "$CONFIG_FILE" --log-level "$level" --log-file "$LOG_FILE" --crash-log "$CRASH_LOG_FILE" >"$fifo_path" 2>&1
    else
        "$stackcomp_bin" -c "$CONFIG_FILE" --log-level "$level" --log-file "$LOG_FILE" --no-crash-handler >"$fifo_path" 2>&1
    fi
    cmd_status=$?

    wait "$tee_pid"
    rm -f "$fifo_path"
    return "$cmd_status"
}

# Error summary helpers
# ==============================================================================

# Emit a compact error summary for the current run only.
# Expects LOG_DIR, LOG_FILE, CRASH_LOG_FILE, CORE_LOG_BASELINE, CRASH_LOG_BASELINE, STACKCOMP_STARTUP_LOG_FILE.
dump_recent_error_summary() {
    pattern='warn|warning|error|failed|crash|segv|sig'
    log_startup INFO "Automatic error summary (current run only)"

    summary_tmp=$(mktemp "$LOG_DIR/stackcomp-error-summary.XXXXXX") || {
        log_startup ERROR "summary-skip: failed to create temporary summary file"
        return
    }

    for pair in \
        "$LOG_FILE:$CORE_LOG_BASELINE" \
        "$CRASH_LOG_FILE:$CRASH_LOG_BASELINE"; do
        f=${pair%%:*}
        baseline=${pair##*:}
        if [ ! -f "$f" ]; then
            log_startup INFO "summary-skip: file not found: $f"
            continue
        fi

        start_line=$((baseline + 1))
        log_startup INFO "summary-source: $f (from line $start_line)"
        if command -v rg >/dev/null 2>&1; then
            matches=$(tail -n +"$start_line" "$f" 2>/dev/null | rg -n -i "$pattern" 2>/dev/null | tail -n 60)
        else
            matches=$(tail -n +"$start_line" "$f" 2>/dev/null | grep -n -i -E "$pattern" 2>/dev/null | tail -n 60)
        fi

        if [ -n "$matches" ]; then
            printf '%s\n' "$matches" >> "$summary_tmp"
        else
            log_startup INFO "summary-source had no matching lines: $f"
        fi
    done

    if [ -s "$summary_tmp" ]; then
        awk '!seen[$0]++' "$summary_tmp" | sed 's/^/[error-scan] /' | tee -a "$STACKCOMP_STARTUP_LOG_FILE"
    else
        log_startup INFO "summary had no matching lines in current run"
    fi
    rm -f "$summary_tmp"
}