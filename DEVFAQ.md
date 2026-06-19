# DEV FAQ (stackcomp)

Practical troubleshooting notes for local development and debugging.

## 1) Debug log shows `modifier INVALID (0x00FFFFFFFFFFFFFF)`

Example line:

```text
DEBUG: [render/allocator/gbm.c:146] Allocated ... modifier INVALID (0x00FFFFFFFFFFFFFF)
```

What it means:

- `INVALID` here is a DRM/libdrm sentinel (`DRM_FORMAT_MOD_INVALID`).
- It usually means no explicit modifier was negotiated for that allocation path.
- In nested sessions (X11/Wayland backend), this is common and not automatically an error.

When it is fine:

- compositor starts and renders normally
- no follow-up import/allocation failures in logs

When to investigate further:

- black/frozen surfaces
- repeated errors like `failed to import dmabuf`, `EGLImage import failed`, allocator failures

## 2) Why do `--verbose`, `--quiet`, and `--log-level` exist?

- `--verbose` is a shortcut for `--log-level debug`.
- `--quiet` is a shortcut for low-noise mode (error-only threshold).
- `--log-level` is the explicit form for exact control: `silent|error|info|debug`.

Recommended test pattern:

```bash
rm -f /tmp/sc-quiet.log /tmp/sc-debug.log
env -u WAYLAND_DISPLAY ./build/stackcomp --quiet --log-file /tmp/sc-quiet.log
env -u WAYLAND_DISPLAY ./build/stackcomp --verbose --log-file /tmp/sc-debug.log
```

## 3) Why did `--help` / `-h` fail before?

Older parser revisions had no help branch and treated unknown flags as errors.

Current behavior:

- `-h` and `--help` are supported and print CLI usage.

## 4) Why does a log file contain old lines?

`--log-file` opens the target in append mode. If you reuse the same path, old entries remain.

Use a fresh file to compare runs:

```bash
rm -f /tmp/stackcomp-run.log
env -u WAYLAND_DISPLAY ./build/stackcomp --log-level debug --log-file /tmp/stackcomp-run.log
```

## 5) Quick sanity checks before deeper debugging

```bash
meson setup build --reconfigure
meson compile -C build
meson test -C build --print-errorlogs
```

Expected:

- build succeeds
- test summary reports all tests as `OK`

## 6) IPC quick-check commands

```bash
echo 'layout tile' | nc -U "$XDG_RUNTIME_DIR/stackcomp-ipc.sock"
echo 'workspace next' | nc -U "$XDG_RUNTIME_DIR/stackcomp-ipc.sock"
echo 'reload config' | nc -U "$XDG_RUNTIME_DIR/stackcomp-ipc.sock"
```

If these fail, verify:

- compositor instance is running
- `XDG_RUNTIME_DIR` is set
- IPC was not disabled with `--no-ipc`

## 7) Closing a terminal via foreign-toplevel protocol logs `slave exited with signal 1 (Hangup)`

This is usually expected for terminal applications (for example `foot`).

What happens:

- A client sends a regular close request through the Wayland foreign-toplevel protocol.
- The terminal then tears down its PTY/session.
- Child processes attached to that PTY can receive `SIGHUP` (signal 1), which appears as a hangup log line.

Interpretation:

- This is not a forced compositor kill path by itself.
- It is a common side effect of closing a terminal window with active child processes.

Investigate further only if you also see repeated compositor-side protocol errors or crashes.

## 8) Closing the compositor via X button logs `Unhandled X11 event: DestroyNotify (17)`

This is usually expected in nested X11 sessions during shutdown.

What happens:

- Clicking the window close button triggers X11 teardown for the compositor window.
- During shutdown, a `DestroyNotify` event can still arrive while backend event handling is winding down.
- wlroots logs this as a debug line: `Unhandled X11 event: DestroyNotify (17)`.

Interpretation:

- If the compositor exits cleanly and no follow-up errors/crashes appear, this is generally harmless.
- Treat it as suspicious only when it repeats during normal runtime or is followed by error/fatal logs.

## 9) Nested shutdown logs `(EE) failed to write to Xwayland fd: Broken pipe`

This can still appear during an otherwise clean nested shutdown and is currently treated as known shutdown noise.

Context:

- stackcomp exits cleanly after the host X11 window is closed.
- During teardown, Xwayland can still attempt one final write while its Wayland connection is already closing.
- This produces the `(EE) ... Broken pipe` line even though the compositor process already terminates correctly.

Interpretation:

- If stackcomp exits and no assertions/crashes follow, this message is expected and not treated as a functional failure.
- Investigate only if the message appears repeatedly during normal runtime or is followed by new fatal errors.

## 10) Nested shutdown logs `(EE) failed to read Wayland events: Broken pipe`

This can appear during a clean quit path in nested sessions and is currently treated as expected shutdown noise.

Context:

- A quit action triggers orderly compositor shutdown.
- While the Wayland side is already closing, Xwayland can still attempt a final event read.
- That race during teardown can emit `(EE) failed to read Wayland events: Broken pipe` even when exit status is successful.

Interpretation:

- If stackcomp exits with code 0 and no follow-up fatal signal/assertion appears, this line is considered harmless.
- Investigate further only if it occurs repeatedly during normal runtime or is followed by new crashes.

## 11) Why does yEd / some Java apps only show a close button?

Some Java applications under native Wayland stackcomp sessions (for example yEd) only expose a close button in the titlebar, while minimize/maximize are missing.

Observed behavior:

- the app starts normally under stackcomp
- the window closes correctly
- minimize/maximize buttons are not offered by the client titlebar

Cause:

- the client negotiates xdg-shell version 3
- stackcomp can only advertise `wm_capabilities` starting at xdg-shell version 5
- with version 3, the compositor cannot legally signal maximize/minimize support

Current status:

- this cannot be fixed cleanly in the existing client-side decoration path alone
- a real fix would require server-side decorations (SSD) with compositor-drawn window controls
- stackcomp does not currently implement SSD decoration

In short:

- close works because it is supported by xdg-shell v3
- minimize/maximize are unavailable because the protocol version is too old
- the proper long-term solution is SSD, not another xdg-shell workaround
