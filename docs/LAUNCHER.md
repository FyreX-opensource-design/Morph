# Morph Launcher Guide

This document describes the runtime launcher behavior shared by **Morph**'s two shell wrappers:

- the development launcher [`testing/morph-session_dbg`](../testing/morph-session_dbg)
- the installed runtime wrapper [`scripts/morph-session`](../scripts/morph-session)

Use this guide for:
- development versus runtime wrapper behavior
- environment and config resolution order
- runtime log location and shutdown list behavior
- managed portal, startup, reload, and shutdown hook resolution

## Scope

The compositor INI syntax is documented in [`docs/CONFIG.md`](CONFIG.md).
This file covers wrapper runtime flow and wrapper-owned files only.

For all environment variables and examples, including XKB settings, see:

- [`docs/ENVIRONMENT.md`](ENVIRONMENT.md)

## Wrapper Roles

[`testing/morph-session_dbg`](../testing/morph-session_dbg) is the repo-local development wrapper.
It is meant for nested testing, local builds, fast validation, and launcher experiments.

[`scripts/morph-session`](../scripts/morph-session) is the installed runtime wrapper.
It is meant for display-manager sessions and normal runtime startup outside the repository.

Both wrappers keep the same managed hook model around **Morph**:

- they resolve environment layers before **Morph** starts
- they resolve the active config path before **Morph** starts
- they export [`MORPH_MANAGED_HOOKS=1`](ENVIRONMENT.md#morph-managed-hooks)
- they let the compositor dispatch [`system_startup.sh`](../scripts/system_startup.sh), [`system_reload.sh`](../scripts/system_reload.sh), and [`system_shutdown.sh`](../scripts/system_shutdown.sh)
- they keep user hooks as the user-facing extension points

**See [`Wrapper Flow`](OVERVIEW.md#wrapper-flow) and [`Managed Session Overview`](OVERVIEW.md#managed-session-overview) in `docs/OVERVIEW.md` for flow details.**

## Wrapper Differences

### Development launcher: [`testing/morph-session_dbg`](../testing/morph-session_dbg)

- uses repository-local debug defaults such as `./build_dbg/morph`
- prefers repository files under `config/` and `testing/`
- is covered by the shell-runtime tests only when they are run explicitly with `--dbg`
- is the best starting point for local nested/native smoke checks during development

### Runtime wrapper: [`scripts/morph-session`](../scripts/morph-session)

- uses installed defaults such as `morph` in `PATH`
- is the wrapper used by the standard automated shell-runtime tests
- prefers installed files under `/etc/morph`
- is the wrapper referenced by [`sessions/morph.desktop`](../sessions/morph.desktop)
- is the runtime entry point for greetd, SDDM, LightDM, and similar session managers

## Runtime Files

Typical files under **Morph**s state/log directory:

- `morph.log`
- `morph-crash.log`
- `morph-startup.log`
- `morph-shutdown.log`
- `morph-nested-startup.log`
- `morph-nested-shutdown.log`
- `shutdown_list.nfo`

Default location:

- `$XDG_STATE_HOME/morph`
- fallback: `~/.local/state/morph`

Both wrappers can override the log directory through [`MORPH_LOG_DIR`](ENVIRONMENT.md#morph-log-dir).

## Environment Resolution

Both wrappers follow the same high-level layering model:

1. Caller environment

   Example: `MORPH_DBG=2 ./testing/morph-session_dbg`

   Example: `MORPH_CONFIG=/tmp/debug.conf morph-session`

2. System environment file

   Example source path in development: [`testing/config/environment`](../testing/config/environment)

   Example installed path: `/etc/morph/environment`

3. User environment file

   Example path: `~/.config/morph/environment`

   Example use case: per-user `XKB_DEFAULT_LAYOUT=de,us`

4. Wrapper defaults

   Example: default log directory under `$XDG_STATE_HOME/morph`

   Example: [`MORPH_X11`](ENVIRONMENT.md#morph-x11) defaults to `1`

5. Caller environment restored as highest priority

Common behavior:

- caller-provided variables win over sourced files
- user environment can override system environment
- wrapper defaults only fill values that remain unset
- the wrapper exports [`MORPH_MANAGED_HOOKS=1`](ENVIRONMENT.md#morph-managed-hooks) before launching **Morph**

Default environment files differ by wrapper:

- [`testing/morph-session_dbg`](../testing/morph-session_dbg) uses the repository-local [`testing/config/environment`](../testing/config/environment)
- [`scripts/morph-session`](../scripts/morph-session) uses `/etc/morph/environment` by default; repo source: [`config/environment`](../config/environment)
- both wrappers also read the user override `~/.config/morph/environment` when it exists

**See [`Wrapper Flow`](OVERVIEW.md#wrapper-flow) in `docs/OVERVIEW.md` for flow details.**

## Config Resolution

Both wrappers resolve the active config before **Morph** starts.

Shared priority:

1. [`MORPH_CONFIG`](ENVIRONMENT.md#morph-config) when set to a readable path
2. user config fallback
3. system config fallback
4. optional builtin fallback when [`MORPH_ALLOW_BUILTIN_FALLBACK=1`](ENVIRONMENT.md#morph-allow-builtin-fallback)

Default config paths differ by wrapper:

- [`testing/morph-session_dbg`](../testing/morph-session_dbg) uses the repository-local config as its system-side default; repo source: [`testing/config/morph.conf`](../testing/config/morph.conf)
- [`scripts/morph-session`](../scripts/morph-session) uses `/etc/morph/morph.conf` as its system-side default; repo source: [`config/morph.conf`](../config/morph.conf)
- both wrappers still prefer `~/.config/morph/morph.conf` as the user fallback

After config resolution, both wrappers export the resolved user hook commands so the managed system hooks can still honor them during fallback cleanup.

**See [`Wrapper Flow`](OVERVIEW.md#wrapper-flow) in `docs/OVERVIEW.md` for flow details.**

## Portal Resolution

Portal startup is managed separately from user autostarts.

Resolution order in the managed flow:

1. The wrapper exports [`MORPH_MANAGED_HOOKS=1`](ENVIRONMENT.md#morph-managed-hooks)
2. The compositor dispatches [`scripts/system_startup.sh`](../scripts/system_startup.sh)
3. Managed base file: [`testing/config/portals`](../testing/config/portals) in development, installed portal file in runtime setups
4. Optional user override: `~/.config/morph/portals`

Behavior:

- The config keeps the user hook paths. The managed runtime reads those commands from the active config and passes them through environment variables.
- [`scripts/system_startup.sh`](../scripts/system_startup.sh) loads startup helpers, prepares nested/native runtime state, and then runs the configured user startup hook or its XDG fallback.
- Native sessions load the managed base portal setup before any session components are started.
- The managed portal resolver sets `MORPH_PORTAL_LIBEXEC_DIR` before `morph_start_portals()` runs, so the same portal file works on `/usr/libexec` and `/usr/lib` based distributions.
- If the user override exists, it is sourced after the managed base file and may replace [`morph_start_portals()`](../testing/config/portals) in development or the installed runtime portal function.
- Nested sessions skip portal startup entirely.
- Users do not need a portal file unless they intentionally want to override the managed default behavior.

**See [`Startup Flow`](OVERVIEW.md#startup-flow) and [`Managed Session Overview`](OVERVIEW.md#managed-session-overview) in `docs/OVERVIEW.md` for flow details.**

## Startup Hook Resolution

The user startup hook is where session components such as bars, terminals, applets, and optional services should start.

Behavior in the managed flow:

- With [`MORPH_MANAGED_HOOKS=1`](ENVIRONMENT.md#morph-managed-hooks), the compositor dispatches [`scripts/system_startup.sh`](../scripts/system_startup.sh) instead of running the configured startup hook directly.
- [`scripts/system_startup.sh`](../scripts/system_startup.sh) prepares nested/native runtime state, starts managed portal setup when needed, and then runs the configured user startup hook or its XDG fallback.
- Path-like startup hook values such as `~/.config/morph/startup.sh` or `${MORPH_USER_CONFIG_DIR}/startup.sh` are sourced as hook files so managed helpers such as `launch`, `launch_nested`, and `launch_nokill` stay available inside the hook. In development, [`testing/config/startup.sh`](../testing/config/startup.sh) is the user entry point exposed by `dev-install.sh`; in runtime installs, the installed template comes from [`config/startup.sh`](../config/startup.sh). Managed portal setup stays separate in [`testing/config/portals`](../testing/config/portals) for development and [`config/portals`](../config/portals) for runtime packaging.
- When the configured hook path is not readable — the normal state on a fresh install, since the shipped `morph.conf` points at `${MORPH_USER_CONFIG_DIR}/startup.sh` before that directory exists — the packaged hook in the system config directory (`/etc/morph/startup.sh`) is sourced instead. This mirrors the user → system resolution already used for `morph.conf`, so a default install runs its shipped session components. Only when neither file is readable is the hook skipped, and the log then names both paths tried. A configured path that does not exist is never downgraded to `sh -c <path>`.
- Services started through `launch <cmd ...>` in the user startup hook are tracked for shutdown cleanup in [`shutdown_list.nfo`](#launcher-managed-shutdown-process-list-file-do-not-edit-this-file).
- `launch_nokill <cmd ...>` is available for helper processes that should not be registered for shutdown tracking.
- Nested sessions still run the user startup hook, but skip the native-only managed portal startup path.

**See [`Startup Flow`](OVERVIEW.md#startup-flow) in `docs/OVERVIEW.md` for flow details.**

## Reload Resolution

Reload uses the compositor IPC entry point `reload config` / `reload`.

Behavior in the managed flow:

- Before it reloads `morph.conf`, the compositor re-sources the system and user `environment` files so environment edits do not need a session restart. Variables that the launcher consumes before starting the compositor still do; see [`Reload Behavior`](ENVIRONMENT.md#reload-behavior) in `docs/ENVIRONMENT.md`.
- With [`MORPH_MANAGED_HOOKS=1`](ENVIRONMENT.md#morph-managed-hooks), the compositor dispatches [`scripts/system_reload.sh`](../scripts/system_reload.sh) instead of running the configured reload hook directly.
- [`scripts/system_reload.sh`](../scripts/system_reload.sh) exposes the same helper library as startup/shutdown and then runs the configured user reload hook or its XDG fallback.
- Path-like reload hook values such as `~/.config/morph/reload.sh` or `${MORPH_USER_CONFIG_DIR}/reload.sh` are sourced as hook files so reload helpers remain available in-process.
- As with startup, an unreadable configured path falls back to the packaged `/etc/morph/reload.sh` before the hook is skipped.
- User reload hooks can restart managed session components in-place with `reload <cmd ...>`, for example `reload sfwbar`.
- User reload hooks can also start optional components once with `reload_once <cmd ...>`, for example `reload_once mutagen`.
- The `reload` helper stops the currently running process by basename, starts the replacement command, and keeps the shutdown tracker free of duplicate entries.

**See [`Reload Flow`](OVERVIEW.md#reload-flow) in `docs/OVERVIEW.md` for flow details.**

## Shutdown Hook Resolution

The user shutdown hook is the place for manual cleanup that should happen before
**Morph** performs its managed session teardown.

Behavior in the managed flow:

- With [`MORPH_MANAGED_HOOKS=1`](ENVIRONMENT.md#morph-managed-hooks), the compositor dispatches [`scripts/system_shutdown.sh`](../scripts/system_shutdown.sh) instead of running the configured shutdown hook directly.
- [`scripts/system_shutdown.sh`](../scripts/system_shutdown.sh) first runs the configured user shutdown hook or its XDG fallback.
- Path-like shutdown hook values such as `~/.config/morph/shutdown.sh` or `${MORPH_USER_CONFIG_DIR}/shutdown.sh` are sourced as hook files so user cleanup can share the managed logging helpers directly. In development, [`testing/config/shutdown.sh`](../testing/config/shutdown.sh) is the user entry point exposed by `dev-install.sh`; in runtime installs, the installed template comes from [`config/shutdown.sh`](../config/shutdown.sh).
- As with startup, an unreadable configured path falls back to the packaged `/etc/morph/shutdown.sh` before the hook is skipped.
- After the user shutdown hook returns, [`scripts/system_shutdown.sh`](../scripts/system_shutdown.sh) performs the managed cleanup, including tracked process termination from [`shutdown_list.nfo`](#launcher-managed-shutdown-process-list-file-do-not-edit-this-file).
- The shutdown hook runs synchronously: **Morph** waits for the user shutdown hook to finish before the managed cleanup continues.
- Nested sessions still follow the same hook resolution path, but the managed cleanup remains scoped to the nested session state.

**See [`Shutdown Flow`](OVERVIEW.md#shutdown-flow) in `docs/OVERVIEW.md` for flow details.**

## Launcher-managed shutdown process list file (DO NOT EDIT THIS FILE!)

- [`shutdown_list.nfo`](#launcher-managed-shutdown-process-list-file-do-not-edit-this-file)

Path:

- `$XDG_STATE_HOME/morph/shutdown_list.nfo`
- fallback: `~/.local/state/morph/shutdown_list.nfo`

This file is runtime state, not user configuration.
Do not edit it manually.

### Short lifecycle

1. At wrapper start, the file is recreated or cleared.
2. Each service started via `launch` in the active user startup hook, for example [`testing/config/startup.sh`](../testing/config/startup.sh) in development or [`config/startup.sh`](../config/startup.sh) in runtime installs, is appended to this file.
3. On abnormal compositor exit, the wrapper runs [`scripts/system_shutdown.sh`](../scripts/system_shutdown.sh) directly.
4. [`scripts/system_shutdown.sh`](../scripts/system_shutdown.sh) first runs the configured user shutdown hook or its XDG fallback.
5. [`scripts/system_shutdown.sh`](../scripts/system_shutdown.sh) then reads this list and terminates the registered services with `TERM`, then `KILL` if needed.
6. Manual edits can desync this list and lead to unclean shutdown with leftover or wrongly terminated processes.

**See [`Shutdown Flow`](OVERVIEW.md#shutdown-flow) in `docs/OVERVIEW.md` for flow details.**

## Typical Runs

Development wrapper:

```sh
./testing/morph-session_dbg
MORPH_DBG=1 ./testing/morph-session_dbg
MORPH_DBG=2 ./testing/morph-session_dbg
MORPH_CONFIG=/path/to/other.conf ./testing/morph-session_dbg
MORPH_X11=0 ./testing/morph-session_dbg
MORPH_X11_DISPLAY=:12 ./testing/morph-session_dbg
MORPH_LOG_DIR=/tmp/morph-test ./testing/morph-session_dbg
MORPH_ALLOW_BUILTIN_FALLBACK=1 ./testing/morph-session_dbg
```

Runtime wrapper:

```sh
morph-session
MORPH_DBG=1 morph-session
MORPH_CONFIG=/etc/morph/morph.conf morph-session
MORPH_LOG_DIR=/tmp/morph-session-test morph-session
MORPH_ALLOW_BUILTIN_FALLBACK=1 morph-session
```

**See [`Managed Session Overview`](OVERVIEW.md#managed-session-overview) and [`Standalone Binary Flow`](OVERVIEW.md#standalone-binary-flow) in `docs/OVERVIEW.md` for flow details.**

## Related Docs

- [`docs/CONFIG.md`](CONFIG.md) for compositor INI syntax
- [`README.md`](../README.md) for project overview and entry points
- [`docs/ENVIRONMENT.md`](ENVIRONMENT.md) for runtime environment variables and XKB settings
- [`testing/test-howto_start-variants.nfo`](../testing/test-howto_start-variants.nfo) for test-oriented command sequences
- [`docs/OVERVIEW.md`](OVERVIEW.md) for the repository map and managed session flow diagrams
