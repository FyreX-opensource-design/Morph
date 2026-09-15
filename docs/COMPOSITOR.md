# Morph — compositor notes

Working document for the wlroots-based **stacking** compositor in this repo. Update this file as features land.

## Current scope (implemented)

- **Stacking / floating**: XDG toplevels are scene-graph siblings; focus raises a window (`wlr_scene_node_raise_to_top`) in stack mode.
- **Tiling mode**: `COMP_LAYOUT_TILE` lays out mapped XDG toplevels in a **roughly square grid** (≈ `ceil(sqrt(n))` columns) inside each physical output’s **`layer_workarea`** (layer-shell exclusive zones subtracted per head). Windows are assigned to an output by the **center** of their geometry in layout coordinates (fallback: primary / cursor output). Uses `wlr_xdg_toplevel_set_size` + scene node position.
- **Scroll mode**: `COMP_LAYOUT_SCROLL` is a niri-like horizontal strip **per output**: one tiled window per that output’s workarea width, with a **per-workspace, per-output** scroll slot; focus / IPC scroll steps apply to the focused window’s output (or the primary output when none focused).
- **Workspaces**: nine virtual desktops (`1`..`9`); new XDG toplevels attach to the current workspace. Only the active workspace’s toplevels are shown; tile/scroll arrange and scroll index are per workspace. Keybinds / IPC: `workspace`, `workspace_next`, `workspace_prev`, `workspace_move`; env [`MORPH_WORKSPACE`](ENVIRONMENT.md#morph-workspace) for `when=`. Panels can bind **`ext_workspace_manager_v1`** (**`ext-workspace-v1`**) to list workspaces and call **`activate`** (see **`docs/PROTOCOLS.md`**); **waybar** and other bars need a workspace module compiled for this protocol, not Sway IPC. Example Waybar (**`ext/workspaces`**): **`config/waybar-morph.jsonc`** + **`config/waybar-morph.css`** — run with **`waybar -c …/waybar-morph.jsonc -s …/waybar-morph.css`** on **`$WAYLAND_DISPLAY`** after morph starts (Waybar must include the ext-workspaces module; distro packages vary).
- **wlroots 0.20.2+**, scene graph (`wlr_scene_*`), **`xdg-shell`**, **`wlr-layer-shell` v4** (panels / overlays via `wlr_scene_layer_surface_v1`), **`zwlr_screencopy_manager_v1`** (via `wlr_screencopy_manager_v1_create` for tools like **grim**), core seat, pointer, keyboard, outputs via `wlr_output_layout`. **Wayland / portal protocol surface** is still small overall; see **`docs/PROTOCOLS.md`** for details and portal expectations.
- **Keybind config** (INI-style): repeated `[bind]` sections with `mods`, `key`, `action`, optional `command` (for `exec`), optional `when` (shell predicate, evaluated **on each key press**; exit 0 = bind active). Optional **`[hooks]`** section: **`startup`**, **`shutdown`**, **`reload`** shell strings (`sh -c`). See **`docs/CONFIG.md`** for full syntax and every action; **`morph.conf.example`** is a commented starter file. Modifiers and keysym are read **before** `wlr_keyboard_notify_key()` so XKB state matches user chord; `exec` / `when` / hooks use `posix_spawnp("sh", …)` instead of `fork` for safer interaction with wlroots’ threads.
  - **Actions** (summary): `quit`, `close`, `exec`, layout toggles, window focus cycling (`nextWindow` / `prevWindow`), tile **sort** moves (`tile_left` / `tile_right`, `tile_first` / `tile_last`, `tile_move`), tile **grid** moves (`tile_up` / `tile_down`, column top/bottom, `tile_grid_move` with `command=`). Details and `command=` rules are in **`docs/CONFIG.md`**. Built-in defaults include **Super+t** for `layout_toggle` when no config file is found, the file cannot be read, or it defines no binds.
  - **Search path:** optional `-c` / `--config` (overrides [`$MORPH_CONFIG`](ENVIRONMENT.md#morph-config)); otherwise [`$MORPH_CONFIG`](ENVIRONMENT.md#morph-config) if set; otherwise the first readable file among `$XDG_CONFIG_HOME/morph/config` and `~/.config/morph/config`. If no file is found or the file defines no binds, built-in defaults apply (`exec` uses `${TERMINAL:-foot}` via `sh -c`).
- **`[tile_rule]`** (optional): tiling placement for XDG toplevels. **`app_id=`** and/or **`title=`** hold **POSIX extended regex** (see `regcomp(3)`); if both are set, **both** must match. **`mode=`** `tile` (default) or `float` (window is not placed in the grid; centered on map, raised on focus). **`order=`** integer: **lower** values sort **earlier** in the tile grid (stable tie-break: map order). Rules are tried **in file order**; the first match applies. Rules are re-evaluated on **`set_title` / `set_app_id`**, and all views are refreshed when switching to tile layout.
- **Pointer**: `Super+drag` moves a window (unchanged; not yet in config file). Optional **`[focus]`** policy (`ClickToFocus` default, `FocusFollowsMouse`, `SloppyFocus`) — see **`docs/CONFIG.md`**.
- **Window focus cycling**: `server->focus_order` is an **MRU** list over the same toplevels as `server->toplevels`, newest first. Windows join it at the **tail** on creation, leave it on destroy, and are promoted to the **head** by every successful `focus_toplevel()` handoff (click, map, workspace switch, foreign-toplevel and xdg-activation requests). Eligibility is evaluated **live** at cycle time (`toplevel_focus_candidate`: mapped, initialized, non-minimized, on the current workspace), so minimize/restore and workspace moves need no extra bookkeeping. Workspace switching also picks its focus target from this list, so returning to a workspace restores the window last used there. Keybinds: `nextWindow` / `prevWindow`; IPC: `window focus next|prev`; CLI: `--window-focus next|prev`. No switcher overlay yet — focus commits immediately on each press.
- **CLI:** Morph exposes startup flags, IPC-aware runtime control flags, logging options, and crash-handler options. The full reference now lives in [`docs/CLI.md`](CLI.md). Keep this file focused on compositor behavior rather than option-by-option CLI detail.
- **Crash handling:** morph installs a minimal async-signal-safe fatal handler by default. Use **`--crash-log PATH`** to append short crash markers to a file, or **`--no-crash-handler`** to disable installation. See **`docs/CRASHING.md`** for full core-dump workflow.
- **IPC commands** (text, newline optional): `layout toggle`, `layout tile`, `layout stack`, `layout scroll`; **`focus ClickToFocus|FocusFollowsMouse|SloppyFocus`**; **`window focus next|prev`** (also accepted as `window next|prev`); **`tile move prev|next|first|last`** or **`tile move <N>`** (sort-order steps); **`scroll prev|next|left|right|<N>`** or the same with an optional **`move`** token (e.g. **`scroll move next`**); **`tile grid`** with the same forms as **`tile_grid_move`** `command=` (e.g. `tile grid left`, `tile grid up 2`, legacy `tile grid -1`); **`reload config`** or **`reload`** to reload the config file and run the new **`[hooks]` `reload=`** script. Example: `echo 'layout tile' | nc -U "$XDG_RUNTIME_DIR/morph-ipc.sock"`.

## Architecture (intended)

| Layer | Role |
|--------|------|
| **Backend** | `wlr_backend_autocreate` (DRM + libinput, or nested X11/Wayland). |
| **Scene** | Root scene → layer trees (background, windows, overlays, cursor policy). Stacking = z-order among window nodes. |
| **Layout** | `wlr_output_layout` + `wlr_scene_attach_output_layout`; optional tiling controller mutates window geometry instead of free drag. |
| **Input** | `wlr_cursor` + `wlr_seat`; keybindings handled in compositor before `wlr_seat_keyboard_notify_key` to clients. |
| **Config** | Eventually loaded at startup + optional reload (see below). |

## Roadmap and Follow-up

Longer-term compositor follow-up items now live in [`docs/BACKLOG.md`](BACKLOG.md).

This keeps this file focused on implemented scope and current architecture while the backlog tracks future design and feature work.

## Build & run

Run `meson setup build && meson compile -C build`. Install with `meson install -C build` or your distro package manager. **`morph`** and **`morph-session`** go to the prefix **`bindir`**, the managed runtime files land under **`sysconfdir/morph`**, and **`sessions/morph.desktop`** is installed as **`share/wayland-sessions/morph.desktop`** so display managers such as greetd and SDDM can start the session through the runtime wrapper.

```bash
meson setup build && meson compile -C build
./build/morph
./build/morph --layout tile
./build/morph --scroll
```

Nested (Wayland): `WAYLAND_DISPLAY=wayland-1 ./build/morph`

Automated tests:

```bash
meson setup build --reconfigure
meson test -C build --print-errorlogs
```

CI workflow lives at `.github/workflows/ci.yml` and runs the same configure/build/test sequence.

**Build deps:** wlroots 0.20.2+, `wayland-server`, `wayland-protocols` (for `xdg-shell` codegen), `xkbcommon`, plus wlroots’ normal graphics stack (DRM, libinput, etc.) for a DRM session.

Runtime is the same as other wlroots compositors: prefer a **TTY login** or a **nested** Wayland/X11 session for first tests.
