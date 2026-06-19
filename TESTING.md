# Local Build/Test Workflow

This project uses a local-first workflow for configure, build, tests, and
nested smoke checks.

Primary command:

```bash
./scripts/local-build-test.sh build
```

The script runs these steps in order:

1. `meson setup build --reconfigure`
2. `meson compile -C build`
3. `meson test -C build --print-errorlogs`
4. `scripts/test-nested-smoke.sh build`

## Quick debug loop

```bash
meson compile -C build
meson test -C build --print-errorlogs
```

Use this faster loop while iterating on code that does not affect startup/
nested behavior.

## Typical failures and fixes

1. Dependency `wlroots-0.19` not found
   - Check: `pkg-config --modversion wlroots-0.19`
   - Fix: install matching development package or adjust `PKG_CONFIG_PATH`
2. `wayland-scanner` missing
   - Fix: install Wayland development tools and `wayland-protocols`
3. Nested smoke fails in headless environments
   - Ensure `xvfb-run` is installed, or run from an active Wayland/X11 session
4. Build links locally but runtime fails to start
   - Inspect logs in `build/meson-logs/` and state logs under
     `${XDG_STATE_HOME:-$HOME/.local/state}/stackcomp`
