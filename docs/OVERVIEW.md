# Morph Overview

This document is the main entry point for **Morph**'s documentation. Use it to understand how the repository is organized, which document answers which question, and how the managed session lifecycle fits together.

## Table of Contents

- [Reading Guide](#reading-guide)
- [Repository Structure](#repository-structure)
- [Managed Session Overview](#managed-session-overview)
- [Wrapper Flow](#wrapper-flow)
- [Startup Flow](#startup-flow)
- [Reload Flow](#reload-flow)
- [Shutdown Flow](#shutdown-flow)
- [Standalone Binary Flow](#standalone-binary-flow)
- [Building Blocks](#building-blocks)
- [Typical Usage Modes](#typical-usage-modes)
- [Related Documents](#related-documents)

## Reading Guide

If you are new to the project, use this order:

1. Read [`README.md`](../README.md) for the short project overview and top-level entry points.
2. Read this file for the repository map and the high-level runtime flows.
3. Continue with the detail document that matches your current goal.

Choose the next document by question:

| Question | Start Here |
|---|---|
| How does a managed **Morph** session start and stop? | [`docs/LAUNCHER.md`](LAUNCHER.md) |
| Which environment variables affect runtime behavior? | [`docs/ENVIRONMENT.md`](ENVIRONMENT.md) |
| How does [`morph.conf`](../config/morph.conf) work? | [`docs/CONFIG.md`](CONFIG.md) |
| How do I build and test changes? | [`docs/TESTING.md`](TESTING.md) and [`docs/TESTS.md`](TESTS.md) |
| Which command-line flags and runtime control options exist? | [`docs/CLI.md`](CLI.md) |
| Which Wayland protocols are implemented or missing? | [`docs/PROTOCOLS.md`](PROTOCOLS.md) |
| How is the compositor structured internally? | [`docs/COMPOSITOR.md`](COMPOSITOR.md) |
| What is still planned or intentionally unfinished? | [`docs/BACKLOG.md`](BACKLOG.md) |

## Repository Structure

This section is intentionally more detailed than the root [`README.md`](../README.md). The goal is to show where the important moving parts live before the flow diagrams reference them.

# Current Repo Structure

| Path || Description |
|---|---|---|
|└─ [`assets/`](assets/) || Runtime assets like icons and other visual resources |
|└─ [`config/`](../config/) || Runtime defaults and managed session files for **Morph**. |
|    |└─ [`environment`](../config/environment) | Runtime environment defaults installed to `/etc/morph/environment` and used by [`scripts/morph-session`](../scripts/morph-session). |
|    |└─ [`morph.conf`](../config/morph.conf) | Runtime base config installed to `/etc/morph/morph.conf`. Can be overridden by copying it to `~/.config/morph/`. |
|    |└─ [`portals`](../config/portals) | Runtime portal startup script installed to `/etc/morph/`. Can be overridden by copying it to `~/.config/morph/`. |
|    |└─ [`reload.sh`](../config/reload.sh) | Runtime user reload hook template. See [`Reload Resolution`](LAUNCHER.md#reload-resolution) in `docs/LAUNCHER.md`. |
|    |└─ [`shutdown.sh`](../config/shutdown.sh) | Runtime user shutdown hook template. See [`Shutdown Hook Resolution`](LAUNCHER.md#shutdown-hook-resolution) in `docs/LAUNCHER.md`. |
|    |└─ [`startup.sh`](../config/startup.sh) | Runtime user startup hook template. See [`Startup Hook Resolution`](LAUNCHER.md#startup-hook-resolution) in `docs/LAUNCHER.md`. |
|└─ [`docs/`](.) || Project and developer documentation. |
|    |└─ [`OVERVIEW.md`](OVERVIEW.md) | Central reading guide, repository map, and managed session flow entry point. |
|    |└─ [`LAUNCHER.md`](LAUNCHER.md) | Shared wrapper behavior for [`morph-session_dbg`](../testing/morph-session_dbg) and [`morph-session`](../scripts/morph-session). |
|    |└─ [`ENVIRONMENT.md`](ENVIRONMENT.md) | Runtime environment variables, environment-layer behavior, and XKB notes. |
|    |└─ [`CLI.md`](CLI.md) | Binary command-line flags and IPC-aware runtime control behavior. |
|    |└─ [`CONFIG.md`](CONFIG.md) | Overview of **Morph** config format, sections, and behavior. |
|    |└─ [`COMPOSITOR.md`](COMPOSITOR.md) | Compositor architecture, features, and implementation notes. |
|    |└─ [`PROTOCOLS.md`](PROTOCOLS.md) | **Morph** Wayland and portal-related protocol support, gaps, and expectations. |
|    |└─ [`TESTING.md`](TESTING.md) | General workflow, build/test loop, smoke checks, and manual validation entry points. |
|    |└─ [`TESTS.md`](TESTS.md) | Automated test scope, test commands, and where to inspect failures. |
|    |└─ [`CRASHING.md`](CRASHING.md) | Crash handling, crash logs, and post-mortem debugging. |
|    |└─ [`DEVFAQ.md`](DEVFAQ.md) | Developer troubleshooting notes, common pitfalls, and workflow hints. |
|    |└─ [`BACKLOG.md`](BACKLOG.md) | Medium- and long-term engineering backlog and follow-up topics. |
|    |└─ [`morph.conf.example`](morph.conf.example) | Example configuration file with comments. |
|    |└─ [`roadmaps/`](roadmaps/) | Branch-specific and topic-specific roadmap documents. |
|    |   └─ [`Roadmap_install-and-user-setup.md`](roadmaps/Roadmap_install-and-user-setup.md) | English roadmap for the install and user setup rework branch. |
|    |   └─ [`Roadmap.de.md`](roadmaps/Roadmap.de.md) | German roadmap notes and follow-up tracking. |
| [`protocols/`](../protocols/) || Wayland protocol XML files for headers and generated sources. |
| [`scripts/`](../scripts/) || Helpers for startup, reload, shutdown, install, and session management. |
|    |└─ [`dev-install.sh`](../scripts/dev-install.sh) | Development install script for **Morph** runtime files and helper symlinks from [`testing/config/`](../testing/config/). |
|    |└─ [`local-build-test.sh`](../scripts/local-build-test.sh) | Local helper to run a build-and-test loop for quick validation while developing. |
|    |└─ [`morph-build.sh`](../scripts/morph-build.sh) | Unified build helper for runtime/debug variants (`--runtime`, `--debug`, `--both`). |
|    |└─ [`morph-install.sh`](../scripts/morph-install.sh) | Unified install helper for runtime/debug artifacts with optional dry-run mode. |
|    |└─ [`morph-session`](../scripts/morph-session) | Installed runtime **Morph** session wrapper. It resolves environment, config, logs, and session mode for real sessions. |
|    |└─ [`morph-uninstall.sh`](../scripts/morph-uninstall.sh) | Unified uninstall helper for runtime/debug artifacts with optional dry-run mode. |
|    |└─ [`shell-helpers.sh`](../scripts/shell-helpers.sh) | Shared shell helper library used throughout the managed session flow. |
|    |└─ [`system_reload.sh`](../scripts/system_reload.sh) | System reload script for managed config reloads. |
|    |└─ [`system_shutdown.sh`](../scripts/system_shutdown.sh) | System shutdown script for managed config cleanup. |
|    |└─ [`system_startup.sh`](../scripts/system_startup.sh) | System startup script for managed config startup. |
|    |└─ [`system-uninstall.sh`](../scripts/system-uninstall.sh) | Conservative uninstall helper for Meson-installed **Morph** artifacts. |
|    |└─ [`test-nested-smoke.sh`](../scripts/test-nested-smoke.sh) | Nested smoke-test helper for quick runtime checks in a development session. |
| [`sessions/`](../sessions/) || Desktop files for runtime and development sessions. |
|    |└─ [`morph.desktop`](../sessions/morph.desktop) | Runtime desktop file for login managers. |
|    |└─ [`morph_dbg.desktop`](../sessions/morph_dbg.desktop) | Development desktop file for local repo testing through a display manager. |
| [`src/`](../src/) || **Morph** compositor source code. |
| [`testing/`](../testing/) || Manual tests, launchers, and dev-only runtime files. See [`docs/TESTING.md`](TESTING.md). |
|    |└─ [`config/`](../testing/config/) | Development-only copies used by [`morph-session_dbg`](../testing/morph-session_dbg) and [`dev-install.sh`](../scripts/dev-install.sh) so the dev flow stays separate from release runtime files. |
|    |   └─ [`morph.conf`](../testing/config/morph.conf) | Development base config for [`morph-session_dbg`](../testing/morph-session_dbg) and `dev-install.sh`. |
|    |   └─ [`environment`](../testing/config/environment) | Development environment file used by [`morph-session_dbg`](../testing/morph-session_dbg). |
|    |   └─ [`portals`](../testing/config/portals) | Development managed portal file used by [`morph-session_dbg`](../testing/morph-session_dbg). |
|    |   └─ [`startup.sh`](../testing/config/startup.sh) | Development user startup hook template for dev installs. |
|    |   └─ [`reload.sh`](../testing/config/reload.sh) | Development user reload hook template for dev installs. |
|    |   └─ [`shutdown.sh`](../testing/config/shutdown.sh) | Development user shutdown hook template for dev installs. |
|    |└─ [`morph-session_dbg`](../testing/morph-session_dbg) | **Morph** main development startup script. See [`Wrapper Roles`](LAUNCHER.md#wrapper-roles) in `docs/LAUNCHER.md`. |
|    |└─ [`test-howto_start-variants.nfo`](../testing/test-howto_start-variants.nfo) | Practical native/nested startup variants and focused launcher checks. |
|    |└─ [`testplan-manual-morph.nfo`](../testing/testplan-manual-morph.nfo) | Broader manual lifecycle, install, uninstall, hook, and fallback test plan. |
| [`tests/`](../tests/) || Automation tests for the compositor and shell runtime behavior. See [`docs/TESTS.md`](TESTS.md). |

## Managed Session Overview

This is the ready-made **Morph** session path used by login managers or by users who want the full managed lifecycle.

```mermaid
flowchart TD
    A["Login manager"]
    A2["TTY / manual start"]
    B["/usr/share/wayland-sessions/morph.desktop [1]"]
    C["/usr/bin/morph-session [2]"]
    D["Resolve environment and config layers"]
    E["Export MORPH_* session variables"]
    F["Start morph binary"]
    G["Load morph.conf"]
    H["Run managed startup flow"]
    I["/etc/morph/system_startup.sh [3]"]
    J["~/.config/morph/startup.sh [4]"]
    K["Morph running"]
    L["Reload command"]
    M["/etc/morph/system_reload.sh [5]"]
    N["~/.config/morph/reload.sh [6]"]
    O["Shutdown / compositor exit"]
    P["~/.config/morph/shutdown.sh [7]"]
    Q["/etc/morph/system_shutdown.sh [8]"]
    R["Session cleanup complete"]

    X1["Note:
The wrapper resolves environment files,
config lookup order, and managed hook mode."]
    X2["Note:
System startup runs before the user startup hook."]
    X3["Note:
User shutdown runs before the managed system cleanup."]
    X4["Note:
Reload re-enters the managed flow
and then calls the user reload hook."]

    A --> B --> C
    A2 --> C
    C --> D --> E --> F --> G --> H
    H --> I --> J --> K
    K --> L --> M --> N --> K
    K --> O --> P --> Q --> R

    D -.-> X1
    I -.-> X2
    P -.-> X3
    M -.-> X4

    classDef wrapper fill:#dbeafe,stroke:#2563eb,stroke-width:1px,color:#111827;
    classDef userhook fill:#dcfce7,stroke:#16a34a,stroke-width:1px,color:#111827;
    classDef systemhook fill:#fed7aa,stroke:#ea580c,stroke-width:1px,color:#111827;
    classDef note fill:#fef3c7,stroke:#ca8a04,stroke-width:1px,color:#111827;

    class A,A2,B,C,D,E wrapper;
    class J,N,P userhook;
    class I,M,Q systemhook;
    class X1,X2,X3,X4 note;
```

<details>
<summary>Reference legend</summary>

| Ref | Details |
|---|---|
| `[1]` | Installed session entry. Source: [`sessions/morph.desktop`](../sessions/morph.desktop) |
| `[2]` | Wrapper script. Source: [`scripts/morph-session`](../scripts/morph-session) |
| `[3]` | Managed startup hook. Source: [`scripts/system_startup.sh`](../scripts/system_startup.sh) |
| `[4]` | User startup hook template. Dev source: [`testing/config/startup.sh`](../testing/config/startup.sh); runtime source: [`config/startup.sh`](../config/startup.sh) |
| `[5]` | Managed reload hook. Source: [`scripts/system_reload.sh`](../scripts/system_reload.sh) |
| `[6]` | User reload hook template. Dev source: [`testing/config/reload.sh`](../testing/config/reload.sh); runtime source: [`config/reload.sh`](../config/reload.sh) |
| `[7]` | User shutdown hook template. Dev source: [`testing/config/shutdown.sh`](../testing/config/shutdown.sh); runtime source: [`config/shutdown.sh`](../config/shutdown.sh) |
| `[8]` | Managed shutdown hook. Source: [`scripts/system_shutdown.sh`](../scripts/system_shutdown.sh) |

</details>

## Wrapper Flow

The wrapper is the ready-made session entry point. It turns a display-manager or shell launch into a predictable **Morph** session with layered environment loading, layered config lookup, and managed hook wiring.

```mermaid
flowchart TD
    A["Login manager"]
    A2["TTY / manual start"]
    B["morph.desktop"]
    C["morph-session"]
    D["Load environment layers"]
    E["Resolve config path"]
    F["Enable managed hook mode"]
    G["Export MORPH_* variables"]
    H["exec morph"]

    A --> B --> C
    A2 --> C
    C --> D --> E --> F --> G --> H
```

- The wrapper exists to keep session setup reproducible.
- Login managers enter through `morph.desktop`, whose `Exec=` starts `morph-session`.
- TTY and manual launches start `morph-session` directly and skip the desktop file.
- It is the best entry point when you want **Morph** to own the full session lifecycle.
- A custom launcher can replace parts of this flow, but then it must deliberately take over those responsibilities.

## Startup Flow

Startup is split into a managed system layer and an optional user layer. This keeps compositor-owned setup reliable while still leaving room for per-user services and applications.

```mermaid
flowchart TD
    A["morph binary starts"]
    B["Read startup hook path"]
    C["Resolve managed startup hook"]
    D["scripts/system_startup.sh"]
    E["Prepare nested/native session state"]
    F["Start managed portal/session pieces"]
    G["config/startup.sh"]
    H["Launch user services and applications"]
    I["Startup complete"]

    A --> B --> C --> D --> E --> F --> G --> H --> I
```

- [`system_startup.sh`](../scripts/system_startup.sh) owns the compositor-side startup preparation.
- [`config/startup.sh`](../config/startup.sh) stays user-facing and is the place for autostart services, panels, bars, terminals, and similar additions.
- Path-like hook values from the active config, for example `~/.config/morph/startup.sh` or `${MORPH_USER_CONFIG_DIR}/startup.sh`, are sourced as files in the managed flow so helper functions stay available.
- In managed mode, the system hook always runs before the user startup hook.

Read the detailed behavior in [`Startup Hook Resolution`](LAUNCHER.md#startup-hook-resolution) in `docs/LAUNCHER.md`.

## Reload Flow

Reload keeps the same layering model as startup: **Morph** first re-enters the managed reload hook, then hands control to the optional user reload hook.

```mermaid
flowchart TD
    A["Reload command"]
    B["Morph resolves reload hook"]
    C["scripts/system_reload.sh"]
    D["Run managed reload tasks"]
    E["config/reload.sh"]
    F["Run user reload tasks"]
    G["Return to running session"]

    A --> B --> C --> D --> E --> F --> G
```

- Before the hooks run, **Morph** re-sources the system and user `environment` files, so hooks and newly spawned components see current environment values. See [`Reload Behavior`](ENVIRONMENT.md#reload-behavior) in `docs/ENVIRONMENT.md` for what a reload can and cannot change.
- This flow is useful for restarting bars, launchers, notification daemons, or other session components without tearing down the full session.
- The helper layer can also provide guarded commands such as one-shot reload helpers.
- Path-like reload hook values are sourced as files in the managed flow so reload helpers remain available without re-sourcing the helper library manually.

Read the detailed behavior in [`Reload Resolution`](LAUNCHER.md#reload-resolution) in `docs/LAUNCHER.md`.

## Shutdown Flow

Shutdown intentionally inverts the startup order for the user-facing part: the user shutdown hook runs first, and the managed cleanup finishes last.

```mermaid
flowchart TD
    A["Morph exits / session ends"]
    B["Morph resolves shutdown hook"]
    C["config/shutdown.sh"]
    D["Run user shutdown tasks"]
    E["scripts/system_shutdown.sh"]
    F["Run managed cleanup"]
    G["Portals and tracked processes are stopped"]
    H["Session cleanup complete"]

    A --> B --> C --> D --> E --> F --> G --> H
```

- [`config/shutdown.sh`](../config/shutdown.sh) is the user-facing place for manual cleanup.
- [`system_shutdown.sh`](../scripts/system_shutdown.sh) owns the managed cleanup and should be the final step so the compositor can clean up the session state it created.
- Path-like shutdown hook values are sourced as files in the managed flow so user cleanup can log through the shared helper layer directly.

Read the detailed behavior in [`Shutdown Hook Resolution`](LAUNCHER.md#shutdown-hook-resolution) in `docs/LAUNCHER.md`.

## Standalone Binary Flow

**Morph** can also be started without [`morph-session`](../scripts/morph-session). In that case, the binary can still use user hooks, system hooks, or a mixed setup depending on how the config and environment are prepared.

```mermaid
flowchart TD
    A["Manual shell / custom launcher / direct exec"]
    B["Optional custom environment setup"]
    C["Optional custom config selection"]
    D["morph binary"]
    E["Load morph.conf"]
    F["Resolve startup / reload / shutdown hooks"]
    G["Optional managed system hooks"]
    H["Optional user hooks"]
    I["Morph running"]

    A --> B --> C --> D --> E --> F --> I
    F -.-> G
    F -.-> H
```

- This is the most flexible mode.
- It is also the mode where the caller must be explicit about which pieces of the managed session stack should still be reused.
- A custom wrapper can selectively replace [`morph-session`](../scripts/morph-session) while still keeping **Morph**'s system hooks.

## Building Blocks

The session architecture is intentionally a small set of reusable parts rather than one indivisible launcher.

```mermaid
flowchart LR
    A["Custom launcher"]
    B["morph-session"]
    C["Environment layer resolver"]
    D["Config resolver"]
    E["System startup / reload / shutdown"]
    F["User startup / reload / shutdown"]
    G["morph binary"]

    A --> C
    A --> D
    A --> G

    B --> C
    B --> D
    B --> G

    G --> E
    G --> F
```

- [`morph-session`](../scripts/morph-session) is a ready-made orchestrator, not a hard requirement.
- Environment resolution can stay in shell space where it remains flexible.
- Config parsing belongs to **Morph** because it is the compositor-facing interface.
- System hooks provide the managed lifecycle.
- User hooks remain the extension points for local customization.

## Typical Usage Modes

- Managed session: [`morph.desktop`](../sessions/morph.desktop) -> [`morph-session`](../scripts/morph-session) -> `Morph` -> `System hooks` -> `User hooks`
- Binary with managed hooks: direct **Morph** start, but the resolved hooks still point at the managed system hooks
- Binary with custom user hooks: direct **Morph** start with user-provided hook paths
- Custom wrapper: a user-defined launcher reuses only the building blocks it wants

## Related Documents

- [`docs/LAUNCHER.md`](LAUNCHER.md) for wrapper-owned behavior and runtime file handling
- [`docs/ENVIRONMENT.md`](ENVIRONMENT.md) for runtime environment variables and layering
- [`docs/CLI.md`](CLI.md) for binary command-line flags and IPC-aware control behavior
- [`docs/CONFIG.md`](CONFIG.md) for compositor config syntax, binds, and hook definitions
- [`docs/TESTING.md`](TESTING.md) for build, test, smoke, and manual verification workflows
- [`docs/BACKLOG.md`](BACKLOG.md) for remaining engineering follow-up
