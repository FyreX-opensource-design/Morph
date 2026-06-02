# Stackcomp (WIP Name)

A stacking, tiling, and scrolling hybrid compositor built from stratch with WL-Roots.

## Current Features

- Three layouts: stack, tile grid, and horizontal scroll.
- 9 workspaces with switch/move actions and IPC commands.
- INI-style keybind configuration with optional shell `when` predicates.
- Tiling rules (`[tile_rule]`) and decoration rules (`[decoration_rule]`) by regex.
- Layer-shell workarea handling via exclusive zones.
- Optional Xwayland support for X11 clients.

## Current Protocol Support

- `zwlr_layer_shell_v1` (tested with Waybar & swaybg)
- `zwlr_screencopy_manager_v1` (tested with grim)
- `ext_workspace_manager_v1` (tested with Waybar ext/workspaces)
- `zwlr_foreign_toplevel_manager_v1`
- `zxdg_decoration_manager_v1`
- `zwp_pointer_constraints_v1`
- `zwp_relative_pointer_manager_v1`
- `zwp_tablet_manager_v2`

See `PROTOCOLS.md` for the exact matrix of implemented and missing globals.

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

## Tests

Run automated tests locally with Meson:

```bash
meson setup build --reconfigure
meson test -C build --print-errorlogs
```

Current automated coverage includes config parser and rule validation checks.

## CI Workflows (Remote + Local)

- GitHub remote workflow: `.github/workflows/ci.yml`
- Local equivalent command: `./scripts/ci-local.sh build`

Detailed setup, local execution via `act`, and troubleshooting are documented in
`WORKFLOWS.md`.

If you are new to GitHub Actions runner setup, start with
`.github/README-ci.md`.

## Repository Structure

- `src/`: compositor source code
- `config/`: helper scripts and sample runtime config
- `testing/`: local test assets
- `protocols/`: vendored XML protocol files used for code generation
- `DEVFAQ.md`: developer troubleshooting FAQ (logs, runtime behavior, common pitfalls)

## Runtime Notes

- wlroots 0.19.x API is targeted.
- Runtime dependencies include the normal wlroots graphics/input stack.
- `xorg-xwayland` is needed if you want X11 client support.
- Startup logging options: `--log-level silent|error|info|debug`, `--quiet`, `--verbose`, and `--log-file /path/to/stackcomp.log`.
- Crash handler options: `--crash-log /path/to/stackcomp-crash.log` and `--no-crash-handler`.
- Crash handling and post-mortem workflow are documented in `CRASHING.md`.
