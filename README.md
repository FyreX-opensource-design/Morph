# Stackcomp (WIP Name)
A stacking, tiling, and scrolling hybrid compositor built from stratch with WL-Roots

# Current feature set
* stacking, tiling, scrooling mode for application windows
* keybinds with conditionals for each
* tiling and floating window rules found via regex for the application window

# Current protocal support
* wlr_layershell (tested with swaybg & waybar)
* wlr_screencopy (tested with grim)
* ext_workspaces (tested with waybar)
* zwlr_foreign_toplevel_manager_v1 (tested with waybar)

# Current repo structure
* \[config]: defult config for the compositor
* \[testing]: testing files for the compositor

# Deps
- `xwayland-satellite` — X11 apps (ATLauncher, etc.) appear as normal XDG windows; started automatically by stackcomp
- `xorg-xwayland` — pulled in by xwayland-satellite

Set `STACKCOMP_X11=0` to disable satellite. Display is auto-picked (`:2`..`:99`, first free socket); override with `STACKCOMP_X11_DISPLAY=:12`.

Java/X11 apps (e.g. ATLauncher) often need:
`_JAVA_AWT_WM_NONREPARENTING=1 atlauncher`
