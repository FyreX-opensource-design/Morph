# Crash Handling in stackcomp

stackcomp uses a minimal crash handler that is safe in signal context.

## Design goals

- Keep the in-signal path async-signal-safe.
- Emit a short crash marker quickly.
- Rely on system core dumps for full post-mortem debugging.

The handler catches fatal signals (`SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGTRAP`) and writes a short marker to stderr and optionally to a configured file.

## Runtime options

- `--crash-log /path/to/file.log`: append crash markers to a file.
- `--no-crash-handler`: disable stackcomp crash handler installation.
- `--crash-test`: trigger a deliberate `SIGSEGV` after startup to verify the crash handler path.

## Recommended debug workflow

1. Enable core dumps for your shell/session:

```bash
ulimit -c unlimited
ulimit -c
```

Expected output of `ulimit -c`: `unlimited`.

Note: `ulimit` is shell-local. If you start stackcomp from another shell/terminal, set it there as well.

2. Run stackcomp with debug symbols and optional crash marker file:

```bash
./build/stackcomp --log-level debug --crash-log /tmp/stackcomp-crash.log

# Optional: deterministic crash-handler test (intentional crash)
./build/stackcomp --log-level debug --crash-log /tmp/stackcomp-crash.log --crash-test
```

3. After a crash, inspect recent core dumps:

```bash
coredumpctl list stackcomp
```

If this still prints `No coredumps found`:

- verify again in the same shell: `ulimit -c` (must be `unlimited` or > 0)
- use `coredumpctl -q list stackcomp` to hide unrelated permission hints
- confirm systemd-coredump is active on the host (`systemctl is-active systemd-coredump.socket`)

4. Open in gdb and collect a full backtrace:

```bash
$ coredumpctl gdb stackcomp
# in gdb:
(gdb) bt full
```

If you don't know in which thread the crash happens you can use `thread apply all bt full` (`bt full` shows only the current thread).

5. Find the trigger location (not only the signal handler frame)
```bash
#0  0x00007f9e44b50007 in kill () at /lib/x86_64-linux-gnu/libc.so.6
#1  0x0000564d106ac60e in crash_signal_handler (signo=11, info=0x564d281e0bb0, ucontext=0x564d281e0a80)
    at ../src/crash_handler.c:125
        sa = {__sigaction_handler = {sa_handler = 0x0, sa_sigaction = 0x0}, sa_mask = {__val = {0 <repeats 16 times>}}, sa_flags = 0, sa_restorer = 0x0}
        unmask = {__val = {1024, 0 <repeats 15 times>}}
#2  0x00007f9e44b4fdf0 in <signal handler called> () at /lib/x86_64-linux-gnu/libc.so.6
#3  0x00007f9e44ba495c in ??? () at /lib/x86_64-linux-gnu/libc.so.6
#4  0x00007f9e44b4fcc2 in raise () at /lib/x86_64-linux-gnu/libc.so.6
#5  0x0000564d106bab37 in main (argc=6, argv=0x7fff2d601008) at ../src/main.c:5327
```

Interpretation:

- `#1` (`crash_signal_handler`) is expected after a fatal signal and is not the original trigger site.
- `#4` (`raise`) performs signal delivery.
- `#5` is the relevant call site in stackcomp source where the signal was triggered.

To get the code around line 5327 where the crash happens we have to switch to frame #5:
```bash
(gdb) frame 5
#5  0x0000564d106bab37 in main (argc=6, argv=0x7fff2d601008) at ../src/main.c:5327
5327			raise(SIGSEGV);
```

Now let's show the code:
```bash
# show code
(gdb) list
5322		comp_config_run_startup(server.config);
5323		if (crash_test_from_argv)
5324		{
5325			/* Deterministic test hook for validating crash-handler marker output. */
5326			wlr_log(WLR_ERROR, "--crash-test requested: triggering SIGSEGV for crash-handler test");
5327			raise(SIGSEGV);
5328		}
5329	
5330		wl_display_run(server.wl_display);
5331	
```

Optional helpers for real-world crashes:

```bash
# show arguments/local state in the selected frame
(gdb) info args
(gdb) info locals
```

## Why not symbolization in the signal handler?

Functions like `malloc`, `fprintf`, `backtrace_symbols`, `popen`, `system`, and many C++/X11 helpers are not async-signal-safe. Running them in a fatal signal handler can deadlock or crash recursively.

stackcomp therefore keeps the signal handler minimal and performs rich analysis outside the crashing context via core dumps.
