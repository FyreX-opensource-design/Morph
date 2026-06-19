# Stackcomp (WIP Name)
A stacking, tiling, and scrolling hybrid compositor built from stratch with WL-Roots

## Current Features

- Three layouts: stack, tile grid, and horizontal scroll.
- 9 workspaces with switch/move actions and IPC commands.
- INI-style keybind configuration with optional shell `when` predicates.
- Tiling rules (`[tile_rule]`) and decoration rules (`[decoration_rule]`) by regex.
- Layer-shell workarea handling via exclusive zones.
- Optional Xwayland support for X11 clients.
- Environment-driven launcher/runtime settings, including XKB keyboard defaults.

# Current repo structure
* \[config]: defult config for the compositor
* \[testing]: testing files for the compositor

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

# Current repo structure
* \[config]: defult config for the compositor
* \[testing]: testing files for the compositor

# Dependencies
- `xwayland-satellite` — X11 apps (ATLauncher, etc.) appear as normal XDG windows; started automatically by stackcomp
- `xorg-xwayland` — pulled in by xwayland-satellite and is needed for X11 client support.

Debian/Ubuntu quick check:

```bash
apt-cache policy xwayland-satellite
apt-cache search xwayland-satellite
```

If no install candidate is available in your configured repositories, build
`xwayland-satellite` from source and ensure the resulting binary is in your
`PATH` before launching stackcomp.

Minimal runtime check:

```bash
command -v xwayland-satellite
```

Set `STACKCOMP_X11=0` to disable satellite. Display is auto-picked (`:2`..`:99`, first free socket); override with `STACKCOMP_X11_DISPLAY=:12`.

Launcher default is session-aware:

- nested (`WLR_BACKENDS=x11|wayland`): satellite defaults to disabled
- native (`WLR_BACKENDS=drm,libinput`): satellite defaults to enabled

Java/X11 apps (e.g. ATLauncher) often need:
`_JAVA_AWT_WM_NONREPARENTING=1 atlauncher`

## IPC (Current State)

stackcomp currently has two IPC/control surfaces that are intentionally different:

1. Local command IPC (text over Unix socket)
2. Wayland protocol IPC (ext-workspace + foreign-toplevel)

### 1) Local command IPC (socket)

- Socket path: `$XDG_RUNTIME_DIR/stackcomp-ipc.sock`
- Transport: `AF_UNIX` stream socket
- Payload: one text command line (newline optional)
- Scope: local scripts and CLI automation

Supported command families:

- `layout toggle|stack|tile|scroll`
- `workspace N|next|prev|move N`
- `tile move prev|next|first|last|<signed-int>`
- `tile grid left|right|up|down|top|bottom|...`
- `scroll prev|next|left|right|<signed-int>` (or `scroll move ...`)
- `reload config` (alias: `reload`)

Example:

```bash
echo 'workspace 2' | nc -U "$XDG_RUNTIME_DIR/stackcomp-ipc.sock"
echo 'layout scroll' | nc -U "$XDG_RUNTIME_DIR/stackcomp-ipc.sock"
```

### 2) Wayland protocol IPC (client-facing)

- `ext_workspace_manager_v1` (workspace listing + activate)
- `zwlr_foreign_toplevel_manager_v1` (window metadata + activate/close/state requests)

This is what tools like waybar or wayctl use through Wayland protocol objects and events, not through the local text socket.

Important current limitation in stackcomp `ext_workspace_manager_v1`:

- `activate`: implemented
- `create_workspace`: currently no-op
- `remove_workspace`: currently no-op
- `assign_workspace`: currently no-op

For probably planned IPC stabilization, see
`feature-request_IPC_stabilisation.md`.

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

Release-style launcher run (recommended for daily use):

```bash
./testing/stackcomp_run
```

The launcher `testing/stackcomp_run` is a convenient way to initialize the compositor with sane defaults.
It supports, among others, these runtime options:

- `STACKCOMP_DBG=0|1|2`
- `STACKCOMP_CFG=/path/to/config`
- `STACKCOMP_X11=0|1`
- `STACKCOMP_X11_DISPLAY=:12`

Examples:

```bash
STACKCOMP_DBG=0 ./testing/stackcomp_run
STACKCOMP_CFG=/etc/stackcomp/stackcomp.conf STACKCOMP_DBG=1 ./testing/stackcomp_run
```

For full launcher behavior and all options, see:

- `testing/LAUNCHER.md`
- `config/ENVIRONMENT.md` (environment variables, including XKB settings)

## Tests

Run automated tests locally with Meson:

```bash
meson setup build --reconfigure
meson test -C build --print-errorlogs
```

Current automated coverage includes config parser and rule validation checks.

## Build/Test Workflow (Local)

- Local build/test command: `./scripts/local-build-test.sh build`

Detailed local workflow and troubleshooting notes are documented in
`TESTING.md`.

## Repository Structure

- `src/`: compositor source code
- `config/`: helper scripts and sample runtime config
- `testing/`: local test assets
- `protocols/`: vendored XML protocol files used for code generation
- `DEVFAQ.md`: developer troubleshooting FAQ (logs, runtime behavior, common pitfalls)

## Runtime Notes

- wlroots 0.19.x API is targeted.
- Runtime dependencies include the normal wlroots graphics/input stack.
- Startup logging options: `--log-level silent|error|info|debug`, `--quiet`, `--verbose`, and `--log-file /path/to/stackcomp.log`.
- Crash handler options: `--crash-log /path/to/stackcomp-crash.log` and `--no-crash-handler`.
- Crash handling and post-mortem workflow are documented in `CRASHING.md`.
