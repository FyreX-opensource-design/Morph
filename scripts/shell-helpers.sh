#!/bin/sh
# Shared Morph startup/shutdown helpers.
# - Provides unified startup/shutdown logging helpers.
# - Starts background services with optional shutdown registration.
# - Detects reachable X11 displays for nested-mode startup decisions.
# - Runs morph with capture into startup logs.
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

# Morph logger wrapper.
log_morph() {
    level="$1"
    shift
    msg="$*"

    # Child process output is already tagged by launch_logged(); only untagged
    # launcher messages get the explicit Morph source prefix.
    case "$msg" in
        \[*)
            log_message "$level" "$msg"
            ;;
        *)
            log_message "$level" "[morph] $msg"
            ;;
    esac
}

# Startup logger wrapper.
log_startup() {
    log_morph "$@"
}

# Shutdown logger wrapper.
log_shutdown() {
    log_morph "$@"
}

# Return shell truth for common environment flag values.
morph_env_flag_is_enabled() {
    case "${1:-}" in
        1|true|TRUE|yes|YES|on|ON)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}


# Process launch helpers
# ==============================================================================

# Return success when the compositor runs nested under X11 or Wayland.
morph_session_is_nested() {
    # Prefer the launcher-provided mode so hooks do not have to infer runtime
    # state from backend strings when a more explicit source is available.
    if [ -n "${MORPH_SESSION_MODE:-}" ]; then
        [ "$MORPH_SESSION_MODE" = "nested" ]
        return $?
    fi

    [ "${WLR_BACKENDS:-}" = "x11" ] || [ "${WLR_BACKENDS:-}" = "wayland" ]
}

# Register one executable basename once for managed shutdown cleanup.
morph_register_shutdown_program() {
    prog_name="$1"

    if [ -z "${MORPH_SHUTDOWN_LIST:-}" ]; then
        return 0
    fi

    if [ -f "$MORPH_SHUTDOWN_LIST" ] && grep -Fx -- "$prog_name" "$MORPH_SHUTDOWN_LIST" >/dev/null 2>&1; then
        return 0
    fi

    printf '%s\n' "$prog_name" >> "$MORPH_SHUTDOWN_LIST"
}

# Start a service with line-buffered logging. Optionally register it for shutdown.
launch_logged() {
    register_mode="$1"
    shift

    cmd_name="$1"
    prog_name=$(basename "$cmd_name")

    case "$register_mode" in
        register)
            log_startup INFO "Starting $cmd_name (registered for automatic shutdown)."
            # Record only the executable basename so shutdown can match the
            # process even when the startup command used an absolute path.
            morph_register_shutdown_program "$prog_name"
            ;;
        skip)
            log_startup INFO "Starting $cmd_name (not registered for shutdown)."
            ;;
        *)
            log_startup ERROR "Internal launch error: unknown register mode '$register_mode'."
            return 1
            ;;
    esac

    stdbuf -oL -eL "$@" 2>&1 | while IFS= read -r line; do
        log_startup INFO "[$cmd_name] $line"
    done &
}

# Start a service, stream output to startup log, and register it for shutdown cleanup.
launch() {
    launch_logged register "$@"
}

# Start a service only for nested sessions and register it for shutdown cleanup.
launch_nested() {
    cmd_name="$1"

    if ! morph_session_is_nested; then
        log_startup INFO "Skipping $cmd_name (launch_nested only runs in nested sessions)."
        return 0
    fi

    # Nested startup preparation already selected the compositor socket, so the
    # helper only needs to enforce the session-mode contract here.
    launch_logged register "$@"
}

# Start a service and log output, but do not add it to shutdown cleanup.
launch_nokill() {
    launch_logged skip "$@"
}

# Restart one managed session component in-place during the reload hook.
reload() {
    cmd_name="$1"
    prog_name=$(basename "$cmd_name")

    # Reload is intended for the running session lifecycle, where the process
    # should be replaced and still remain part of managed shutdown tracking.
    log_message INFO "Reloading $cmd_name."

    if pkill -x "$prog_name" >/dev/null 2>&1; then
        log_message INFO "Stopped running instance for reload: $prog_name"
    else
        log_message INFO "No running instance found for reload: $prog_name"
    fi

    morph_register_shutdown_program "$prog_name"

    stdbuf -oL -eL "$@" 2>&1 | while IFS= read -r line; do
        log_message INFO "[reload:$cmd_name] $line"
    done &
}

# Start one managed session component during reload only when it is not already running.
reload_once() {
    cmd_name="$1"
    prog_name=$(basename "$cmd_name")

    # This helper exists for reload-time experiments or optional components
    # that should be brought up once without turning every reload into a restart.
    if pkill -0 -x "$prog_name" >/dev/null 2>&1; then
        log_message INFO "Skipping reload_once for already running component: $prog_name"
        morph_register_shutdown_program "$prog_name"
        return 0
    fi

    log_message INFO "Starting component through reload_once: $cmd_name"
    morph_register_shutdown_program "$prog_name"

    stdbuf -oL -eL "$@" 2>&1 | while IFS= read -r line; do
        log_message INFO "[reload_once:$cmd_name] $line"
    done &
}

# Portal startup helpers
# ==============================================================================

# Return the fixed managed config directory for portals and other runtime assets.
# The dev launcher may override this so the same runtime code can be exercised
# from the repository without pretending that repo files already live in /etc.
morph_managed_config_dir() {
    printf '%s\n' "${MORPH_SYSTEM_CONFIG_DIR:-/etc/morph}"
}

# Return the user config directory that can override managed runtime files.
morph_user_config_dir() {
    if [ -n "${MORPH_USER_CONFIG_DIR:-}" ]; then
        printf '%s\n' "$MORPH_USER_CONFIG_DIR"
    else
        printf '%s\n' "${XDG_CONFIG_HOME:-$HOME/.config}/morph"
    fi
}

# Return the libexec-style directory that contains Morph's default portal set.
# Distributions disagree here: Arch commonly uses /usr/lib, while Debian-like
# layouts use /usr/libexec. A caller-provided value remains the highest-priority
# escape hatch for local packaging or custom installs.
morph_portal_libexec_dir_has_default_set() {
    candidate="$1"

    [ -d "$candidate" ] || return 1
    [ -x "$candidate/xdg-desktop-portal" ] || return 1
    [ -x "$candidate/xdg-desktop-portal-wlr" ] || return 1
    [ -x "$candidate/xdg-desktop-portal-gtk" ] || return 1
}

morph_portal_libexec_dir() {
    if [ -n "${MORPH_PORTAL_LIBEXEC_DIR:-}" ]; then
        if morph_portal_libexec_dir_has_default_set "$MORPH_PORTAL_LIBEXEC_DIR"; then
            printf '%s\n' "$MORPH_PORTAL_LIBEXEC_DIR"
            return 0
        fi
        return 1
    fi

    for candidate in /usr/libexec /usr/lib /usr/local/libexec /usr/local/lib; do
        if morph_portal_libexec_dir_has_default_set "$candidate"; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

morph_log_portal_libexec_candidate() {
    candidate="$1"
    missing=""

    if [ ! -d "$candidate" ]; then
        log_startup WARN "Portal executable candidate is not a directory: $candidate"
        return 0
    fi

    for portal_bin in xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk; do
        if [ ! -x "$candidate/$portal_bin" ]; then
            if [ -z "$missing" ]; then
                missing="$portal_bin"
            else
                missing="$missing, $portal_bin"
            fi
        fi
    done

    if [ -n "$missing" ]; then
        log_startup WARN "Portal executable candidate is incomplete: $candidate (missing: $missing)"
    fi
}

morph_log_portal_libexec_resolution_failure() {
    if [ -n "${MORPH_PORTAL_LIBEXEC_DIR:-}" ]; then
        log_startup WARN "MORPH_PORTAL_LIBEXEC_DIR was set but does not contain the complete default portal set: $MORPH_PORTAL_LIBEXEC_DIR"
        morph_log_portal_libexec_candidate "$MORPH_PORTAL_LIBEXEC_DIR"
        return 0
    fi

    for candidate in /usr/libexec /usr/lib /usr/local/libexec /usr/local/lib; do
        morph_log_portal_libexec_candidate "$candidate"
    done
}


# Return the last configured hook command from a config file's [hooks] section.
morph_config_hook_from_file() {
    hook_kind="$1"
    config_file="$2"

    if [ ! -r "$config_file" ]; then
        return 1
    fi

    # Parse only the final value from [hooks] so repeated compatibility keys
    # behave like the compositor config loader instead of accumulating values.
    awk -v hook_kind="$hook_kind" '
        function trim(s) {
            sub(/^[ \t\r\n]+/, "", s)
            sub(/[ \t\r\n]+$/, "", s)
            return s
        }

        BEGIN {
            in_hooks = 0
            value = ""
        }

        {
            line = $0
            sub(/\r$/, "", line)
            if (line ~ /^[ \t]*#/ || line ~ /^[ \t]*$/) {
                next
            }
            if (line ~ /^[ \t]*\[/) {
                lower = tolower(trim(line))
                in_hooks = (lower == "[hooks]")
                next
            }
            if (!in_hooks) {
                next
            }

            eq = index(line, "=")
            if (!eq) {
                next
            }

            key = tolower(trim(substr(line, 1, eq - 1)))
            raw = trim(substr(line, eq + 1))

            if (hook_kind == "startup" && (key == "startup" || key == "on_startup")) {
                value = raw
            } else if (hook_kind == "shutdown" && (key == "shutdown" || key == "on_shutdown")) {
                value = raw
            } else if (hook_kind == "reload" && (key == "reload" || key == "on_reload")) {
                value = raw
            }
        }

        END {
            if (value != "") {
                print value
            }
        }
    ' "$config_file"
}

# Run a user hook command from the active config, or fall back to the standard
# XDG user hook path when the managed runtime was enabled without an explicit
# hook entry for that lifecycle phase.
morph_resolve_hook_path() {
    hook_cmd="$1"
    expanded_hook_cmd=""

    case "$hook_cmd" in
        "")
            return 1
            ;;
        *[\`\;\|\&\<\>\(\)\"\'\ \	]*)
            # Commands with shell syntax must stay shell commands. This helper
            # only resolves path-like hook entries so managed helpers remain
            # available when the hook file is sourced in-process.
            return 1
            ;;
    esac

    # The patterns must stay quoted: an unquoted ~ in a case pattern is itself
    # tilde-expanded to $HOME, so a literal "~/..." hook value would never match
    # and would silently fall through to the branch that expands nothing.
    case "$hook_cmd" in
        "~")
            expanded_hook_cmd="$HOME"
            ;;
        "~/"*)
            expanded_hook_cmd="$HOME/${hook_cmd#"~/"}"
            ;;
        *)
            expanded_hook_cmd="$hook_cmd"
            ;;
    esac

    expanded_hook_cmd=$(eval "printf '%s' \"$expanded_hook_cmd\"")

    if [ -z "$expanded_hook_cmd" ]; then
        return 1
    fi

    printf '%s\n' "$expanded_hook_cmd"
}

# Source the packaged hook from the system config dir when no user hook is
# readable. The shipped morph.conf points at ${MORPH_USER_CONFIG_DIR}/<kind>.sh,
# so without this a fresh install silently runs no hooks at all until the user
# creates ~/.config/morph. Mirrors the user -> system resolution of morph.conf.
morph_source_system_hook() {
    sys_hook_kind="$1"
    sys_tried_path="$2"
    sys_hook_path="$(morph_managed_config_dir)/$sys_hook_kind.sh"

    if [ "$sys_hook_path" = "$sys_tried_path" ] || [ ! -r "$sys_hook_path" ]; then
        log_morph INFO "No readable $sys_hook_kind hook found (tried $sys_tried_path and $sys_hook_path)."
        return 0
    fi

    log_morph INFO "Sourcing system $sys_hook_kind hook: $sys_hook_path"
    # shellcheck disable=SC1090
    . "$sys_hook_path"
    return $?
}

morph_run_optional_user_hook() {
    hook_kind="$1"
    hook_cmd="$2"
    hook_path="$(morph_user_config_dir)/$hook_kind.sh"
    hook_cmd_path=""

    if [ -n "$hook_cmd" ]; then
        # Resolve path-like hook entries first so config values such as
        # ${MORPH_USER_CONFIG_DIR}/startup.sh and ~/.config/... keep working
        # as sourceable hook files instead of being downgraded to sh -c calls.
        hook_cmd_path="$(morph_resolve_hook_path "$hook_cmd" || true)"

        if [ -n "$hook_cmd_path" ] && [ -r "$hook_cmd_path" ]; then
            log_morph INFO "Sourcing user $hook_kind hook file from config: $hook_cmd_path"
            # shellcheck disable=SC1090
            . "$hook_cmd_path"
            return $?
        fi
        if [ -n "$hook_cmd_path" ]; then
            # A configured path that does not exist must never be downgraded to
            # `sh -c <path>`; fall back to the packaged hook instead.
            log_morph INFO "Configured user $hook_kind hook file is not readable: $hook_cmd_path"
            morph_source_system_hook "$hook_kind" "$hook_cmd_path"
            return $?
        fi

        # Config-provided commands have highest priority because they are the
        # explicit lifecycle contract selected by the active config file.
        log_morph INFO "Running user $hook_kind hook from config."
        if ! sh -c "$hook_cmd"; then
            log_morph WARN "User $hook_kind hook from config exited with a non-zero status."
            return 1
        fi
        return 0
    fi

    if [ -r "$hook_path" ]; then
        # Source the fallback hook in the current shell so helper functions stay
        # available even when the user relies on the conventional XDG path
        # instead of configuring an explicit hook command in morph.conf.
        log_morph INFO "Sourcing default user $hook_kind hook: $hook_path"
        # shellcheck disable=SC1090
        . "$hook_path"
        return $?
    fi

    log_morph INFO "No user $hook_kind hook configured or found: $hook_path"
    morph_source_system_hook "$hook_kind" "$hook_path"
    return $?
}

# Source the managed portal definition and then an optional user override.
# This keeps portal startup in the runtime layer while still allowing advanced
# users to replace the implementation in one dedicated file.
morph_source_portals() {
    base_portals_file="$(morph_managed_config_dir)/portals"
    user_portals_file="$(morph_user_config_dir)/portals"

    if ! resolved_portal_libexec_dir="$(morph_portal_libexec_dir)"; then
        morph_log_portal_libexec_resolution_failure
        log_startup ERROR "No complete xdg-desktop-portal default set found in MORPH_PORTAL_LIBEXEC_DIR or known libexec paths."
        return 1
    fi
    MORPH_PORTAL_LIBEXEC_DIR="$resolved_portal_libexec_dir"
    export MORPH_PORTAL_LIBEXEC_DIR
    log_startup INFO "Portal executable directory: $MORPH_PORTAL_LIBEXEC_DIR."

    if [ ! -r "$base_portals_file" ]; then
        log_startup ERROR "Managed portals file is missing or unreadable: $base_portals_file."
        return 1
    fi

    # Source the base first so a user override can replace only the function it
    # cares about instead of having to duplicate unrelated runtime setup.
    . "$base_portals_file"
    log_startup INFO "Loaded managed portals file: $base_portals_file."

    if [ -r "$user_portals_file" ]; then
        # Load the override after the base so a user can replace only the
        # portal function instead of re-implementing the managed defaults.
        . "$user_portals_file"
        log_startup INFO "Loaded user portal override: $user_portals_file."
    else
        log_startup INFO "No user portal override found. Using managed portal defaults."
    fi

    if ! type morph_start_portals >/dev/null 2>&1; then
        log_startup ERROR "Portal setup did not define morph_start_portals."
        return 1
    fi
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

# Run morph and append stdout/stderr into the startup log.
# Expects LOG_DIR, MORPH_STARTUP_LOG_FILE, MORPH_BIN, CONFIG_FILE, LOG_FILE,
# CRASH_LOG_FILE, MORPH_LOG_LEVEL and MORPH_ENABLE_CRASH_HANDLER.
run_morph_with_capture() {
    # Avoid FIFO+tee here. The compositor starts helper processes such as
    # xwayland-satellite and portals; if any child inherits the FIFO write end,
    # tee never receives EOF and the display-manager session remains stuck after
    # Morph has already exited. A regular append-only log fd cannot block logout.
    morph_bin="${MORPH_BIN:?MORPH_BIN is not set}"
    level="${MORPH_LOG_LEVEL:-error}"
    enable_crash="${MORPH_ENABLE_CRASH_HANDLER:-0}"
    if [ "$enable_crash" = "1" ]; then
        if [ -n "${CONFIG_FILE:-}" ]; then
            "$morph_bin" -c "$CONFIG_FILE" --log-level "$level" --log-file "$LOG_FILE" --crash-log "$CRASH_LOG_FILE" >>"$MORPH_STARTUP_LOG_FILE" 2>&1
        else
            # Omit -c entirely only when the launcher intentionally reached the
            # no-config builtin fallback path. Any explicit or resolved config
            # file is still passed through -c above; nothing is ignored here.
            "$morph_bin" --log-level "$level" --log-file "$LOG_FILE" --crash-log "$CRASH_LOG_FILE" >>"$MORPH_STARTUP_LOG_FILE" 2>&1
        fi
    else
        if [ -n "${CONFIG_FILE:-}" ]; then
            "$morph_bin" -c "$CONFIG_FILE" --log-level "$level" --log-file "$LOG_FILE" --no-crash-handler >>"$MORPH_STARTUP_LOG_FILE" 2>&1
        else
            "$morph_bin" --log-level "$level" --log-file "$LOG_FILE" --no-crash-handler >>"$MORPH_STARTUP_LOG_FILE" 2>&1
        fi
    fi
    return $?
}

# Error summary helpers
# ==============================================================================

# Emit a compact error summary for the current run only.
# Expects LOG_DIR, LOG_FILE, CRASH_LOG_FILE, CORE_LOG_BASELINE, CRASH_LOG_BASELINE, MORPH_STARTUP_LOG_FILE.
dump_recent_error_summary() {
    pattern='warn|warning|error|failed|crash|segv|sig'
    log_startup INFO "Automatic error summary (current run only)"

    summary_tmp=$(mktemp "$LOG_DIR/morph-error-summary.XXXXXX") || {
        log_startup ERROR "Summary skipped: failed to create temporary summary file."
        return
    }

    # Scan core and crash logs from their per-run baselines only.
    for pair in \
        "$LOG_FILE:$CORE_LOG_BASELINE" \
        "$CRASH_LOG_FILE:$CRASH_LOG_BASELINE"; do
        f=${pair%%:*}
        baseline=${pair##*:}
        if [ ! -f "$f" ]; then
            log_startup INFO "Summary skipped: file not found: $f."
            continue
        fi

        start_line=$((baseline + 1))
        log_startup INFO "Summary source: $f (from line $start_line)."
        if command -v rg >/dev/null 2>&1; then
            matches=$(tail -n +"$start_line" "$f" 2>/dev/null | rg -n -i "$pattern" 2>/dev/null | tail -n 60)
        else
            matches=$(tail -n +"$start_line" "$f" 2>/dev/null | grep -n -i -E "$pattern" 2>/dev/null | tail -n 60)
        fi

        if [ -n "$matches" ]; then
            printf '%s\n' "$matches" >> "$summary_tmp"
        else
            log_startup INFO "Summary source had no matching lines: $f."
        fi
    done

    if [ -s "$summary_tmp" ]; then
        awk '!seen[$0]++' "$summary_tmp" | sed 's/^/[error-scan] /' | tee -a "$MORPH_STARTUP_LOG_FILE"
    else
        log_startup INFO "Summary had no matching lines in the current run."
    fi
    rm -f "$summary_tmp"
}
