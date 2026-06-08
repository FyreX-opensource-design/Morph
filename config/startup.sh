#!/bin/sh
# Main stackcomp startup hook.
# - Sources shared helpers from scripts/shell-helpers.sh.
# - Uses log_startup <level> <message> to write to
#   $STACKCOMP_STARTUP_LOG_FILE.
# - launch <command> starts a service, logs output, and registers it for shutdown.
# - launch_nokill <command> starts/logs without shutdown registration.
################################################################################

# Activate Helper functions for logging and launching services
# shellcheck disable=SC2034
CURRENT_LOG_FILE="${STACKCOMP_STARTUP_LOG_FILE:?STACKCOMP_STARTUP_LOG_FILE is not set}"
. "$COMP_ROOT_DIR/scripts/shell-helpers.sh"

# Nested Mode
# ==============================================================================
if [ "$WLR_BACKENDS" = "x11" ] || [ "$WLR_BACKENDS" = "wayland" ]; then
    log_startup INFO "Nested mode ($WLR_BACKENDS) detected. Starting test clients only."

    # Prefer an explicit socket from the launcher, otherwise keep existing
    # WAYLAND_DISPLAY, and only then fall back to a known nested default.
    if [ -n "$WLR_WL_SOCKET" ]; then
        export WAYLAND_DISPLAY="$WLR_WL_SOCKET"
    elif [ -z "$WAYLAND_DISPLAY" ]; then
        export WAYLAND_DISPLAY="wayland-nested"
    fi

    log_startup INFO "Starting test clients on $WAYLAND_DISPLAY."
    # ==========================================================================
    # Optional test clients:
    # launch alacritty
    # log_startup INFO "Started alacritty."
    # ==========================================================================
    
    # Exit so native-session services below are not started.
    exit 0
fi


# Native Mode
# ==============================================================================
log_startup INFO "Native Wayland mode detected. Starting autostart services for the main session."

# Portal Services
# ----------------------------
# Kill stale instances to avoid leftovers.
pkill -x "xdg-desktop-portal-wlr|xdg-desktop-portal-gtk|xdg-desktop-portal" 2>/dev/null
log_startup INFO "Killed stale xdg-desktop-portal instances."

# Start portal services via helper wrappers.
launch_nokill /usr/libexec/xdg-desktop-portal-wlr
launch_nokill /usr/libexec/xdg-desktop-portal-gtk

# Wait briefly before starting the main portal.
sleep 1
launch_nokill /usr/libexec/xdg-desktop-portal
log_startup INFO "Started xdg-desktop-portal instances."

# Optional: force GTK portal usage for desktop apps
export GTK_USE_PORTAL=1

# Qt: prefer Wayland, but allow xcb fallback for apps without Wayland plugin.
export QT_QPA_PLATFORM="wayland;xcb"

# GTK: prefer Wayland, but keep X11 fallback for apps/toolkits that still expect it.
export GDK_BACKEND="wayland,x11"

log_startup INFO "Set portal vars (QT_QPA_PLATFORM=wayland;xcb, GDK_BACKEND=wayland,x11, GTK_USE_PORTAL=1)."

# Additional autostart services
# ==============================================================================

# Start and redirect services
#launch_nokill lxqt-policykit-agent
#launch /usr/bin/xfce4-power-manager

# Set background color.
#launch swaybg -c '#80c3d8'

# Configure output mode/position/scale/transform.
# Use wlr-randr to get output names.
# Example ~/.config/kanshi/config:
#   profile {
#     output HDMI-A-1 position 1366,0
#     output eDP-1 position 0,0
#   }
#launch kanshi


# Session Components
# ==============================================================================

# Launch a panel such as yambar or waybar.
launch sfwbar
log_startup INFO "Started sfwbar."
launch waybar -c ./config/waybar-stackcomp.jsonc -s ./config/waybar-stackcomp.css
log_startup INFO "Started waybar."

# Enable notifications via org.freedesktop.Notifications (e.g. Thunderbird).
# A notification client such as mako/dunst is required.
launch dunst
log_startup INFO "Started dunst."

# Lock after 5 minutes; power off displays after 10 minutes.
# Restart kanshi when leaving powersave.
#
# Avoid disabling outputs directly (e.g. wlr-randr --off), as this can
# rearrange windows (since a837fef). Use wlr-output-power-management clients
# such as wlopm instead: https://git.sr.ht/~leon_plickat/wlopm

launch swayidle -w \
    timeout 300 'swaylock -f -c 000000' \
    timeout 600 'pkill kanshi; wlopm --off \*' \
    resume 'kanshi &wlopm --on \*' \
    before-sleep 'swaylock -f -c 000000'
log_startup INFO "Started swayidle."

# ==============================================================================
log_startup INFO "Startup hook completed."
