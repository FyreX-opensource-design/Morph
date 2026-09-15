# Morph CLI Guide

This document collects Morph's command-line interface in one place.
Use it when you want to start a new compositor instance, send an action to a running instance, or understand which flags are startup-only versus IPC-oriented.

Primary implementation reference:
- [`src/main.c:4319`](../src/main.c#L4319)
- [`src/main.c:4512`](../src/main.c#L4512)

## Table of Contents

- [Invocation Model](#invocation-model)
- [General Options](#general-options)
- [Logging Options](#logging-options)
- [Crash Handling Options](#crash-handling-options)
- [Runtime Control Options](#runtime-control-options)
- [Behavior Notes](#behavior-notes)
- [Examples](#examples)
- [Related Documents](#related-documents)

## Invocation Model

Morph has two CLI usage patterns:

1. Start a new compositor instance
2. Send a command to an already running compositor through IPC

Some flags always affect local process startup, for example `--config` or `--log-level`.
Other flags are IPC-aware and may send an action to an already running compositor instead of starting a second instance.

## General Options

| Option | Meaning | Notes |
|---|---|---|
| `-h`, `--help` | Show help and exit | Printed by [`src/main.c:4319`](../src/main.c#L4319) |
| `-c PATH`, `--config PATH` | Load config from `PATH` | Highest config priority; also documented in [`docs/CONFIG.md`](CONFIG.md) |
| `--allow-builtin-fallback` | Start without a config file using the four built-in emergency bindings | Mirrors [`MORPH_ALLOW_BUILTIN_FALLBACK=1`](ENVIRONMENT.md#morph-allow-builtin-fallback) |
| `--ipc` | Keep compatibility; IPC is already default-on when `XDG_RUNTIME_DIR` exists | Mostly useful for older scripts |
| `--no-ipc` | Disable the IPC socket for this instance | Prevents local socket creation |

## Logging Options

| Option | Meaning | Notes |
|---|---|---|
| `--log-level silent|error|info|debug` | Set startup log verbosity | Parsed early before wlroots setup |
| `--quiet` | Shortcut for `--log-level error` | |
| `--verbose` | Shortcut for `--log-level debug` | |
| `--log-file PATH` | Append logs to `PATH` | Keeps stderr logging and adds a file sink |

These options are also reflected in launcher behavior through [`MORPH_DBG`](ENVIRONMENT.md#morph-dbg), but the CLI flags act directly on the `morph` binary.

## Crash Handling Options

| Option | Meaning | Notes |
|---|---|---|
| `--crash-log PATH` | Append crash markers to `PATH` | See [`docs/CRASHING.md`](CRASHING.md) |
| `--no-crash-handler` | Disable crash handler installation | Useful for debugger runs |
| `--crash-test` | Deliberately trigger `SIGSEGV` after startup | Debug/testing aid |

## Runtime Control Options

These flags are IPC-aware and may talk to a running Morph instance.

### Layout Selection

| Option | Meaning | Behavior |
|---|---|---|
| `--layout stack` | Select stack layout | Starts a new compositor if needed; otherwise sends `layout stack` over IPC |
| `--layout tile` | Select tile layout | Same IPC-aware behavior |
| `--layout scroll` | Select scroll layout | Same IPC-aware behavior |
| `--scroll` | Shortcut for `--layout scroll` | Same IPC-aware behavior |

### Tile and Scroll Movement

| Option | Accepted Values | Behavior |
|---|---|---|
| `--tile-move ARG` | `prev`, `next`, `left`, `right`, `first`, `last`, signed integer | Sends `tile move ...` over IPC; exits `1` if no compositor is listening |
| `--tile-grid ARG [COUNT]` | `up`, `down`, `left`, `right`, `top`, `bottom`, signed integer, or `DIR COUNT` | Sends `tile grid ...` over IPC; exits `1` if no compositor is listening |
| `--scroll-move ARG` | `prev`, `next`, `left`, `right`, signed integer | Sends `scroll ...` over IPC; exits `1` if no compositor is listening |

### Workspace Control

| Option | Accepted Values | Behavior |
|---|---|---|
| `--workspace ARG` | `1`..`9`, `next`, `prev` | Sends `workspace ...` over IPC; exits `1` if no compositor is listening |
| `--workspace-move N` | `1`..`9` | Sends `workspace move N` over IPC; exits `1` if no compositor is listening |

### Window Focus Cycling

| Option | Accepted Values | Behavior |
|---|---|---|
| `--window-focus ARG` | `next`, `prev` | Sends `window focus ...` over IPC; exits `1` if no compositor is listening |

Cycling walks focus history (most recently used first) across mapped, non-minimized windows on the current workspace. See [`docs/CONFIG.md`](CONFIG.md) for the `nextWindow` / `prevWindow` keybind actions that share this policy.

### Focus Policy

| Option | Accepted Values | Behavior |
|---|---|---|
| `--focus POLICY` | `ClickToFocus`, `FocusFollowsMouse`, `SloppyFocus` (and aliases such as `click`, `ffm`, `mouse`, `sloppy`) | Sends `focus …` over IPC when a compositor is listening; otherwise starts a new instance with that policy overriding the config file value. A later `reload config` restores the file value. |

### Reload Control

| Option | Meaning | Behavior |
|---|---|---|
| `--reload-config` | Trigger config reload on a running compositor | Sends `reload config` over IPC; exits `1` if no compositor is listening |

## Behavior Notes

- `--config` only affects the process being started locally; it does not send a config path to an already running instance.
- `--reload-config`, `--tile-move`, `--tile-grid`, `--scroll-move`, `--workspace`, `--workspace-move`, and `--window-focus` require a running Morph instance with IPC enabled.
- `--layout`, `--scroll`, and `--focus` are special: they can start a new compositor instance or talk to an existing one.
- `--no-ipc` disables socket creation for the current process, so later IPC-based commands cannot target that instance.
- `--allow-builtin-fallback` exports the same policy into the process environment so startup and reload follow the same fallback contract.
- Runtime command behavior is implemented in [`src/main.c`](../src/main.c) (`ipc_process_line`).

## Examples

Start Morph with an explicit config:

```bash
./build/morph --config /tmp/morph-debug.conf
```

Start Morph with verbose logs written to a file:

```bash
./build/morph --verbose --log-file /tmp/morph.log
```

Allow the legacy builtin fallback only for this run:

```bash
./build/morph --allow-builtin-fallback
```

Switch the running compositor to scroll layout:

```bash
morph --layout scroll
```

Move the focused tiled window three slots left in grid terms:

```bash
morph --tile-grid left 3
```

Move to the next workspace on the running compositor:

```bash
morph --workspace next
```

Reload the running compositor config:

```bash
morph --reload-config
```

Cycle to the next window from a bar or script:

```bash
morph --window-focus next
```

Set FocusFollowsMouse on a running compositor (or at startup if none is listening):

```bash
morph --focus FocusFollowsMouse
```

Run without IPC socket creation:

```bash
./build/morph --no-ipc
```

## Related Documents

- [`docs/CONFIG.md`](CONFIG.md) for config file resolution and hook semantics
- [`docs/ENVIRONMENT.md`](ENVIRONMENT.md) for wrapper-driven environment variables
- [`docs/COMPOSITOR.md`](COMPOSITOR.md) for compositor scope and architecture notes
- [`docs/CRASHING.md`](CRASHING.md) for crash handling and debugging workflow
- [`docs/LAUNCHER.md`](LAUNCHER.md) for wrapper-managed session startup outside the bare binary
