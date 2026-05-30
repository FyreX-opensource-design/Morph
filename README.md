# stackcomp

A wlroots-based Wayland compositor with stack, tile, and scroll layouts.

## Current Features

- Three layouts: stack, tile grid, and horizontal scroll.
- 9 workspaces with switch/move actions and IPC commands.
- INI-style keybind configuration with optional shell `when` predicates.
- Tiling rules (`[tile_rule]`) and decoration rules (`[decoration_rule]`) by regex.
- Layer-shell workarea handling via exclusive zones.
- Optional Xwayland support for X11 clients.

## Current Protocol Support

- `zwlr_layer_shell_v1` (tested with Waybar/swaybg)
- `zwlr_screencopy_manager_v1` (tested with grim)
- `ext_workspace_manager_v1` (tested with Waybar ext/workspaces)
- `zwlr_foreign_toplevel_manager_v1`
- `zxdg_decoration_manager_v1`
- `zwp_pointer_constraints_v1`
- `zwp_relative_pointer_manager_v1`
- `zwp_tablet_manager_v2`

See `PROTOCOLS.md` for the exact matrix of implemented and missing globals.

## Build

Use Meson/Ninja:

```bash
meson setup build
meson compile -C build
```

Run:

```bash
./build/stackcomp
```

## Tests

Run automated tests locally with Meson:

```bash
meson setup build --reconfigure
meson test -C build --print-errorlogs
```

Current automated coverage includes config parser and rule validation checks.

## Repository Structure

- `src/`: compositor source code
- `config/`: helper scripts and sample runtime config
- `testing/`: local test assets
- `protocols/`: vendored XML protocol files used for code generation

## Runtime Notes

- wlroots 0.19.x API is targeted.
- Runtime dependencies include the normal wlroots graphics/input stack.
- `xorg-xwayland` is needed if you want X11 client support.
- Startup logging options: `--log-level silent|error|info|debug`, `--quiet`, `--verbose`, and `--log-file /path/to/stackcomp.log`.
- Crash handler options: `--crash-log /path/to/stackcomp-crash.log` and `--no-crash-handler`.
- Crash handling and post-mortem workflow are documented in `CRASHING.md`.
