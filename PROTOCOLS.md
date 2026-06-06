# Wayland protocols and desktop integration (stackcomp)

This document lists what **stackcomp** actually advertises and handles today, what **wlroots** adds implicitly when you call its helpers, and what is **not** implemented. It is derived from `src/main.c`, `meson.build`, and wlroots 0.19 behavior.

**Important distinction:** **xdg-desktop-portal** (settings, file chooser, screen cast, etc.) talks to implementations over **D-Bus** and, for some features, expects the **Wayland compositor** to expose specific **Wayland protocol** extensions (often **wlroots**-specific unstable ones). Stackcomp exposes a **moderate** set of globals suitable for tiling, panels, capture, and games; several desktop/portal features still need more protocols or session services.

---

## Explicitly created in `server_init` (your code)

These `wlr_*_create` calls register the corresponding **Wayland globals** (names clients see):

| Global (typical client name) | wlroots API | Notes |
|------------------------------|-------------|--------|
| **`wl_compositor`** | `wlr_compositor_create(dpy, 6, …)` | Version **6**. Surfaces and buffer attachment. |
| **`wl_subcompositor`** | `wlr_subcompositor_create` | Subsurfaces. |
| **`wl_data_device_manager`** | `wlr_data_device_manager_create` | Clipboard and drag-and-drop plumbing; selection is wired from the seat’s `request_set_selection`. |
| **`wl_output`** | Via **backend** / output layout | Physical outputs; layout uses `wlr_output_layout` + `wlr_scene_attach_output_layout`. |
| **`zxdg_output_manager_v1`** (xdg-output-unstable) | `wlr_xdg_output_manager_v1_create(dpy, output_layout)` | Logical output geometry for clients (e.g. **waybar**). |
| **`zwlr_screencopy_manager_v1`** (wlr-screencopy-unstable) | `wlr_screencopy_manager_v1_create(dpy)` | Screen capture (**grim**, some recorders). Uses **`wlr_scene_output`** commit path. |
| **`xdg_wm_base`** (XDG shell) | `wlr_xdg_shell_create(dpy, 3)` | Version **3**. Toplevels; **xdg popups** (menus, tooltips) are added to the scene graph and unconstrained in `main.c`. |
| **`ext_workspace_manager_v1`** (staging **ext-workspace-v1**) | `wl_global_create` + `src/ext_workspace.c` | Nine fixed workspaces; **`activate`** switches desktop; **`state`** + **`done`** for bars (e.g. **waybar** `ext/workspaces`). No create/remove/assign. |
| **`zxdg_decoration_manager_v1`** (xdg-decoration-unstable-v1) | `wlr_xdg_decoration_manager_v1_create(dpy)` | Client vs server-side decorations; stackcomp picks mode per layout / tile-float / config. |
| **`zwlr_layer_shell_v1`** (wlr-layer-shell unstable) | `wlr_layer_shell_v1_create(dpy, 4)` | Panels, wallpapers, overlays via **`wlr_scene_layer_surface_v1`**. Exclusive zones → per-output **`layer_workarea`**. Layer popups handled like toplevel popups. |
| **`zwlr_foreign_toplevel_manager_v1`** | `wlr_foreign_toplevel_manager_v1_create(dpy)` | Window list state for bars / switchers that use foreign-toplevel. |
| **`zwp_pointer_constraints_v1`** | `wlr_pointer_constraints_v1_create(dpy)` | Lock/confine pointer for games and confined UIs; activated on pointer focus and new constraints. |
| **`zwp_relative_pointer_manager_v1`** | `wlr_relative_pointer_manager_v1_create(dpy)` | Relative motion while constrained (paired with pointer constraints). |
| **`zwp_tablet_manager_v2`** | `wlr_tablet_v2_create(dpy)` | Tablet/stylus input. |
| **`wl_seat`** | `wlr_seat_create(dpy, "seat0")` | Pointer, keyboard, touch (when devices present). |
| **`wl_shm`** / **`linux_dmabuf`** (and related buffer support) | `wlr_renderer_init_wl_display(renderer, dpy)` | Buffer formats for clients (exact globals depend on renderer/backend). |
| **`wp_viewporter`** | `wlr_viewporter_create(dpy)` | Required for **xwayland-satellite** (X11 → XDG bridge). |
| **X11 (via satellite)** | `xwayland-satellite` child process | Not in-process Xwayland; stackcomp spawns satellite after startup and sets `DISPLAY`. |

**Not created anywhere in this repo:** primary selection, output management, export-dmabuf, gamma control, idle/keyboard-shortcuts inhibit, text-input/input-method, xdg-activation, fractional-scale (explicit global), cursor-shape (wp), presentation-time, security-context, virtual keyboard/pointer, etc.

---

## Seat capabilities (`WL_SEAT_CAPABILITY_*`)

`server_update_seat_capabilities` sets **pointer**, **keyboard**, and **touch** when a touch device is attached. Tablets use **tablet-v2** globals, not the legacy tablet seat capability.

---

## Input events forwarded to clients

From the seat and cursor wiring in `main.c`:

- **Keyboard:** keymap, enter, key, modifiers (compositor keybinds run before `wlr_seat_keyboard_notify_key`).
- **Pointer:** enter, motion, button, axis, frame; client cursor surface via `request_set_cursor`.
- **Relative pointer:** `wlr_relative_pointer_manager_v1_send_relative_motion` on hardware pointer motion (for constrained clients).
- **Pointer constraints:** lock (no on-screen cursor motion; relative events still flow) and confine (motion clipped to region); focus and `new_constraint` drive activation.
- **Touch:** down/up/motion/cancel/frame when touch devices are present (with pointer emulation fallback).
- **Tablet v2:** proximity, motion, tip, buttons.
- **Clipboard:** `request_set_selection` → `wlr_seat_set_selection` (regular clipboard).
- **Primary selection:** **not** wired (no `wlr_primary_selection_v1` / middle-click paste).

---

## `wayland-protocols` in this project

`meson.build` runs **wayland-scanner** on **stable `xdg-shell`**, **tablet-v2**, and **ext-workspace-v1** (plus vendored **wlr-layer-shell** XML). Other unstable protocols come from **wlroots** internal generated code (pointer constraints, relative pointer, screencopy, foreign toplevel, xdg-decoration, layer-shell).

---

## xdg-desktop-portal and session integration

Tools like **xdg-desktop-portal-wlr** expect a mix of **D-Bus** and compositor Wayland globals:

| Need (examples) | Typical Wayland / wlroots side | In stackcomp? |
|-----------------|----------------------------------|---------------|
| Screen / window capture (grim-style) | `zwlr_screencopy_unstable_v1` | **Yes** |
| PipeWire portal capture (some paths) | screencopy + **export-dmabuf** | **Partial** (no export-dmabuf) |
| Inhibit shortcuts / idle | `zwp_keyboard_shortcuts_inhibit_v1`, idle inhibit | **No** |
| Output layout for clients | `xdg_wm_base`, `zxdg_output_manager_v1` | **Yes** |
| Settings / dark mode (portal) | D-Bus + optional compositor hooks | **No** compositor hook |
| File open/save (portal) | Mostly D-Bus + GTK/Qt | Often works without extra globals |
| Games / confined pointer | pointer constraints + relative pointer | **Yes** |

A working portal still needs **`xdg-desktop-portal`** + a backend (e.g. **xdg-desktop-portal-wlr**) in the session, even when the compositor exposes screencopy.

---

## Summary tables

### Implemented (directly or via wlroots)

- **Core:** compositor, subcompositor, SHM/dmabuf (via renderer), outputs, seat (pointer/keyboard/touch), data device manager.
- **Shell:** XDG shell (toplevels + popups), xdg-output, wlr-layer-shell, foreign-toplevel, ext-workspace.
- **Input extras:** pointer constraints, relative pointer, tablet-v2; X11 via xwayland-satellite.
- **Decoration / capture:** xdg-decoration, screencopy.
- **Rendering:** scene graph, output layout, frame loop.

### Not implemented (common gaps, by impact)

**High**

- **Text input / input method** — fcitx/ibus, CJK.
- **Keyboard shortcuts inhibit** — fullscreen games/browsers.
- **Primary selection** — middle-click paste.

**Medium**

- **xdg-activation** — notification / launcher focus routing.
- **Idle inhibit** — prevent dim during video.
- **Fractional scale** — HiDPI blur on some clients.
- **export-dmabuf** — some portal/OBS capture paths.

**Lower**

- **Output management** — wdisplays/kanshi-style monitor GUI.
- **Gamma control** — compositor night light.
- **wp_cursor_shape** — named cursors (many apps use `set_cursor` surfaces instead).
- **presentation-time**, **security-context**, etc.

### Suggested order for further work

1. **Primary selection** — middle-click paste.
2. **Text input / input method** — IME users.
3. **Keyboard shortcuts inhibit** — fullscreen apps.
4. **xdg-activation** — focus from notifications.
5. **Fractional scale** — HiDPI clarity.
6. **Idle inhibit** — presentations / video.

Each addition needs new globals (often `wayland-protocols` or wlroots unstable headers) and event wiring; portal features also need the **portal service** in the session.

---

The **`wlr-layer-shell-unstable-v1.xml`** file under **`protocols/`** is a vendored copy (for `wayland-scanner`); it tracks upstream **wlr-protocols**.

## See also

- **`COMPOSITOR.md`** — feature scope, workspaces, build/run.
- **`CONFIG.md`** — keybinds, `when=`, IPC.
- **wlroots** [documentation](https://gitlab.freedesktop.org/wlroots/wlroots) for protocol modules in 0.19.
