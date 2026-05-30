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

## Recommended debug workflow

1. Enable core dumps for your shell/session:

```bash
ulimit -c unlimited
```

2. Run stackcomp with debug symbols and optional crash marker file:

```bash
./build/stackcomp --log-level debug --crash-log /tmp/stackcomp-crash.log
```

3. After a crash, inspect recent core dumps:

```bash
coredumpctl list stackcomp
```

4. Open in gdb and collect a full backtrace:

```bash
coredumpctl gdb stackcomp
# in gdb:
thread apply all bt full
```

## Why not symbolization in the signal handler?

Functions like `malloc`, `fprintf`, `backtrace_symbols`, `popen`, `system`, and many C++/X11 helpers are not async-signal-safe. Running them in a fatal signal handler can deadlock or crash recursively.

stackcomp therefore keeps the signal handler minimal and performs rich analysis outside the crashing context via core dumps.
