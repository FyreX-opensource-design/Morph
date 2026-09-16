# Morph configuration

This file documents the **INI-style** configuration read at compositor startup. The format is line-oriented: sections in square brackets, `key = value` pairs, `#` comments, and blank lines are ignored.

## Where the file is loaded from

1. **`morph -c PATH` / `morph --config PATH`** — highest priority when given.
2. **[`$MORPH_CONFIG`](ENVIRONMENT.md#morph-config)** — if set to a readable path.
3. **Default search** (first file that exists and is readable):
   - `$XDG_CONFIG_HOME/morph/morph.conf`
   - `~/.config/morph/morph.conf`
   - `/etc/morph/morph.conf`

If no file is found, startup stops with a log message instead of silently continuing without a config. You can opt into the built-in fallback behavior with **`--allow-builtin-fallback`** or **[`MORPH_ALLOW_BUILTIN_FALLBACK=1`](ENVIRONMENT.md#morph-allow-builtin-fallback)**. The fallback provides only four emergency bindings: terminal (`Super+Return`), close (`Super+Shift+Q`), quit (`Super+Escape`), and layout toggle (`Super+T`). It does not synthesize hooks, application rules, portal setup, input mappings, workspace bindings, or startup applications. If the file exists but defines **no** `[bind]` entries, the same built-in binds are still generated as a compatibility fallback.

A starting point for your own file is **[`morph.conf.example`](morph.conf.example)** in this repository.

---

## Launcher environment ([morph-session_dbg](../testing/morph-session_dbg))

Further launcher details are documented in:

- [`docs/LAUNCHER.md`](LAUNCHER.md)

This keeps `docs/CONFIG.md` focused on INI syntax and behavior.

---

## Section `[bind]`

Each `[bind]` block describes **one** shortcut. Start a new `[bind]` section for each binding.

| Key | Required | Meaning |
|-----|----------|---------|
| **`mods`** | Yes | Modifier mask. Tokens separated by `+`, comma, or space (case-insensitive). Recognized tokens: **`shift`**, **`ctrl`** / **`control`**, **`alt`** / **`meta`**, **`super`** / **`mod`** / **`logo`** / **`win`** / **`mod4`**. The key name **`mod`** is also accepted as an alias for **`mods`**. |
| **`key`** | Yes | Keysym name passed to **xkb_keysym_from_name** (case-insensitive), for example `Return`, `Escape`, `Q`, `Left`, `F1`. |
| **`action`** | Yes | What to do when the chord matches (see **Actions** below). |
| **`command`** | For some actions | Shell command line for **`exec`**, or parameter for **`tile_move`** / **`tile_grid_move`** as documented below. |
| **`when`** | No | If set, **`/bin/sh -c '…'`** is run **on every key press** before the bind is considered; **exit status 0** means the bind is active. Non-zero skips the bind. The shell sees **[`MORPH_LAYOUT`](ENVIRONMENT.md#morph-layout)** as `stack`, `tile`, or `scroll`, and **[`MORPH_WORKSPACE`](ENVIRONMENT.md#morph-workspace)** as the current workspace number **`1`**..**`9`** (decimal string). |

Bindings are matched using modifier and keysym sampled **before** the compositor updates XKB state from the key event, so the chord matches what the user pressed.

If a `[bind]` block has a typo or other parse/validation error (unknown key, bad keysym/action, incomplete fields, invalid `command=`), Morph **skips that bind**, logs the problem, and continues loading the rest of the config so startup is not bricked by one bad shortcut.

### Layout-aware `when` example

```ini
[bind]
mods = Super+Shift
key = Right
action = scroll_next
when = [ "$MORPH_LAYOUT" = scroll ]

[bind]
mods = Super+Shift
key = Right
action = tile_right
when = [ "$MORPH_LAYOUT" = tile ]
```

### Security

**`command`** (for `exec`) and **`when`** are executed with **`posix_spawnp("sh", "-c", …)`**. Treat the config file as **trusted code**; do not point untrusted users at writable config paths.

---

## Section `[hooks]` (optional)

A single **`[hooks]`** block may define shell snippets run at lifecycle points. Each value is passed to **`/bin/sh -c`** (same trust model as **`exec`**).
Use **`${MORPH_USER_CONFIG_DIR}`** for user hook files and **`${MORPH_SYSTEM_CONFIG_DIR}`** only for managed/system hook files; in release sessions the system directory normally resolves to `/etc/morph`.

| Key | When it runs |
|-----|----------------|
| **`startup`** / **`on_startup`** | After the compositor has started the backend and set **`WAYLAND_DISPLAY`** (async: morph does not wait for the shell to exit). |
| **`shutdown`** / **`on_shutdown`** | When the compositor is exiting after a normal session (**after** `wl_display_run` returns). Morph **waits** for this process to exit before tearing down the display. |
| **`reload`** / **`on_reload`** | After a successful **config reload** (IPC **`reload config`** or **`reload`**, or **`morph --reload-config`**). Runs under the **new** config (async). In the managed session flow, file-based reload hooks also receive helper functions such as `reload <cmd ...>`. |

If no startup config path can be resolved and no default config file exists, **`reload config`** cannot resolve a path and fails with a log message.

In the **managed** session flow, a hook value that resolves to a **file path** is sourced as a hook file. If that file is not readable, the packaged hook of the same kind in the system config directory (**`/etc/morph/startup.sh`** and friends) is sourced instead, so a default install still runs its shipped session components before the user creates `~/.config/morph`. The hook is only skipped when neither path is readable, and the log names both paths that were tried. A configured path that does not exist is **never** re-interpreted as a shell command.

---

## Section `[layout_anim]` (optional)

A single block **`[layout_anim]`** (alias **`[layout_animation]`**) configures **scene-node** easing when tiled or scroll-layout windows **move** to new cells or scroll positions. Client **configure / resize** is unchanged; only translation is smoothed. If the section is omitted, easing is **on** with the defaults below.

| Key | Default | Meaning |
|-----|---------|---------|
| **`enabled`** / **`enable`** | `yes` | `yes` / `true` / `1` / `on` or `no` / `false` / `0` / `off`. When off, positions snap immediately on every arrange. |
| **`lambda`** / **`speed`** | `15` | Exponential approach rate per second (higher = snappier). Allowed range **0.5–120**. |
| **`epsilon`** / **`snap`** | `0.35` | Pixel distance below which a window snaps to its target. Allowed range **0.05–64**. |

Changes apply after **config reload** (or compositor restart).

---

## Section `[focus]` (optional)

A single **`[focus]`** block selects the pointer-driven keyboard focus policy for XDG toplevels. If omitted, Morph uses **`ClickToFocus`** (unchanged from the historical default).

| Key | Default | Meaning |
|-----|---------|---------|
| **`policy`** / **`mode`** / **`focus_policy`** | `ClickToFocus` | One of the policies below. |

Accepted policy names (case-insensitive; underscores and hyphens are allowed):

| Policy | Aliases | Behavior |
|--------|---------|----------|
| **`ClickToFocus`** | `click`, `click_to_focus`, `click-to-focus` | Clicking a toplevel gives it keyboard focus. Clicking empty root space clears keyboard focus. Pointer motion alone does not change keyboard focus. |
| **`FocusFollowsMouse`** | `ffm`, `focus_follows_mouse`, `focus-follows-mouse`, `mouse` | Pointer enter over an XDG toplevel gives it keyboard focus. Leaving to empty root space or a desktop-like BACKGROUND/BOTTOM layer (wallpaper) clears keyboard focus. |
| **`SloppyFocus`** | `sloppy`, `sloppy_focus`, `sloppy-focus` | Pointer enter over an XDG toplevel gives it keyboard focus. Passing over empty root or a desktop-like wallpaper layer does **not** clear focus; focus stays until another focus target is selected (another window via enter/click, map/activation, workspace switch, etc.). Empty-root clicks also keep focus. |

### Interaction rules

These rules apply to all policies and are part of the stable contract:

- **Layer-shell** (panels / overlays): keyboard focus moves to an interactive layer surface only on **click/tap**, never on pointer enter. Hovering a panel does not clear an XDG toplevel’s keyboard focus under FocusFollowsMouse or SloppyFocus.
- **Root / empty desktop**: ClickToFocus and FocusFollowsMouse clear keyboard focus on an empty-root click (or a click on a BACKGROUND/BOTTOM wallpaper layer); SloppyFocus does not. FocusFollowsMouse additionally clears when the pointer leaves onto empty root / desktop wallpaper via motion. In a fully tiled layout with no visible desktop, SloppyFocus and FocusFollowsMouse look the same until the pointer rests on uncovered desktop or wallpaper.
- **Compositor move/resize grabs** (`Super+drag`, edge resize): motion during an active grab does not apply FocusFollowsMouse / SloppyFocus handoff. Explicit focus still happens when a grab is armed on a toplevel.
- **Non-pointer focus** (toplevel map, foreign-toplevel / xdg-activation, workspace switch, keybinds): always uses the compositor’s normal focus path and ignores the mouse policy.
- **Future server-side decorations**: SSD chrome belonging to a toplevel is expected to follow the same enter/click rules as the window body (no separate focus policy for chrome).

Example:

```ini
[focus]
policy = FocusFollowsMouse
```

Changes apply after **config reload** (or compositor restart). Runtime overrides are also available via IPC **`focus …`** / **`morph --focus`** (see below); a later **`reload config`** restores the value from the file.

---

## Section `[decoration]` (optional)

Optional defaults for **xdg-decoration** (see **`[decoration_rule]`** below). If omitted, the compositor defaults to **stripping** client-side title bars in **tile** and **scroll** layouts, for clients that support the protocol.

| Key | Default | Meaning |
|-----|---------|---------|
| **`strip_in_tile_scroll`** / **`default_strip`** / **`strip`** | `yes` | If **yes**, windows that do **not** match a `[decoration_rule]` use **server-side** decoration mode in tile/scroll (clients hide their own title bar; morph does not draw SSD, so the window appears frameless). If **no**, those windows keep **client-side** decorations in tile/scroll. |

**Stack** layout and **tile-float** windows always use **client-side** decorations (normal title bars), regardless of this section.

---

## Section `[decoration_rule]` (optional)

POSIX **extended** regex rules, same style as **`[tile_rule]`**: optional **`app_id`** and/or **`title`** (both must match when both are present). **First matching rule wins** in file order.

| Key | Meaning |
|-----|---------|
| **`app_id`** / **`app-id`** | Regex matched against the XDG `app_id`. |
| **`title`** | Regex matched against the window title. |
| **`strip`** | `yes` or `no` — same as “strip client title bar in tile/scroll” for this match. If omitted in a rule block, it defaults to **yes**. |

Use **`strip = no`** on specific apps (e.g. video players) to keep their client decorations while tiled.

---

## Section `[input_map]` (optional)

Rules in **`[input_map]`** map physical input devices to one output by connector name. This is mainly for touch and tablet devices that should always follow a specific monitor. **First matching rule wins** in file order.

| Key | Meaning |
|-----|---------|
| **`match`** / **`name`** | POSIX **extended** regex matched against the device name. |
| **`output`** | Output connector name, for example `eDP-1`, `HDMI-A-1`, or `DP-2`. |
| **`type`** | Optional comma/space-separated filter: **`touch`**, **`tablet`** / **`stylus`** / **`pen`**, **`pointer`** / **`mouse`**, or **`all`** / **`both`**. If omitted, the rule applies to **touch** and **tablet** devices. |

If both **`match`** and **`output`** are present, the rule is applied to matching devices of the selected type(s). A rule that omits either key is rejected at load time.

Example:

```ini
[input_map]
match = ^HUION .*
output = DP-2
type = tablet
```

## Actions reference

Unless noted, tiling-related actions are **no-ops** when not in **tile** layout, when there is **no focused** toplevel, when the focused surface is **unmapped**, or when the focused window is a **tile float** (floating within tile mode).

### General

| `action` | Aliases | `command` | Effect |
|----------|---------|-----------|--------|
| **`quit`** | `exit` | — | Terminate the compositor. |
| **`close`** | `kill` | — | Send close to the focused XDG toplevel. |
| **`exec`** | `spawn` | **Required** | Run `command` under `/bin/sh -c`. |
| **`layout_toggle`** | `toggle_layout` | — | Switch between stack and tile layout (scroll switches to stack). |
| **`layout_tile`** | `tile` | — | Force **tile** layout. |
| **`layout_stack`** | `stack` | — | Force **stack** layout. |
| **`layout_scroll`** | `scroll` | — | Force **scroll** layout (niri-like horizontal strip). |

### Window focus cycling (Alt-Tab)

These cycle keyboard focus between windows in **focus-history** order (most recently used first), independent of layout.

| `action` | Aliases | `command` | Effect |
|----------|---------|-----------|--------|
| **`nextWindow`** | `window_next`, `focus_next` | — | Focus the **next** window in focus history (wraps). |
| **`prevWindow`** | `window_prev`, `focus_prev` | — | Focus the **previous** window in focus history (wraps). |

Candidates are **mapped**, **initialized**, **non-minimized** XDG toplevels on the **current workspace**. Minimized windows, windows on other workspaces, and stale/unfocusable views are skipped. With fewer than **two** candidates, focus is left unchanged.

#### Held-modifier cycling

If the bind has a modifier (the **`Alt`** in `Alt+Tab`), **keeping it held** starts a cycling **session**: the candidate list is captured once and **frozen**, so each further press walks one step further down focus history instead of bouncing between the two most recent windows. Each step **previews** focus on the highlighted window; focus history is only reordered when the session ends. Releasing the modifier **commits** the selection and promotes it to the front of focus history. Pressing **`Esc`** during a session **cancels** it and returns to the window you started from; that `Esc` is consumed and not delivered to the window.

**`Shift`** is treated as a direction selector rather than part of the held chord, so an `Alt+Tab` bind and an `Alt+Shift+Tab` bind share a **single** session — you can hold `Alt`, overshoot with `Tab`, and step back with `Shift+Tab` without restarting the cycle.

A bind with **no** modifiers (or with `Shift` only) has nothing to hold, so it commits immediately on every press and therefore alternates between the two most recently used windows. The same applies to the IPC and CLI entry points, which are single-shot by nature.

Sessions also end on their own when the workspace changes, and windows that close mid-cycle drop out of the frozen list. While the session is held, Morph draws a compositor-owned **switcher overlay** centered on the output of the highlighted window: each row shows the window **title** and **app_id** (when they differ), and the current candidate is highlighted. The overlay is visual-only (pointer events pass through). Releasing the modifier and pressing **`Esc`** both dismiss it.

```ini
[bind]
mods = Alt
key = Tab
action = nextWindow

[bind]
mods = Alt+Shift
key = Tab
action = prevWindow
```

### Workspaces (9 virtual desktops)

There are **nine** workspaces (**`1`**..**`9`** in config and IPC; internally zero-based). New windows open on the **current** workspace. Only windows on the **active** workspace are **visible** and receive pointer hits; tiling and scroll logic apply **per workspace**. In **scroll** layout, the visible column index is stored **per workspace and per physical output** (multi-monitor: each head scrolls independently). **`when=`** predicates can use **[`MORPH_WORKSPACE`](ENVIRONMENT.md#morph-workspace)**.

| `action` | Aliases | `command` | Effect |
|----------|---------|-----------|--------|
| **`workspace`** | `workspace_goto` | **Required** `1`..`9` | Switch to that workspace (focus the **most recently focused** mapped XDG window there, or clear keyboard focus). |
| **`workspace_next`** | `ws_next` | — | Next workspace (wraps **9** → **1**). |
| **`workspace_prev`** | `ws_prev` | — | Previous workspace (wraps **1** → **9**). |
| **`workspace_move`** | `ws_move` | **Required** `1`..`9` | Move the **focused** toplevel to that workspace (refocus on the current workspace if it left). |

### Sort order (linear index along the tiled list)

These move the **focused tiled** window along the internal sort order (row-major grid order, not “same row only”).

| `action` | Aliases | `command` | Effect |
|----------|---------|-----------|--------|
| **`tile_swap_prev`** | `tile_prev`, **`tile_left`**, `scroll_prev`, `scroll_left` | — | One step toward the **start** of the sort list. |
| **`tile_swap_next`** | `tile_next`, **`tile_right`**, `scroll_next`, `scroll_right` | — | One step toward the **end** of the sort list. |
| **`tile_to_first`** | `tile_first`, `tile_begin` | — | Bubble to the **first** slot in sort order. |
| **`tile_to_last`** | `tile_last`, `tile_end` | — | Bubble to the **last** slot in sort order. |
| **`tile_move`** | `tile_shift` | Optional signed integer; default **`1`** | Move **N** steps along sort order (positive = toward end, negative = toward start). |

### Grid moves (geometry: same row or same column)

The tile grid uses a fixed column count derived from the number of tiled windows (see compositor notes). **`tile_up` / `tile_down`** move one row within the **same column**; **`tile_grid_move`** can move **left/right** on the **same row**, or **up/down** in the **same column**, including multi-step forms.

| `action` | Aliases | `command` | Effect |
|----------|---------|-----------|--------|
| **`tile_grid_up`** | `tile_up` | — | Swap with the cell **one row up** (same column). |
| **`tile_grid_down`** | `tile_down` | — | Swap with the cell **one row down** (same column). |
| **`tile_column_top`** | `tile_col_top`, `tile_grid_top` | — | Bubble to the **top** of the current column. |
| **`tile_column_bottom`** | `tile_col_bottom`, `tile_grid_bottom` | — | Bubble to the **bottom** of the current column. |
| **`tile_grid_move`** | `tile_row_move` | **Required** | See **`tile_grid_move` `command=`** below. |

#### `tile_grid_move` `command=`

Exactly one of the following forms (after trimming surrounding space):

1. **Direction only:** `left`, `right`, `up`, `down`, `top`, `bottom` — one step, or column edge for **`top`** / **`bottom`**.
2. **Direction and count:** `DIR N` where **N** is an integer **≥ 1** (e.g. `left 3`, `up 2`). **`left`** / **`right`** move on the **same row**; **`up`** / **`down`** on the **same column**.
3. **Legacy vertical:** a **bare signed integer** (whole `command=` value is only a number, optional leading sign). Positive = **down**, negative = **up**, in grid rows.

Invalid or trailing junk after a valid prefix causes the bind line to be rejected at load time.

---

## Section `[tile_rule]` (optional)

Rules control how individual XDG toplevels behave in **tile** layout. **First matching rule in file order wins.**

| Key | Meaning |
|-----|---------|
| **`app_id`** / **`app-id`** | POSIX **extended** regex matched against the window’s **app_id** (Wayland). |
| **`title`** | POSIX **extended** regex matched against the window’s **title**. |
| **`mode`** | **`tile`** / **`tiled`** (default): normal grid cell. **`float`** / **`floating`**: not placed in the grid; shown centered when mapped and raised on focus in tile mode. |
| **`order`** | Integer; **lower** values sort **earlier** in the tile list among tiled windows. |

If both **`app_id`** and **`title`** are set on a rule, **both** must match. At least one of **`app_id`** or **`title`** must be present for the rule to apply.

---

## Related: IPC and CLI (not config syntax)

From another terminal you can send one-line commands to a running instance (Unix socket under **`$XDG_RUNTIME_DIR/morph-ipc.sock`** when IPC is enabled), for example:

These are the current compositor control commands for local use. The Wayland-facing workspace protocol is intentionally narrower for now: **`activate`** works, while **`create_workspace`**, **`remove_workspace`**, and **`assign_workspace`** are still no-ops for external clients that talk to **`ext_workspace_manager_v1`**.

- **`layout stack`**, **`layout tile`**, **`layout scroll`**, **`layout toggle`**
- **`focus ClickToFocus`**, **`focus FocusFollowsMouse`**, **`focus SloppyFocus`** (aliases: `click`, `ffm`, `sloppy`, …) — set pointer-driven keyboard focus policy at runtime (does not rewrite the config file; **`reload config`** restores the file value)
- **`tile move …`** (same semantics as sort-order actions above)
- **`scroll …`** or **`scroll move …`** — **`prev`** / **`left`**, **`next`** / **`right`**, or a **signed integer** (viewport steps in scroll layout; no-op if not in scroll layout)
- **`tile grid …`** (same forms as **`tile_grid_move` `command=`**)
- **`reload config`** or **`reload`** — re-source the system and user **`environment`** files, re-read the config file (same path as at startup, or the default path), replace keybinds and tile rules, refresh tile layout if applicable, then run the new file’s **`reload`** hook from **`[hooks]`**. In the managed launcher flow this passes through `system_reload.sh` first. Some environment variables only take effect on a session restart; see **`docs/ENVIRONMENT.md`** for the per-variable list.
- **`workspace N`** — switch to workspace **`1`**..**`9`**
- **`workspace next`** / **`workspace prev`** — cycle workspaces (wraps)
- **`workspace move N`** — move the focused toplevel to workspace **`N`**

The **`morph`** binary also accepts **`--layout`**, **`--scroll`** (same as **`--layout scroll`**), **`--focus`**, **`--tile-move`**, **`--scroll-move`**, **`--tile-grid`**, **`--workspace`** **`1`**..**`9`** or **`next`** / **`prev`**, **`--workspace-move`** **`N`** (move focused window to workspace **`N`**), **`--reload-config`** for scripting, and **`--allow-builtin-fallback`** to keep the old no-config fallback behavior; see **`docs/CLI.md`** for behavior when no compositor is listening.

---

## See also

- **`docs/COMPOSITOR.md`** — compositor scope, architecture notes, build and run.
- **`docs/PROTOCOLS.md`** — Wayland globals, wlroots vs portal expectations, unsupported protocols.
- **`morph.conf.example`** — commented sample file in the repo.
