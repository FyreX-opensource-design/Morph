# Morph Environment Guide

This document describes runtime environment variables used by **Morph**s shell wrappers and passed to the compositor.

## Scope

Use this file for environment-based runtime setup.
Launcher runtime flow and files are documented in [`docs/LAUNCHER.md`](LAUNCHER.md).

## Resolution Order

Launcher settings are resolved in this order:

1. Caller environment (for example `VAR=value ./testing/morph-session_dbg` or `VAR=value morph-session`)
2. User environment file (`$XDG_CONFIG_HOME/morph/environment` or `~/.config/morph/environment`)
3. System environment file (`$MORPH_SYSTEM_CONFIG_DIR/environment`)
4. Wrapper defaults

If [`MORPH_ENV_FILE`](#morph-env-file) is set, it replaces the wrapper's system environment file path but keeps the same priority level.

Default system environment files differ by wrapper:

- [`testing/morph-session_dbg`](../testing/morph-session_dbg) uses the repository-local [`testing/config/environment`](../testing/config/environment)
- [`morph-session`](../scripts/morph-session) uses `/etc/morph/environment` by default; repo source: [`config/environment`](../config/environment)

<a id="launcher-variables"></a>
## Launcher Variables

<a id="morph-dbg"></a>
### MORPH_DBG

Controls how the launcher behaves:

- 0: normal mode
  - --log-level error
  - --no-crash-handler
- 1: normal mode with info logging
  - --log-level info
  - crash handler remains off
- 2: debug mode
  - --log-level debug
  - crash handler on with --crash-log

Invalid values fall back to the launcher default (currently 0) and emit a warning.

<a id="morph-config"></a>
### MORPH_CONFIG

Optional alternate config file path passed through `-c` or the environment variable `MORPH_CONFIG`.

- If readable: used directly
- If not readable: launcher exits with an error

<a id="morph-allow-builtin-fallback"></a>
### MORPH_ALLOW_BUILTIN_FALLBACK

Optional compatibility switch for config resolution.

- unset or `0`: missing config is a hard error
- `1` / `true` / `yes` / `on`: allow Morph to start without a config file and use the built-in defaults

The synthesized configuration currently contains only these emergency bindings:

- `Super+Return`: execute `${TERMINAL:-foot}`
- `Super+Shift+Q`: close the focused window
- `Super+Escape`: quit Morph
- `Super+T`: toggle between stack and tile layout; scroll switches back to stack

The fallback does not create hooks, portal configuration, tile or decoration
rules, input mappings, workspace bindings, or startup applications. Other
configuration defaults, such as stack layout, enabled layout animation,
server-side decoration policy, and SSD settings, are initialized independently
by the config loader and are also used for regular config files when those
sections are omitted.

This mirrors the binary CLI flag `--allow-builtin-fallback`.

<a id="morph-x11"></a>
### MORPH_X11

Controls xwayland-satellite startup in compositor:

- 0: disable satellite
- 1: enable satellite

Default: 1 for both nested and native sessions. Set `MORPH_X11=0` explicitly
to disable X11 application support.

<a id="morph-x11-display"></a>
### MORPH_X11_DISPLAY

Optional display number override for the satellite, for example `:12`.

<a id="morph-log-dir"></a>
### MORPH_LOG_DIR

Optional runtime log directory override.

Default if unset:

- $XDG_STATE_HOME/morph
- fallback: ~/.local/state/morph

Note:

- The **morph** suffix is a fixed project runtime namespace.
- It does not depend on launcher install path or binary name.

<a id="morph-env-file"></a>
### MORPH_ENV_FILE

Optional path to an environment override file.
If set and readable, it is sourced before launcher defaults are applied.

<a id="morph-user-config-dir"></a>
### MORPH_USER_CONFIG_DIR

Runtime path to Morph's user configuration directory.

- default: `${XDG_CONFIG_HOME:-$HOME/.config}/morph`
- exported by `morph-session` and `morph-session_dbg`

Use this in `[hooks]` when you want lifecycle hooks to follow the active user
configuration directory, for example `${MORPH_USER_CONFIG_DIR}/startup.sh`.
`MORPH_SYSTEM_CONFIG_DIR` is reserved for managed runtime files such as
`/etc/morph` in release sessions.


<a id="morph-portal-libexec-dir"></a>
### MORPH_PORTAL_LIBEXEC_DIR

Optional directory override for the xdg-desktop-portal executables used by the
managed portal startup file.

- default: auto-detect `/usr/libexec`, `/usr/lib`, `/usr/local/libexec`, then `/usr/local/lib`
- required executables in that directory: `xdg-desktop-portal`, `xdg-desktop-portal-wlr`, and `xdg-desktop-portal-gtk`
- exported by the managed portal resolver before `morph_start_portals()` runs

Set this only when the portal executables are installed in a distro- or
locally-specific directory that Morph cannot auto-detect.

<a id="ld-library-path"></a>
### LD_LIBRARY_PATH

Optional dynamic linker search path used by runtime loader resolution.

Use this only when Morph depends on locally installed shared libraries
(for example a locally built `wlroots-0.20`) that are not available through
system runtime library paths.

Recommended setup:

- uncomment the `LD_LIBRARY_PATH` block in `/etc/morph/environment`
- or uncomment the same block in `~/.config/morph/environment`

Runtime behavior in `morph-session`:

- when `LD_LIBRARY_PATH` is already set, the wrapper keeps it unchanged
- when an environment file assigns `LD_LIBRARY_PATH`, the wrapper exports it
  before starting Morph
- when `LD_LIBRARY_PATH` is empty and local library directories exist,
  the wrapper sets it automatically as a fallback and writes a warning
  to the startup log
- the warning points to the commented `LD_LIBRARY_PATH` block in the
  environment files above for explicit configuration

<a id="morph-debug-xdg"></a>
### MORPH_DEBUG_XDG

Optional verbose XDG lifecycle debug flag passed to **Morph**.

- unset or `0`: disabled
- non-empty and not `0`: enabled

Use this when debugging early xdg_toplevel requests/state transitions.

<a id="morph-debug-xdg-commits"></a>
### MORPH_DEBUG_XDG_COMMITS

Optional per-commit XDG trace. Requires `MORPH_DEBUG_XDG=1`.

- unset or `0`: disabled
- non-empty and not `0`: enabled

Keep this disabled unless you explicitly need every `xdg_surface` commit in the
log. Clients such as VS Code can commit often enough to overload live sessions
with synchronous log I/O.

<a id="morph-debug-pointer-focus"></a>
### MORPH_DEBUG_POINTER_FOCUS

Optional pointer enter/leave trace for diagnosing focus churn.

- unset or `0`: disabled
- non-empty and not `0`: enabled

Use this for panel hover, root focus, and pointer-focus handoff debugging. It
logs only focus transitions, not every pointer motion event.

<a id="morph-debug-layer-hit"></a>
### MORPH_DEBUG_LAYER_HIT

Optional layer-shell hitbox trace for diagnosing panel hover flicker.

- unset or `0`: disabled
- non-empty and not `0`: enabled

Use this together with `MORPH_DEBUG_POINTER_FOCUS` when a layer-shell panel loses
hover focus to an adjacent or maximized toplevel. Logs include layer surface
bounds, guarded bounds, exclusive zone, anchor flags, and current workarea.

<a id="morph-log-keys"></a>
### MORPH_LOG_KEYS

Optional key-event trace for diagnosing compositor keybind handling.

- unset or `0`: disabled
- non-empty and not `0`: enabled

Use this only while debugging keyboard grabs or keybind matching. It is not
needed for normal runtime logging.

<a id="morph-bridge-resize-hz"></a>
### MORPH_BRIDGE_RESIZE_HZ

Optional compatibility override for the interactive resize rate of legacy X11
clients running through xwayland-satellite.

- unset: use Morph's tested default of 17 Hz
- integer `1` through `240`: override the resize frequency in hertz
- invalid value: keep the default and emit an error in the Morph log

Most users should leave this variable unset. It exists for diagnosing or tuning
legacy toolkit clients that cannot keep up with unrestricted resize configure
events. It does not change resize behavior for native Wayland clients.

Tune this value by changing it in small integer steps and restarting the session
that sources the environment file. Lower values send fewer resize configures and
can reduce flicker or delayed redraws in slow legacy toolkits, but interactive
resize feels more stepped. Higher values feel more responsive, but can make old
GTK2/X11 bridge clients fall behind and repaint stale sizes. Fractional values
such as `12.5` are invalid; use whole hertz values only.

<a id="morph-managed-hooks"></a>
### MORPH_MANAGED_HOOKS

Internal wrapper flag for managed session dispatch.

- unset: the compositor runs configured hooks directly
- `1`: the compositor dispatches [`system_startup.sh`](../scripts/system_startup.sh), [`system_reload.sh`](../scripts/system_reload.sh), and [`system_shutdown.sh`](../scripts/system_shutdown.sh) first

This variable is exported by [`testing/morph-session_dbg`](../testing/morph-session_dbg) and [`morph-session`](../scripts/morph-session).
Normally users do not need to set it manually.

<a id="morph-layout"></a>
### MORPH_LAYOUT

Runtime helper variable exposed to shell predicates such as `when=`. See also [`MORPH_WORKSPACE`](#morph-workspace).

Possible values:

- `stack`
- `tile`
- `scroll`

Morph updates this variable before evaluating keybind predicates and managed hook helpers that depend on current layout state.

<a id="morph-workspace"></a>
### MORPH_WORKSPACE

Runtime helper variable exposed to shell predicates such as `when=`. See also [`MORPH_LAYOUT`](#morph-layout).

Possible values:

- decimal workspace number `1` through `9`

Morph updates this variable before evaluating keybind predicates and managed hook helpers that depend on current workspace state.

<a id="java-awt-wm-nonreparenting"></a>
### _JAVA_AWT_WM_NONREPARENTING

Optional Java/X11 compatibility flag for some legacy or toolkit-specific clients.

- unset: Java app keeps its default window-manager assumptions
- `1`: tells AWT/Swing to expect a non-reparenting window manager

This is often useful for X11 applications started through Xwayland, for example ATLauncher.

<a id="xkb-variables"></a>
## XKB Variables

These variables are read by **Morph** keyboard initialization via getenv().
The development launcher does not parse them; it only sources [`testing/config/environment`](../testing/config/environment).

- XKB_DEFAULT_LAYOUT
- XKB_DEFAULT_MODEL
- XKB_DEFAULT_VARIANT
- XKB_DEFAULT_OPTIONS

Behavior:

- unset or empty values let libxkbcommon use its system defaults
- set values are forwarded to xkb_rule_names and used for keymap compile

Example:

```bash
XKB_DEFAULT_LAYOUT=de,us \
XKB_DEFAULT_OPTIONS=grp:alt_shift_toggle \
./testing/morph-session_dbg
```

The same variables also work through the installed runtime wrapper:

```bash
XKB_DEFAULT_LAYOUT=de,us morph-session
```

## Practical Keyboard Test

sfwbar is not a good choice for keyboard layout validation.
Use a text editor such as Geany, Mousepad, or another editor inside the compositor session.

Suggested check:

1. Start with the XKB variables set.
2. Open Geany, Mousepad, or another editor and type text.
3. Switch layout, for example with `Alt+Shift` when it is configured.
4. Verify that character output changes as expected.

## Related Docs

- [`docs/LAUNCHER.md`](LAUNCHER.md) for launcher flow and runtime files
- [`testing/test-howto_start-variants.nfo`](../testing/test-howto_start-variants.nfo) for test runbook and triage commands
