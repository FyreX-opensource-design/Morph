# Morph

**Morph** is a stacking, tiling, and scrolling hybrid compositor built with wlroots.

## Current Features

- Three layouts: stack, tile grid, and horizontal scroll
- 9 workspaces with switch/move actions and IPC commands
- INI-style keybind configuration with optional shell `when` predicates
- Tiling rules ([tile_rule]) and decoration rules ([decoration_rule]) by regex.
- Layer-shell workarea handling via exclusive zones
- Optional Xwayland support for X11 clients
- Environment-driven launcher/runtime settings, including XKB keyboard defaults

## Current Protocol Support

- `zwlr_layer_shell_v1` (tested with Waybar/swaybg)
- `zwlr_screencopy_manager_v1` (tested with grim)
- `ext_image_copy_capture_manager_v1` + output/window capture sources (wlroots 0.20+)
- `ext_workspace_manager_v1` (tested with Waybar ext/workspaces)
- `zwlr_foreign_toplevel_manager_v1` / `ext_foreign_toplevel_list_v1`
- `zxdg_decoration_manager_v1`
- `zwp_pointer_constraints_v1`
- `zwp_relative_pointer_manager_v1`
- `zwp_tablet_manager_v2`

See [`docs/PROTOCOLS.md`](docs/PROTOCOLS.md) for the exact matrix of implemented and missing globals.

## Current Repository Structure

| Path | Purpose |
|---|---|
| [`assets/`](assets/) | Runtime assets like icons and other visual resources |
| [`config/`](config/) | Runtime defaults, user hook templates, portal setup, and sample [`morph.conf`](config/morph.conf) |
| [`docs/`](docs/) | Current project documentation |
| [`protocols/`](protocols/) | Wayland protocol XML files used for generated protocol code |
| [`scripts/`](scripts/) | Session wrappers, install helpers, and managed lifecycle scripts |
| [`sessions/`](sessions/) | Desktop entries for runtime and development sessions |
| [`src/`](src/) | Morph compositor source code |
| [`testing/`](testing/) | Manual runbooks, launcher assets, and dev-only session files under `testing/config/` |
| [`tests/`](tests/) | Automated tests for config and shell/runtime behavior |

## What to Read First

- [`docs/OVERVIEW.md`](docs/OVERVIEW.md) for the documentation map, repository structure, and managed session flows
- [`docs/CONFIG.md`](docs/CONFIG.md) for compositor config syntax and hook definitions
- [`docs/LAUNCHER.md`](docs/LAUNCHER.md) for wrapper behavior, managed hooks, and runtime files
- [`docs/CLI.md`](docs/CLI.md) for binary command-line flags and IPC-aware runtime control
- [`docs/ENVIRONMENT.md`](docs/ENVIRONMENT.md) for runtime environment variables and XKB-related settings
- [`INSTALL.md`](INSTALL.md) for dependencies, runtime/dev install paths, and uninstall flows
- [`docs/TESTING.md`](docs/TESTING.md) for build, test, smoke-check, and manual validation runbooks

## Build and Run

```bash
meson setup build
meson compile -C build
./build/morph
```

Or use the helper script in `scripts/morph-build.sh`. See [`INSTALL.md`](INSTALL.md) for more infos.

## Tests

Run automated tests with:

```bash
meson test -C build --print-errorlogs
```

For the broader local workflow, including smoke checks and manual validation entry points, see [`docs/TESTING.md`](docs/TESTING.md).

## Installation

Morph can be installed in two variants: 

- Release/Runtime for normal use
- Debug/Development for debugging, toubleshooting and development

See [`INSTALL.md`](INSTALL.md) for:

- build dependencies
- runtime install and uninstall
- development install and uninstall
- the current install target layout

## Runtime Notes

- wlroots `0.20.2+` API is targeted
- X11 support depends on `xwayland-satellite` plus `xorg-xwayland`
- Disable X11 satellite with [`MORPH_X11=0`](docs/ENVIRONMENT.md#morph-x11)
- Java/X11 apps may need [`_JAVA_AWT_WM_NONREPARENTING=1`](docs/ENVIRONMENT.md#java-awt-wm-nonreparenting)
