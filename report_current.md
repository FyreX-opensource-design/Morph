# Morph Test Report for Branch `window-focus` (as of 2026-10-03)

This report is a practical manual checklist for the current `window-focus` work: MRU window cycling, configurable focus policies, held `Alt+Tab` with a compositor-owned switcher overlay, CLI/IPC triggers, and the startup-hook path fixes (`~` expansion and `/etc/morph` fallback). Tick items while testing and add notes next to them if useful.

Last checked against commit `1868074` plus the current worktree changes described in this report.

Relevant commits on this branch:

- `810ecbd` Alt+Tab support (partial)
- `ddcb47e` fix `~` not being expanded in config
- `5e68ba1` continue implementing alt-tab (held-modifier session + overlay)
- `c795b90` fix environment not being sourced on `--reload-config`
- `562e523` focus policies and window-focus cycling (`ClickToFocus`, `FocusFollowsMouse`, `SloppyFocus`)
- `a4a8091` stabilized panel focus and pointer hit testing
- `a1b9d65` added XDG activation focus routing
- `e99f0e9` stabilized Alt-Tab, focus handling, and panel integration
- `eb7c381` stabilized Shift-Tab handling, layout transitions, and forced uninstall cleanup
- `1868074` updated the sfwbar test configuration for sfwbar 1.0pre

**Note:** Overlay and keybind checks need a real nested or native Morph session with at least two mapped windows. IPC/CLI cycling can be driven from a second host terminal. Hook-path checks need the managed wrapper (`scripts/morph-session` or `testing/morph-session_dbg`).

## Legend

- `[ ]` Not tested yet
- `[x]` Tested successfully
- `[!]` Tested with notes or known limitation
- `[n/a]` Not applicable
- `[?]` Needs clarification or an external follow-up
- `NEW` Additional edge-case test for the focus-cycling session

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

[x] **VS Code keyboard focus after sfwbar restore**

Minimize VS Code, select it from sfwbar, and type directly in the editor without using Alt+Tab first. The restored window must become visible and accept keyboard input immediately. Geany and Alacritty remain useful control clients for the same sequence.

The trace confirms that VS Code uses its native Wayland backend. Morph handles sfwbar's separate foreign-toplevel `unset_minimized` request and preserves the already scheduled activation state when a combined maximize/size configure follows. After the activation configure ACK, Morph performs one keyboard leave/enter. The native retest showed `current:1,pending:1,scheduled:1`, and text entered in VS Code immediately without an Alt-Tab round trip.

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

This section validates the installed runtime path on three additional distributions.

The matrix focuses on Alt-Tab, focus policies, and directly related edge cases. Package names and library versions are checked only as prerequisites for the runtime build.

| Test | Debian | Fedora | Arch |
|---|---|---|---|
| Automated build and test run succeeds | [x] | [x] | [x] |
| One short nested smoke test succeeds | [x] | [x] | [x] |
| Runtime build and installation succeed | [x] | [x] | [x] |
| Morph session is available in the display manager | [x] | [x] | [x] |
| Native Morph session starts after logging in again | [x] | [x] | [x] |
| Focus policies | [x] | [x] | [x] |
| Immediate cycle and IPC/CLI | [x] | [x] | [x] |
| Held Alt-Tab switcher and Esc | [x] | [x] | [x] |
| Workspaces, minimize, and close | [x] | [x] | [x] |
| Layouts and panel workarea | [x] | [x] | [x] |

### 12.1 Automated Build and Test Run

[x] **Build and run the automated tests**

First change to the project directory:

```bash
meson setup build --reconfigure
meson compile -C build
meson test -C build --print-errorlogs
```

Expected: `morph` and `morph-test-config` compile, and the `config` and `shell-runtime` tests pass. This does not replace a native session test; it only confirms that the distribution can build the current source and run the automated contracts.

### 12.2 One Short Nested Smoke Test

[x] **Start nested Morph once and exit cleanly**

Run this short test before logging out of the current desktop session. Change to the project directory and run:

```bash
MORPH_ROOT="$PWD" \
MORPH_BIN="$MORPH_ROOT/build/morph" \
MORPH_SYSTEM_HOOK_DIR="$MORPH_ROOT/scripts" \
MORPH_SYSTEM_CONFIG_DIR="$MORPH_ROOT/config" \
MORPH_SYSTEM_CONFIG_FILE="$MORPH_ROOT/config/morph.conf" \
MORPH_LOG_DIR="/tmp/morph-distro-nested" \
sh "$MORPH_ROOT/scripts/morph-session"
```

Only verify startup, one terminal window, and `quit`; do not repeat the focus-cycling matrix in the nested session. Then inspect the startup log:

```bash
cat /tmp/morph-distro-nested/morph-nested-startup.log
```

Expected:

- The wrapper selects `nested-x11` or `nested-wayland`.
- Morph starts and accepts at least one terminal window.
- Morph exits cleanly after `quit`.

### 12.3 Install the Runtime

[x] **Install the runtime on the distribution**

Run this before logging out of the current desktop session:

```bash
./scripts/morph-install.sh --runtime
command -v morph
command -v morph-session
ls -l /usr/share/wayland-sessions/morph.desktop
ls -l /usr/bin/morph /usr/bin/morph-session /etc/morph/morph.conf
```

Expected:

- `morph` and `morph-session` are available through `PATH`.
- The display manager finds `morph.desktop`.
- `morph --help` uses the installed binary.
- `/etc/morph/` contains the runtime configuration and managed hook files.
- On Arch-based systems, merged `/usr` and `PATH` ordering may cause `command -v` to report `/usr/sbin/morph` while `whereis` reports `/usr/bin/morph`. This is valid when `readlink -f /usr/sbin/morph` and `readlink -f /usr/bin/morph` resolve to the same binary.

### 12.4 Compare Installed Helpers with the Checkout

[x] **The installed helper file matches the tested checkout**

```bash
diff -q scripts/shell-helpers.sh /etc/morph/shell-helpers.sh
```

Expected: there is no difference after `scripts/morph-install.sh --runtime`. Otherwise, an older installed helper could still contain the already fixed tilde-expansion or fallback bug.

### 12.5 `.bashrc` Preparation

Add the following aliases and functions to the test user's interactive `~/.bashrc`. `MORPH_ROOT` must point to the local Morph checkout. After installation, the test commands deliberately use the installed `morph` from `PATH`, not the checkout binary.

```bash
alias morph-ipc-next="morph --window-focus next"
alias morph-ipc-prev="morph --window-focus prev"

morph-runtime-log() {
    local log_base="${XDG_STATE_HOME:-$HOME/.local/state}/morph"
    tail -n 0 -F "$log_base/morph-startup.log" | \
        rg --line-buffered "Selected session mode|Native Wayland|keyboard:|window-cycle:|switcher:|focus:|Compositor exited"
}

morph-test-windows() {
    alacritty --title WIN-A &
    alacritty --title WIN-B &
    alacritty --title WIN-C &
}
```

Run `morph-test-windows` only after logging in to Morph. Open additional terminals inside the Morph session for logging and IPC commands.

### 12.6 Log Out and Back In to Morph

[x] **Start a native Morph session through the display manager**

1. Log out of the current desktop session.
2. Select **Morph** as the session in the display manager.
3. Log in again.
4. Open a terminal inside Morph and check:

```bash
printf "session=%s\n" "${XDG_CURRENT_DESKTOP:-unset}"
printf "wayland=%s\n" "${WAYLAND_DISPLAY:-unset}"
command -v morph
command -v morph-session
```

Expected:

- Morph runs as a native Wayland session with `WAYLAND_DISPLAY` set.
- `morph-session` and `morph` come from the installed runtime path.
- The startup log is `${XDG_STATE_HOME:-$HOME/.local/state}/morph/morph-startup.log`.
- The log reports `Selected session mode: native-tty` or the corresponding native session identifier, not a nested mode.

Then open three test windows:

```bash
morph-test-windows
```

Click the windows once in A, B, C order so focus history is deterministic for the following expectations.

### 12.7 Reduced Focus-Cycling Matrix

[x] **Test focus policies**

Use `morph --focus ClickToFocus`, `morph --focus FocusFollowsMouse`, and `morph --focus SloppyFocus` to repeat the tests from [section 3.1](#31-focus-policies): pointer motion, clicking a window, empty root, and behavior over the panel. Then restore the configured policy with `morph --reload-config`.

[x] **Test immediate cycle and IPC**

Run `morph-ipc-next`, `morph-ipc-prev`, and `morph --window-focus next|prev`. Verify focus history, wrap-around, no overlay for single-shot cycling, and equivalent behavior for the IPC aliases.

[x] **Test the held Alt-Tab switcher**

With at least three windows, test Alt+Tab, Shift+Alt+Tab, repeated Tab presses, direction changes, and releasing Alt. The overlay, orange marker, actual focus, and MRU commit must remain consistent.

[x] **Test Esc cancellation**

Cancel an active Alt-Tab session with Esc. The overlay must disappear, the original focus must return, and Esc must not be forwarded to the window.

[x] **Test workspaces, minimize, and close**

Move one window to another workspace ([Workspace Isolation](#6-workspace-isolation)), minimize a window during a cycle session, and close another window. Hidden or closed windows must not remain selectable in the ring ([section 7](#7-minimize-close-and-empty-list)).

[x] **Test Stack, Tile, and Scroll**

Run a short cycle in all three layouts. The overlay, focus, window positions, and panel-reserved workarea must remain correct.

### 12.8 Runtime Logs and Clean Shutdown

[x] **Verify the native session and lifecycle**

In another terminal:

```bash
morph-runtime-log
```

Or check directly:

```bash
LOG_BASE="${XDG_STATE_HOME:-$HOME/.local/state}/morph"
rg -n "Selected session mode|Native Wayland|Startup hook completed|Starting managed reload hook|Managed reload hook completed|Compositor exited cleanly" \
  "$LOG_BASE"/morph-startup.log "$LOG_BASE"/morph*.log
```

Expected:

- The runtime wrapper and startup hook were loaded from the installation.
- `morph --reload-config` returns successfully and the session remains active.
- Reload restores the focus policy from the configuration file.
- Morph exits cleanly through `quit`.

If a distribution fails this native matrix, repeat only the affected full section of the main report. The cross-distribution matrix does not require another nested run.

References: sections 3 through 11 of this report, `INSTALL.md`, `testing/test-howto_start-variants.nfo`, `sessions/morph.desktop`, `scripts/morph-install.sh`, `docs/CONFIG.md`, `docs/LAUNCHER.md`

## Open Follow-Ups

- [ ] Switcher icons when a freedesktop icon exists for `app_id`
- [ ] Decide whether scratchpad, fullscreen, and per-output workspaces join the cycle candidate set
- [?] Held-modifier overlay is compositor-input only; there is still no automated input harness for Alt+Tab itself
- [?] Portal packages missing on the portrait device (`xdg-desktop-portal`, `-wlr`, `-gtk`) — environment gap, not this branch

## Short Conclusion

- Result: The reduced native matrix passed on Debian Sid, Fedora 44, and Arch; the earlier development-laptop checks in sections 0 through 10 also passed.
- Blocking issues: none for the focus-cycling or sfwbar restore behavior.
- Follow-up tests needed: Nathan must run the native portrait-device checks in section 11.
