# Testing Guide

This document describes the overall testing workflow for Morph.

Use it when you want to:

- configure and build the project locally
- run the automated test suite
- execute the nested smoke check
- navigate to the manual runbooks for launcher and lifecycle validation

For the automated test inventory under `tests/`, see [`docs/TESTS.md`](TESTS.md).

## Scope

Testing in this repository currently has three layers:

1. Automated tests under `tests/`
2. Local build/test helpers under `scripts/`
3. Manual validation runbooks under `testing/`

The goal is to keep quick automated feedback separate from longer manual
session checks.

## Primary Local Workflow

The main local build-and-test command is:

```bash
./scripts/local-build-test.sh build
```

That helper currently runs these steps in order:

1. `meson setup build --reconfigure`
2. `meson compile -C build`
3. `meson test -C build --print-errorlogs`
4. `scripts/test-nested-smoke.sh build`

Use this when you want one command that covers the normal local verification
path after code changes.

## Quick Debug Loop

For faster iteration during development:

```bash
meson compile -C build
meson test -C build --print-errorlogs
```

Use this shorter loop when you are only changing code that does not require a
full startup or nested-session recheck.

## Automated Tests

The automated suite currently includes:

- config parsing and validation checks in `tests/test_config.c`
- shell/runtime integration checks in `tests/test_shell_runtime.sh`

Run them directly with:

```bash
meson test -C build --print-errorlogs
```

For a more verbose view while debugging:

```bash
meson test -C build --print-errorlogs -v
```

For the automated test scope and intent, see [`docs/TESTS.md`](TESTS.md).

## Nested Smoke Check

The local helper also includes a nested smoke run:

```bash
./scripts/test-nested-smoke.sh build
```

This is useful when you want a quick sanity check that the compositor still
starts in a nested environment after build changes.

## Manual Validation Runbooks

Manual session validation lives under `testing/`.

Use these files:

- [`docs/LAUNCHER.md`](../docs/LAUNCHER.md)
  For launcher behavior, environment layering, managed hooks, and runtime file handling.
- [`testing/test-howto_start-variants.nfo`](../testing/test-howto_start-variants.nfo)
  For practical native/nested startup variants and focused launcher checks.
- [`testing/testplan-manual-morph.nfo`](../testing/testplan-manual-morph.nfo)
  For the broader manual lifecycle, install, uninstall, hook, and fallback test plan.

These runbooks are the right place for end-to-end checks that need a real
session, a display manager, a nested compositor, or log inspection with `rg`.

## Logs and Troubleshooting

Useful locations while testing:

- Meson logs:
  - `build/meson-logs/`
- Runtime logs:
  - `${XDG_STATE_HOME:-$HOME/.local/state}/morph`

Common troubleshooting approach:

1. Rebuild with `meson compile -C build`
2. Re-run tests with `meson test -C build --print-errorlogs`
3. Inspect `build/meson-logs/testlog.txt`
4. Inspect runtime logs under `${XDG_STATE_HOME:-$HOME/.local/state}/morph`

## Typical Failure Patterns

1. Dependency `wlroots-0.20` not found
   - Check: `pkg-config --modversion wlroots-0.20`
   - Fix: install the matching development package or adjust `PKG_CONFIG_PATH`
2. `wayland-scanner` missing
   - Fix: install the Wayland development tools and `wayland-protocols`
3. Nested smoke fails in headless environments
   - Ensure `xvfb-run` is available, or run from an active X11/Wayland session
4. A renamed or moved repository path breaks existing build metadata
   - Remove the old `build/` directory and run `meson setup build` again

## Related Docs

- [`docs/CONFIG.md`](CONFIG.md)
- [`docs/CRASHING.md`](CRASHING.md)
- [`docs/DEVFAQ.md`](DEVFAQ.md)
