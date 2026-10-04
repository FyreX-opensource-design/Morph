# Shutdown issue: xwayland-satellite causes exit 139

## Summary

When Morph is run in nested X11 mode with `xwayland-satellite` enabled, a clean
`quit` request does not currently result in a clean process exit. The issue is
reproducible on the T580 test system and occurs after Morph begins normal
shutdown.

With `MORPH_X11=0`, Morph exits cleanly. This isolates the failure to the
X11/satellite shutdown path rather than the general Morph shutdown path or the
focus-cycling implementation.

## Reproduction

Run the nested session repeatedly with a separate log directory for each run:

```bash
for i in $(seq 1 5); do
    echo "Run $i"
    RUST_BACKTRACE=full \
    MORPH_DBG=1 \
    MORPH_LOG_DIR="/tmp/morph-xwayland-$i" \
    ./testing/morph-session_dbg
done
```

Exit Morph through the normal `quit` keybind. The current test result was
five failures out of five runs.

Control run without the satellite bridge:

```bash
MORPH_X11=0 \
MORPH_DBG=1 \
MORPH_LOG_DIR=/tmp/morph-no-x11 \
./testing/morph-session_dbg
```

The control run exits cleanly.

## Observed log sequence

The relevant part of the failing run is:

```text
Quit keybind pressed, terminating display loop
...
Io error: Broken pipe (os error 32)

thread 'main' panicked at /usr/src/packages/BUILD/src/server/mod.rs:799:79:
called `Option::unwrap()` on a `None` value
stack backtrace:
note: Some details are omitted, run with `RUST_BACKTRACE=full` for a verbose backtrace.
Segmentation fault (core dumped)
...
Compositor exited with rc=139
```

The panic originates in `xwayland-satellite`, not in Morph's C source. The
`Broken pipe` follows the Wayland server teardown and is therefore consistent
with the satellite losing its compositor connection while it is still running.

The wrapper's `[error-scan]` output also reports long EGL/GLES2 extension lists
as errors. Those are `INFO` messages and should not be treated as failures.
The actual failure indicators are the `Broken pipe`, the satellite panic, and
exit code `139`.

## Current shutdown flow

Morph starts the satellite as a child process from
[`src/main.c`](src/main.c) in `spawn_xwayland_satellite()`:

```c
pid_t pid = fork();
...
execlp("xwayland-satellite", "xwayland-satellite", disp, NULL);
```

The PID is only logged. It is not stored in `struct comp_server`, and Morph
does not retain a shutdown handle for the child.

The corresponding teardown helper is currently a no-op:

```c
static void server_destroy_xwayland(struct comp_server *server)
{
    (void)server;
}
```

After `wl_display_run()` returns, the main shutdown path is effectively:

```c
server_destroy_xwayland(&server);
server_finish(&server);
wl_display_destroy_clients(server.wl_display);
wl_display_destroy(server.wl_display);
```

There is no explicit satellite shutdown request, bounded wait, forced cleanup,
or `waitpid()` for the satellite child. Destroying the Wayland display closes
the satellite's connection while the satellite is still processing protocol
events. The satellite then panics on the disconnect instead of treating it as a
normal shutdown condition.

## Desired shutdown flow

Morph should own the complete lifecycle of the child it starts:

1. Request termination of the compositor event loop.
2. Stop accepting new compositor work and run the normal Morph shutdown hooks.
3. Request satellite termination before destroying the Wayland display.
4. Wait for the satellite with `waitpid(..., WNOHANG)` for a bounded period.
5. If it does not exit, send `SIGKILL` and reap it.
6. Destroy Wayland clients and the display only after the satellite has exited,
   or after the forced cleanup path has completed.
7. Make the whole helper idempotent so backend teardown, startup failures, and
   normal quit cannot perform the sequence twice.

A ten-second timeout may be used as the outer bound, but the implementation
should poll with `WNOHANG` rather than block the event loop in one uninterruptible
`waitpid()` call. A shorter normal grace period followed by a final timeout may
also be appropriate once the runtime policy is defined.

## Morph-side improvements

- Add a `pid_t xwayland_satellite_pid` field to `struct comp_server`.
- Store the PID returned by `fork()` and reset it if `exec()` fails.
- Replace the no-op `server_destroy_xwayland()` with an idempotent shutdown
  helper.
- Send `SIGTERM` to the tracked child before
  `wl_display_destroy_clients()` closes its Wayland connection.
- Poll `waitpid(pid, &status, WNOHANG)` until the child exits or the timeout is
  reached.
- Send `SIGKILL` on timeout and always reap the child to avoid a zombie.
- Handle an already exited child and `ESRCH` without treating them as a new
  failure.
- Avoid waiting for a child that was never started or whose `exec()` failed.
- Log the satellite PID, termination signal, wait result, timeout, and final
  child status so shutdown failures are diagnosable.
- Keep the normal exit status independent from a satellite failure when Morph
  itself completed its shutdown successfully, while still reporting a bridge
  failure clearly.
- Tighten the wrapper error scan so only actual `WARN`/`ERROR`/`fatal` markers
  are reported, not arbitrary `INFO` lines containing renderer text.

## Satellite-side expectation

Morph should shut down the child in an orderly way, but a Wayland server
disconnect is still an expected lifecycle event from the client's perspective.
`xwayland-satellite` should therefore handle `EPIPE`/connection loss without
calling `unwrap()` on missing protocol state. The upstream panic at
`src/server/mod.rs:799` should be reported or checked against a newer revision.

The two fixes are complementary: Morph must avoid tearing down a live child
without coordination, and the satellite must not crash if the connection is
closed unexpectedly.

## Verification matrix

| Scenario | Expected result |
|---|---|
| Nested Morph, `MORPH_X11=0`, normal `quit` | Exit code `0`, no crash marker |
| Nested Morph, satellite enabled, normal `quit` | Exit code `0`, satellite reaped, no `rc=139` |
| Satellite exits before Morph | Morph logs the already-exited child and continues cleanly |
| Satellite ignores `SIGTERM` | Morph waits up to the configured bound, sends `SIGKILL`, reaps it, and exits cleanly |
| Host/nested backend disappears | Morph does not double-run teardown or dereference stale satellite state |
| Repeated start/quit cycles | No orphaned satellite processes or zombies remain |

Useful checks after each run:

```bash
pgrep -a -f xwayland-satellite || true
ps -eo pid,ppid,state,cmd | rg 'xwayland-satellite|<defunct>' || true
rg -n "Broken pipe|panicked|fatal signal|rc=139|exited cleanly" \
  /tmp/morph-xwayland-*/morph-nested-startup.log
```

## Classification

This is a shutdown/lifecycle integration issue. It is independent of the
focus-cycling feature, but it currently prevents the nested X11 runtime test
from being considered clean when `xwayland-satellite` is enabled.
