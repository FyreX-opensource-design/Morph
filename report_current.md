# Morph Test Report for Branch `window-focus` (as of 2026-09-26)

This report is a practical manual checklist for the current `window-focus` work: MRU window cycling, configurable focus policies, held `Alt+Tab` with a compositor-owned switcher overlay, CLI/IPC triggers, and the startup-hook path fixes (`~` expansion and `/etc/morph` fallback). Tick items while testing and add notes next to them if useful.

Last checked against commit `6977e82` plus the current worktree changes described in this report.

Relevant commits on this branch:

- `810ecbd` Alt+Tab support (partial)
- `ddcb47e` fix `~` not being expanded in config
- `5e68ba1` continue implementing alt-tab (held-modifier session + overlay)
- `c795b90` fix environment not being sourced on `--reload-config`
- `562e523` focus policies and window-focus cycling (`ClickToFocus`, `FocusFollowsMouse`, `SloppyFocus`)
- `a4a8091` stabilized panel focus and pointer hit testing
- `a1b9d65` added XDG activation focus routing

**Note:** Overlay and keybind checks need a real nested or native Morph session with at least two mapped windows. IPC/CLI cycling can be driven from a second host terminal. Hook-path checks need the managed wrapper (`scripts/morph-session` or `testing/morph-session_dbg`).

## Legend

- `[ ]` Not tested yet
- `[x]` Tested successfully
- `[!]` Tested with notes or known limitation
- `[n/a]` Not applicable

Expected:

- What should be visible in a successful test

References:

- Relevant files or docs for follow-up

## Table of Contents

- [0. Automated Baseline](#0-automated-baseline)
- [1. Docs and CLI Surface](#1-docs-and-cli-surface)
- [2. Nested Session Setup](#2-nested-session-setup)
- [3. Immediate Cycle via IPC and CLI](#3-immediate-cycle-via-ipc-and-cli)
- [3.1 Focus Policies](#31-focus-policies)
- [4. Held Alt-Tab Switcher](#4-held-alt-tab-switcher)
- [5. Esc Cancel](#5-esc-cancel)
- [6. Workspace Isolation](#6-workspace-isolation)
- [7. Minimize, Close, and Empty List](#7-minimize-close-and-empty-list)
- [8. Layouts](#8-layouts)
- [9. Startup Hook Resolution](#9-startup-hook-resolution)
- [10. Reload Environment](#10-reload-environment)
- [11. Native / Portrait-Device Follow-Up](#11-native--portrait-device-follow-up)
- [12. Reduced Cross-Distro Matrix](#12-reduced-cross-distro-matrix)
- [Open Follow-Ups](#open-follow-ups)
- [Short Conclusion](#short-conclusion)

## 0. Automated Baseline

[x] **Build and run the automated suite**

```bash
meson setup build --reconfigure
meson compile -C build
meson test -C build --print-errorlogs
```

Expected:

- `morph` and `morph-test-config` compile (cairo / pangocairo are now required for the switcher overlay)
- `morph:config` passes, including `nextWindow` / `prevWindow` / alias parsing
- `morph:shell-runtime` passes, including tilde hook expansion and system-hook fallback

References:

- `meson.build`
- `tests/test_config.c`
- `tests/test_shell_runtime.sh`
- `docs/TESTS.md`

## 1. Docs and CLI Surface

[x] **`--help` lists `--window-focus next|prev`**

```bash
./build/morph --help | rg -n -- '--window-focus|nextWindow|workspace-move'
```

[x] **IPC-only command fails cleanly without a compositor**

```bash
test ! -S "$XDG_RUNTIME_DIR/morph-ipc.sock"
./build/morph --window-focus next; echo "exit=$?"
./build/morph --window-focus sideways; echo "exit=$?"
```

Expected:

- `--help` documents `--window-focus next|prev`
- Missing compositor: exit `1`, log mentions IPC failure
- Invalid value `sideways`: exit `1`, log says `use next or prev`
- Neither command starts a new compositor

References:

- `docs/CLI.md`
- `docs/CONFIG.md` (Window focus cycling)
- `src/main.c` (`print_usage`, `--window-focus` argv handling)

## 2. Nested Session Setup

Run these from a host terminal that already has `WAYLAND_DISPLAY` or a reachable `DISPLAY`. Nested is the right default for Alt-Tab; you can reach the nested Morph window and still drive IPC from the host.

[x] **Confirm the parent session is usable**

```bash
env | rg '^(WAYLAND_DISPLAY|DISPLAY|XDG_RUNTIME_DIR)='
```

[!] **Start nested Morph from this checkout**

```bash
meson compile -C build
./testing/morph-session_dbg
```

If you prefer the release wrapper against `build/morph`:

```bash
MORPH_BIN="$PWD/build/morph" \
  MORPH_SYSTEM_HOOK_DIR="$PWD/scripts" \
  MORPH_SYSTEM_CONFIG_DIR="$PWD/config" \
  MORPH_SYSTEM_CONFIG_FILE="$PWD/config/morph.conf" \
  sh ./scripts/morph-session
```

[!] **Open at least three windows inside Morph**

Use distinct titles so the overlay is easy to read, for example:

```bash
alacritty --title WIN-A
alacritty --title WIN-B
alacritty --title WIN-C
```

Click them in order A, then B, then C so focus history is C (newest), B, A.

Expected:

- Nested Morph starts and accepts clients
- Three titled windows are visible
- The last clicked window (C) has keyboard focus

References:

- `testing/morph-session_dbg`
- `config/morph.conf` (`[bind]` Alt+Tab / Alt+Shift+Tab)
- `docs/TESTING.md`

## 3. Immediate Cycle via IPC and CLI

Keep the nested session from section 2 running. Drive these from a **host** terminal (same user, same `XDG_RUNTIME_DIR`).

[x] **Socket is present**

```bash
test -S "$XDG_RUNTIME_DIR/morph-ipc.sock" && echo "ipc: OK"
```

[x] **CLI `--window-focus next` then `next` then `prev`**

With focus on WIN-C and history C, B, A:

```bash
./build/morph --window-focus next
# expect WIN-B
./build/morph --window-focus next
# expect WIN-C  (immediate cycle reorders MRU, so this toggles the two most recent)
./build/morph --window-focus prev
# expect WIN-A or the wrap target documented below
```

[x] **Raw IPC forms**

```bash
printf 'window focus next\n' | nc -U "$XDG_RUNTIME_DIR/morph-ipc.sock"
printf 'window next\n' | nc -U "$XDG_RUNTIME_DIR/morph-ipc.sock"
printf 'window focus prev\n' | nc -U "$XDG_RUNTIME_DIR/morph-ipc.sock"
```

Expected:

- Each CLI command exits `0` and does **not** start a second compositor
- Immediate cycle **commits on every press**, so repeating `next` alternates the two most recently used windows (C ↔ B after the first step from C)
- No switcher overlay appears for IPC/CLI (there is no modifier to hold)
- `window next` and `window focus next` are equivalent
- With only one mapped window, both `next` and `prev` are no-ops (focus unchanged)

References:

- `docs/CLI.md` (Window Focus Cycling)
- `src/main.c` (`ipc_process_line`, `server_window_focus_cycle`)

## 3.1 Focus Policies

Use the three windows from section 2 and leave some desktop background visible.

### 3.1.1 ClickToFocus

[x] **Pointer motion does not change focus; clicking a window does**

```bash
./build_dbg/morph --focus ClickToFocus
```

[!] **Clicking empty root clears XDG keyboard focus**

The window becomes inactive. sfwbar 0.8.2 on Debian Stable may keep the last task visually highlighted even though Morph reports `activated = false` through foreign-toplevel management.

### 3.1.2 FocusFollowsMouse

[x] **Pointer enter focuses the window and empty root clears focus**

```bash
./build_dbg/morph --focus FocusFollowsMouse
```

[x] **Hovering the panel does not clear window focus**

Expected: keyboard focus follows windows without a click; merely crossing the bottom panel does not clear it.

### 3.1.3 SloppyFocus

[x] **Pointer enter focuses, while empty root keeps the last focus**

```bash
./build_dbg/morph --focus SloppyFocus
```

[x] **Another window can still take focus on pointer enter**

### 3.1.4 Runtime switch and reload

[x] **Switch policy through CLI/IPC and restore config with reload**

```bash
./build_dbg/morph --focus FocusFollowsMouse
./build_dbg/morph --focus SloppyFocus
./build_dbg/morph --reload-config
```

Expected: each command succeeds immediately; reload restores the policy from `[focus]`.

References: `docs/CONFIG.md`, `src/main.c` (`server_focus_policy`, `process_cursor_motion`, `server_cursor_button`)

## 4. Held Alt-Tab Switcher

This is the main interactive check. Use the nested Morph window from section 2, with at least three mapped windows.

Default binds in `config/morph.conf`:

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

[x] **Hold Alt, tap Tab once**

Expected:

- Focus previews the next window in MRU order (from C, that is B)
- A compositor-owned overlay appears, centered on the output of the highlighted window
- Rows show `title · app_id` (app_id omitted when it equals the title)
- The current candidate is highlighted (Morph orange)
- Overlay stays up while Alt remains held

[x] **Keep Alt held, tap Tab repeatedly**

Expected:

- Each tap walks **further** down the frozen list (B → A → C → B …), not just C ↔ B
- Overlay highlight tracks the previewed window
- Focus history is **not** reordered until Alt is released

[x] **Keep Alt held, tap Shift+Tab**

Expected:

- Direction reverses inside the **same** session (no restart)
- Highlight and preview move backward

[!] **Release Alt**

Expected:

- Overlay disappears
- The previewed window keeps focus
- That window is now most-recent in history, so a later single `Alt+Tab` tap (new session) should offer the previous window next

[x] **Pointer pass-through**

While the overlay is visible, move the pointer over it and click.

Expected:

- Click reaches the window underneath (or empty root), not a dead overlay hit-target
- Overlay is visual-only

[x] **Single-window session**

Close all but one window, then Alt+Tab.

Expected:

- No overlay
- Focus unchanged

References:

- `docs/CONFIG.md` (Held-modifier cycling)
- `docs/COMPOSITOR.md` (Window focus cycling)
- `src/ui.c`, `src/ui.h`
- `src/main.c` (`server_window_cycle_step`, `window_cycle_overlay_sync`)

## 5. Esc Cancel

[!] **Start a held session, move at least two steps, press Esc, then release Alt**

Expected:

- Esc dismisses the overlay immediately
- Focus returns to the window that had focus when the session started
- Esc is not delivered to that window (no unexpected “close dialog” / cancel in the client)
- Releasing Alt afterwards does not re-commit the cancelled preview

References:

- `src/main.c` (`keyboard_handle_key` Escape intercept, `server_window_cycle_cancel`)

## 6. Workspace Isolation

Default workspace count is 9. Use Super+1 / Super+2 if those binds are present, or:

```bash
./build/morph --workspace-move 2
./build/morph --workspace 1
./build/morph --window-focus next
./build/morph --workspace 2
```

[x] **Move one window to workspace 2, cycle on workspace 1**

Expected:

- Workspace 1 cycle never selects the window that left
- Overlay (held Alt+Tab on workspace 1) lists only workspace 1 windows

[x] **Switch to workspace 2**

Expected:

- Focus restores the most recently used window **on that workspace**
- A held Alt+Tab session that was open on workspace 1 is committed/ended (workspace change ends the session)

References:

- `src/main.c` (`toplevel_focus_candidate`, `server_workspace_go`)
- `docs/CONFIG.md` (Workspaces)

## 7. Minimize, Close, and Empty List

[x] **Minimize the focused window, then Alt+Tab / `--window-focus next`**

Expected:

- Minimized windows are skipped
- They do not appear in the overlay

[x] **Close a window while a held Alt+Tab session is open**

Expected:

- Closed window drops out of the overlay list
- Session stays alive if at least one candidate remains
- Morph does not crash

[x] **Close every window, then cycle**

Expected:

- No overlay
- IPC `window focus next` is a no-op
- Morph stays running

References:

- `src/main.c` (`window_cycle_forget`, `toplevel_focus_candidate`)

[x] **Minimize a candidate during a held cycle**

Start Super+Tab, keep Super held, minimize the next candidate, step again, and release Super. The minimized window must disappear from or be skipped by the overlay and must never receive focus.

[x] **Unmap a candidate during a held cycle**

Run `sleep 10; exit` in one candidate, select it while holding Super, then wait for it to unmap. No stale row or unfocusable MRU entry may remain.

[x] **Run an IPC cycle while a held cycle is active**

```bash
sleep 5; ./build_dbg/morph --window-focus next
```

The frozen-ring selection, orange overlay marker, and actual focus must advance together; the held session stays active until Super is released.

## 8. Layouts

Repeat a short held Alt+Tab (two taps, release) in each layout.

[x] **Stack**

[x] **Tile** (`Super+Space` or `./build/morph --layout tile`)

[x] **Scroll** (`./build/morph --layout scroll`)

Expected:

- Overlay still appears and tracks the preview
- Previewed window is the one that actually receives keys after release
- Tile/scroll arrangement does not jump the window to a new slot merely because it was previewed; history reorder happens on commit

References:

- `docs/COMPOSITOR.md`
- `src/main.c` (`cycle.suppress_mru`)

## 9. Startup Hook Resolution

These checks validate the hook-path bugs found on the portrait device: literal `~/...` never expanded, and a missing user hook skipped `/etc/morph/startup.sh`.

Use a nested wrapper run so you can read the startup log without taking over the TTY.

Set:

```bash
export LOG_BASE="${XDG_STATE_HOME:-$HOME/.local/state}/morph"
```

Nested logs are `morph-nested-startup.log`.

### 9.1. Tilde path in `[hooks] startup`

[x] **Config uses `startup = ~/.config/morph/startup.sh` and the file exists and is readable**

Start nested Morph, then:

```bash
rg -n 'Sourcing user startup hook file from config:|not readable|Sourcing system startup hook' \
  "$LOG_BASE"/morph-nested-startup.log
```

Expected:

- Log shows a **fully expanded** path (`/home/<user>/.config/morph/startup.sh`), not a literal `~/...`
- Log contains `Sourcing user startup hook file from config:`
- Hook body actually ran (marker file, `wlr-randr`, `log_startup`, etc.)

### 9.2. Missing user hook falls back to the system hook

[ ] **Config points at `${MORPH_USER_CONFIG_DIR}/startup.sh` or `~/.config/morph/startup.sh`, but that file is absent**

Expected:

- Log contains `Configured user startup hook file is not readable:` with an expanded path
- Log then contains `Sourcing system startup hook:` pointing at the managed config dir (`config/startup.sh` in a repo wrapper run, `/etc/morph/startup.sh` when installed)
- Packaged hook body ran
- The configured path is **not** executed as `sh -c ~/.config/morph/startup.sh`

References:

- `scripts/shell-helpers.sh` (`morph_resolve_hook_path`, `morph_source_system_hook`)
- `docs/LAUNCHER.md` (Startup Hook Resolution)
- `docs/CONFIG.md` (`[hooks]`)

## 10. Reload Environment

[x] **Run reload in a nested Wayland session**

Start `./testing/morph-session_dbg` from an existing Wayland session. Confirm `nested-wayland`, `WLR_BACKENDS: wayland`, and `WLR_WL_SOCKET: wayland-nested` in the startup log, then run from a second host terminal:

```bash
test -S "$XDG_RUNTIME_DIR/morph-ipc.sock"
./build/morph --reload-config
LOG_BASE="${XDG_STATE_HOME:-$HOME/.local/state}/morph"
rg -n "reload: sourced environment|Starting managed reload hook|Managed reload hook completed" \
  "$LOG_BASE" -g "morph*.log"
```

Expected: reload exits `0`, environment files are sourced before the new config, and the session remains running. An X11 host tests `nested-x11` instead and must be recorded as such.

References: `docs/CLI.md`, `docs/ENVIRONMENT.md`, commit `c795b90`

## 11. Native / Portrait-Device Follow-Up

Optional, but this is the setup that originally showed the hook bug (monitor defaults to portrait, `wlr-randr` in `startup.sh`).

[ ] **Install current `shell-helpers.sh` and compositor binary on that device**

[ ] **Native login, then confirm startup log**

```bash
rg -n 'Sourcing user startup hook|Sourcing system startup hook|not readable' \
  "${XDG_STATE_HOME:-$HOME/.local/state}/morph/morph-startup.log"
```

Expected:

- With a user hook, the log contains `Sourcing user startup hook file from config:` and the expanded path.
- Without a user hook, `Configured user startup hook file is not readable:` is followed by `Sourcing system startup hook:`.
- A `not readable` entry without the system fallback is a failure.

[ ] **Confirm the output is rotated**

`wlr-randr` (or kanshi) from the hook that actually ran should match the physical orientation.

Expected:

- Hook log no longer shows a literal `~/...` skip
- Rotation applies without a manual `wlr-randr` after login
- Alt+Tab overlay still works on the rotated output (centered in that output’s workarea)

## 12. Reduced Cross-Distro Matrix

Run this reduced matrix on Debian, Fedora, and Arch-based systems. The focus is Alt-Tab, focus policies, and related edge cases; package versions are build prerequisites only.

| Test | Debian | Fedora | Arch |
|---|---|---|---|
| Automated build and tests | [ ] | [ ] | [ ] |
| One short nested smoke test | [ ] | [ ] | [ ] |
| Runtime installation | [ ] | [ ] | [ ] |
| Native Morph login | [ ] | [ ] | [ ] |
| Focus policies and cycling matrix | [ ] | [ ] | [ ] |
| Clean runtime shutdown | [ ] | [ ] | [ ] |

### 12.1 Build, tests, and nested smoke

```bash
meson setup build --reconfigure
meson compile -C build
meson test -C build --print-errorlogs
```

Start one nested session with `scripts/morph-session`, open one terminal, and exit with `quit`. Do not repeat the full focus matrix nested.

### 12.2 Install runtime and verify helpers

```bash
./scripts/morph-install.sh --runtime
command -v morph
command -v morph-session
morph --help
diff -q scripts/shell-helpers.sh /etc/morph/shell-helpers.sh
```

On Arch-based systems, `command -v` may report `/usr/sbin/morph` while `whereis` reports `/usr/bin/morph`. This is valid when `readlink -f /usr/sbin/morph` and `readlink -f /usr/bin/morph` resolve to the same binary.

### 12.3 Native Morph session

Log out, select **Morph** in the display manager, log in, and open three terminals titled WIN-A, WIN-B, and WIN-C. Click them in A, B, C order for deterministic focus history.

Repeat the focus-policy checks from section 3.1; immediate next/prev; held Alt+Tab and reverse cycling; Esc cancel; workspace isolation; minimize, unmap, and close during cycling; and a short Stack/Tile/Scroll pass.

Expected: overlay marker, actual focus, and MRU selection remain consistent; hidden or closed windows never remain selectable; panel workarea remains correct.

### 12.4 Runtime logs and shutdown

Confirm native mode, startup/reload hook completion, policy restoration after reload, and `Compositor exited cleanly` in `${XDG_STATE_HOME:-$HOME/.local/state}/morph`.

## Open Follow-Ups

- [ ] Switcher icons when a freedesktop icon exists for `app_id`
- [ ] Decide whether scratchpad, fullscreen, and per-output workspaces join the cycle candidate set
- [ ] Held-modifier overlay is compositor-input only; there is still no automated input harness for Alt+Tab itself
- [ ] Portal packages missing on the portrait device (`xdg-desktop-portal`, `-wlr`, `-gtk`) — environment gap, not this branch

## Short Conclusion

- Result: Debian Stable development-laptop checks in sections 0 through 10 are complete.
- Blocking issues: none for starting the reduced distro tests.
- Follow-up tests needed: Nathan must run section 11; Debian, Fedora, and Arch-based systems must complete section 12.
