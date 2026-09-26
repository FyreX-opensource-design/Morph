#!/bin/sh
# Development morph startup hook.
# Keep this file focused on user-facing startup content:
# - Additional autostart services
# - Session components
# The managed runtime provides launch/log helpers before this hook runs.
################################################################################

# Helper commands available in this hook:
# - launch <cmd ...>        Start a managed session component and register it for shutdown cleanup.
# - launch_nested <cmd ...> Start a managed component only in nested sessions.
# - launch_nokill <cmd ...> Start a component without adding it to managed shutdown cleanup.
# - log_startup <lvl> <msg> Write a message into the standard startup log.

# Additional autostart services
# ==============================================================================

# Start and redirect services
#launch_nokill lxqt-policykit-agent
#launch /usr/bin/xfce4-power-manager

# Set background color.
launch swaybg -c '#80c3d8'
#launch_nested swaybg -c '#1e691b'


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

# Full example:
log_startup INFO "Starting user startup example components."
# launch sfwbar
# launch dunst
# launch swayidle -w \
#     timeout 300 'swaylock -f -c 000000' \
#     timeout 600 'wlopm --off \*' \
#     resume 'wlopm --on \*'

# Launch a panel such as yambar or waybar.
launch sfwbar -f testing/sfwbar_morph.config
log_startup INFO "Started panel sfwbar."
#launch waybar -c ./config/waybar-morph.jsonc -s ./config/waybar-morph.css
#log_startup INFO "Started waybar."

# Enable notifications via org.freedesktop.Notifications (e.g. Thunderbird).
# A notification client such as mako/dunst is required.
launch dunst
log_startup INFO "Started notification client dunst."

# start editor/viewer with config to see the bindings
#launch mousepad "${HOME}"/.config/morph/morph.conf
#log_startup INFO "Started editor with ~/.config/morph/morph.conf."

# Lock after 5 minutes; power off displays after 10 minutes.
# Restart kanshi when leaving powersave.
#
# Avoid disabling outputs directly (e.g. wlr-randr --off), as this can
# rearrange windows (since a837fef). Use wlr-output-power-management clients
# such as wlopm instead: https://git.sr.ht/~leon_plickat/wlopm

#launch swayidle -w \
#    timeout 300 'swaylock -f -c 000000' \
#    timeout 600 'pkill kanshi; wlopm --off \*' \
#    resume 'kanshi &wlopm --on \*' \
#    before-sleep 'swaylock -f -c 000000'
#log_startup INFO "Started swayidle with swaylock (5 min idle time)."

# ==============================================================================
log_startup INFO "Startup hook completed."
