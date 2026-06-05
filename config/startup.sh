#!/bin/sh
# This is the main startup script for stackcomp. It sets up the environment,
# starts the compositor, and handles logging. The shutdown script is called
# automatically when the compositor exits, to clean up any remaining processes.
#
# Additional helper functions for logging and launching services are defined in
# scripts/shell-helpers.sh and sourced here. This allows both startup.sh and
# shutdown.sh to use the same logging functions and maintain consistent log formatting.
#
# log_startup <level> <message> is used for logging, and the messages will
# be written to the startup log file defined in $STACKCOMP_STARTUP_LOG_FILE.
#
# launch <command> is used to start a program and log its output, and it relies
# on the log_startup function to write logs.
#
# launch_nokill <command> is similar to launch but does not register the program
# for automatic shutdown. This can be used for services that should persist beyond
# the compositor's lifecycle, such as system-wide daemons or background services.
################################################################################

# Activate Helper functions for logging and launching services
CURRENT_LOG_FILE="${STACKCOMP_STARTUP_LOG_FILE:?STACKCOMP_STARTUP_LOG_FILE is not set}"
source "$HOME/scratch-compositor/scripts/shell-helpers.sh"

# Nested Mode
# ==============================================================================
if [ "$WLR_BACKENDS" = "x11" ] || [ "$WLR_BACKENDS" = "wayland" ]; then
    log_startup INFO "Nested mode ($WLR_BACKENDS) detected. Starting test clients only."
    
    # Since WLR_WL_SOCKET is defined in stackcomp_run, the display is named this way:
    export WAYLAND_DISPLAY="wayland-nested"
    
    log_startup INFO "Starting test clients on $WAYLAND_DISPLAY."
    # ==========================================================================
    # Specific test clients can be started here, for example:
    # launch alacritty
    # log_startup INFO "Started alacritty."
    # ==========================================================================
    
    # Exit here so the portals/panels/services below are not started.
    exit 0
fi


# Native Mode
# ==============================================================================
log_startup INFO "Native Wayland mode detected. Starting autostart services for the main session."

# Portal Services
# ----------------
# Kill old instances to avoid leftovers
pkill -x "xdg-desktop-portal-wlr|xdg-desktop-portal-gtk|xdg-desktop-portal" 2>/dev/null
log_startup INFO "Killed stale xdg-desktop-portal instances."
# Start portal services via the launch helper
launch /usr/libexec/xdg-desktop-portal-wlr
launch /usr/libexec/xdg-desktop-portal-gtk
# IMPORTANT: wait briefly before starting the main portal
sleep 1
launch /usr/libexec/xdg-desktop-portal
log_startup INFO "Started xdg-desktop-portal instances."

# Optional: force GTK portal usage for Qt apps
export GTK_USE_PORTAL=1
log_startup INFO "Set GTK_USE_PORTAL=1 for Qt apps."

# Additional autostart services
# ==============================================================================

# Start and redirect services
#launch_nokill lxqt-policykit-agent
#launch_nokill /usr/bin/xfce4-power-manager

# Set background color.
#launch swaybg -c '#80c3d8' >/dev/null 2>&1 &

# Configure output directives such as mode, position, scale and transform.
# Use wlr-randr to get your output names
# Example ~/.config/kanshi/config below:
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

# Enable notifications. Typically GNOME/KDE application notifications go
# through the org.freedesktop.Notifications D-Bus API and require a client such
# as mako to function correctly. Thunderbird is an example of this.
launch dunst
log_startup INFO "Started dunst."

# Lock screen after 5 minutes; turn off display after another 5 minutes.
# kill and restart kanshi when entering powersave.
#
# Note that in the context of idle system power management, it is *NOT* a good
# idea to turn off displays by 'disabling outputs' for example by
# `wlr-randr --output <whatever> --off` because this re-arranges windows
# (since a837fef). Instead use a wlr-output-power-management client such as
# https://git.sr.ht/~leon_plickat/wlopm

launch swayidle -w \
    timeout 300 'swaylock -f -c 000000' \
    timeout 600 'pkill kanshi; wlopm --off \*' \
    resume 'kanshi &wlopm --on \*' \
    before-sleep 'swaylock -f -c 000000' &
log_startup INFO "Started swayidle."

# ==============================================================================
log_startup INFO "Startup hook completed."
