# Morph Backlog

This file tracks follow-up work that is still open after the current implementation state. Each block captures the current state first, then the remaining work. Completed sub-parts stay listed as `[x]` so the backlog also documents what is already in place.

## Table of Contents

- [Server-side decorations for Java and native Wayland windows](#server-side-decorations-for-java-and-native-wayland-windows)
- [Workspace architecture and scalability](#workspace-architecture-and-scalability)
- [Layout modes and tiling controller follow-up](#layout-modes-and-tiling-controller-follow-up)
- [Compositor-owned UI foundation](#compositor-owned-ui-foundation)
- [Keybind config and shell-conditional follow-up](#keybind-config-and-shell-conditional-follow-up)
- [Configurable focus policy](#configurable-focus-policy)
- [Compositor input test coverage](#compositor-input-test-coverage)
- [Plugin system and external control surface](#plugin-system-and-external-control-surface)
- [Theming and desktop integration follow-up](#theming-and-desktop-integration-follow-up)

# ----------------------------------------------------------------------------
# Server-side decorations for Java and native Wayland windows
# ----------------------------------------------------------------------------

## Current State

Morph already implements `xdg-decoration` policy selection and can switch clients between client-side and server-side decoration mode depending on layout, float state, and config rules. That is enough to hide client title bars in tile and scroll layouts, but Morph still does not draw its own SSD chrome or buttons.

Relevant implementation points:

- [`src/main.c:1090`](../src/main.c#L1090) selects the requested decoration mode for each toplevel.
- [`src/main.c:4203`](../src/main.c#L4203) creates the `wlr_xdg_decoration_manager_v1` manager.
- [`src/config.c:1330`](../src/config.c#L1330) parses `[decoration]` defaults.
- [`src/config.c:1345`](../src/config.c#L1345) parses `[decoration_rule]` overrides.
- [`docs/DEVFAQ.md:169`](DEVFAQ.md#L169) documents that the current long-term fix for Java window controls is real SSD.

## What Still Needs Work

- [x] Per-layout `xdg-decoration` policy exists via config and runtime layout checks. Source: [`src/main.c:1090`](../src/main.c#L1090), [`src/config.c:1330`](../src/config.c#L1330)
- [x] Rule-based decoration overrides already exist. Source: [`src/config.c:1345`](../src/config.c#L1345), [`docs/CONFIG.md:106`](CONFIG.md#L106)
- [ ] Real compositor-drawn SSD chrome is still missing. No title bar, caption buttons, hit zones, or themeable SSD rendering layer exists yet.
- [ ] Java and older Wayland clients that cannot expose minimize/maximize through current client-side decoration paths still need the SSD implementation to become fully usable.
- [ ] Existing `xdg-shell` guards should remain additive safety checks around the eventual SSD path, not be removed during the SSD implementation.

# ----------------------------------------------------------------------------
# Workspace architecture and scalability
# ----------------------------------------------------------------------------

## Current State

Morph currently supports nine workspaces and uses `COMP_WORKSPACE_COUNT` as a compile-time constant. The visible behavior, IPC handling, and config validation already compare against that constant consistently, but the underlying storage remains static.

Relevant implementation points:

- [`src/main.c:2150`](../src/main.c#L2150) clamps absolute workspace changes against `COMP_WORKSPACE_COUNT`.
- [`src/main.c:2194`](../src/main.c#L2194) uses the same constant for wrap-around relative navigation.
- [`src/ext_workspace.c:20`](../src/ext_workspace.c#L20) stores workspace handles in a fixed-size array.
- [`src/ext_workspace.c:230`](../src/ext_workspace.c#L230) emits one ext-workspace handle per fixed workspace slot.

## What Still Needs Work

- [x] Absolute workspace switching, relative switching, and workspace move behavior are implemented. Source: [`src/main.c:2150`](../src/main.c#L2150), [`src/main.c:2190`](../src/main.c#L2190), [`src/main.c:2203`](../src/main.c#L2203)
- [x] External workspace exposure already exists through `ext_workspace_manager_v1`. Source: [`src/ext_workspace.c:374`](../src/ext_workspace.c#L374), [`docs/PROTOCOLS.md:22`](PROTOCOLS.md#L22)
- [ ] Replace static arrays such as `handles[COMP_WORKSPACE_COUNT]` with dynamic storage allocated after config parsing. Source: [`src/ext_workspace.c:20`](../src/ext_workspace.c#L20)
- [ ] Replace direct `COMP_WORKSPACE_COUNT` loop and bounds usage with a runtime field such as `server->workspace_count`. Source: [`src/main.c:2154`](../src/main.c#L2154), [`src/main.c:2194`](../src/main.c#L2194), [`src/ext_workspace.c:56`](../src/ext_workspace.c#L56)
- [ ] Decide whether a future `workspace_count = N` config knob should exist at all, or whether the constant should remain fixed deliberately.
- [ ] Keep a hard internal safety cap even if the workspace count becomes dynamic, so bars and workspace UIs cannot be pushed into unreasonable configurations.

# ----------------------------------------------------------------------------
# Layout modes and tiling controller follow-up
# ----------------------------------------------------------------------------

## Current State

The compositor already ships the three intended runtime layouts: stack, tile, and scroll. Layout switching is available through keybinds, CLI flags, and IPC.

Relevant implementation points:

- [`docs/COMPOSITOR.md:7`](COMPOSITOR.md#L7) documents stack, tile, and scroll as implemented current scope.
- [`src/config.c:159`](../src/config.c#L159) through [`src/config.c:172`](../src/config.c#L172) parse the layout actions.
- [`src/main.c:2676`](../src/main.c#L2676) handles `layout toggle|tile|scroll|stack` over IPC.

## What Still Needs Work

- [x] The explicit stack, tile, and scroll layout model already exists. Source: [`docs/COMPOSITOR.md:7`](COMPOSITOR.md#L7), [`src/config.c:159`](../src/config.c#L159), [`src/main.c:2676`](../src/main.c#L2676)
- [x] Layout switching already exists through keybinds, CLI, and IPC. Source: [`src/config.c:159`](../src/config.c#L159), [`src/main.c:2676`](../src/main.c#L2676), [`docs/COMPOSITOR.md:17`](COMPOSITOR.md#L17)
- [ ] Decide whether additional layout modes such as monocle or a richer workspace-local float mode are actually needed, instead of keeping the current three-mode model stable.
- [ ] If more layout modes are added later, keep the current tile and scroll movement semantics coherent instead of creating action drift across layouts.
- [ ] Revisit floating overrides only if real use cases show that the current tile-float behavior is too limited.

# ----------------------------------------------------------------------------
# Compositor-owned UI foundation
# ----------------------------------------------------------------------------

## Current State

Morph already has compositor-owned input handling, scene graph usage, and some compositor-controlled chrome-adjacent behavior such as decoration policy selection, titlebar hit handling, and layout-driven raise/focus decisions. What it does not have yet is a reusable compositor-owned UI layer for overlays, popups, switchers, menus, or future SSD chrome.

Relevant implementation points:

- [`src/main.c:2108`](../src/main.c#L2108) already centralizes keyboard focus handoff for toplevels.
- [`src/main.c:4827`](../src/main.c#L4827) already centralizes pointer-button focus handoff and compositor-owned grab behavior.
- [`src/main.c:623`](../src/main.c#L623) and related scene helpers already perform compositor-side hit testing.
- [`docs/roadmaps/Roadmap_compositor-ui.de.md`](roadmaps/Roadmap_compositor-ui.de.md) now captures the intended shared UI direction for WinList, root menus, and later SSD titlebars.

## What Still Needs Work

- [x] Add a real WindowList / MRU structure separate from `server->toplevels`, since the existing toplevel list is not a focus-history model. Source: [`src/server.h`](../src/server.h) (`comp_server.focus_order`, `comp_toplevel.focus_link`), [`src/main.c`](../src/main.c) (`server_window_focus_cycle`, `toplevel_focus_candidate`)
- [x] Ship `Alt-Tab` / `Alt-Shift-Tab` without an overlay first: `nextWindow` / `prevWindow` keybind actions, `window focus next|prev` IPC, and `--window-focus next|prev` CLI all commit focus immediately.
- [x] Held-modifier cycling session: freeze the candidate ring while Alt is held, preview focus without reordering MRU, commit on modifier release, cancel with `Esc`.
- [x] Compositor-owned switcher overlay for the held session: title / app_id list with the current candidate highlighted, visual-only (pointer pass-through). Source: [`src/ui.c`](../src/ui.c), [`src/ui.h`](../src/ui.h), `server->ui_tree`
- [ ] Grow `src/ui.c` into a small reusable compositor-owned UI foundation before implementing further one-off chrome independently (root menus, SSD titlebars).
- [ ] Use that shared UI layer for root menus after the switcher, and only after that for SSD titlebars and other compositor-owned chrome. The MRU list above is the data source the switcher already reads.
- [ ] Decide whether scratchpad, fullscreen, and future per-output workspaces participate in the cycle candidate set; currently the filter is mapped + initialized + non-minimized + current workspace.
- [ ] Show icons in the switcher when a freedesktop icon is available for the window's app_id.
- [ ] Keep the root-menu DSL and registry work separate from the shared popup/overlay runtime so menus do not become the implicit UI foundation for everything else.
- [ ] Revisit theme integration later so compositor-owned UI can eventually align with client-side styling where practical, without making GTK/Qt a hard dependency for the first implementation.

# ----------------------------------------------------------------------------
# Keybind config and shell-conditional follow-up
# ----------------------------------------------------------------------------

## Current State

The keybind config is already much more mature than the old roadmap assumed. Morph supports INI-style `[bind]` sections, `when=` shell predicates, multiple layout actions, workspace actions, tile-grid actions, and config reload through IPC.

Relevant implementation points:

- [`src/config.c:145`](../src/config.c#L145) parses the current action set.
- [`src/config.c:366`](../src/config.c#L366) evaluates dynamic `when=` shell predicates.
- [`docs/CONFIG.md:30`](CONFIG.md#L30) documents the current bind format and trusted-shell model.
- [`docs/CONFIG.md:140`](CONFIG.md#L140) onward documents the current action set in detail.

## What Still Needs Work

- [x] INI-style `[bind]` parsing exists. Source: [`docs/CONFIG.md:30`](CONFIG.md#L30), [`src/config.c:145`](../src/config.c#L145)
- [x] Dynamic `when=` shell conditionals exist and are evaluated on each key press. Source: [`src/config.c:366`](../src/config.c#L366), [`docs/CONFIG.md:39`](CONFIG.md#L39)
- [x] Workspace, layout, tile, scroll, and grid actions already exist. Source: [`src/config.c:159`](../src/config.c#L159), [`src/config.c:224`](../src/config.c#L224), [`docs/CONFIG.md:140`](CONFIG.md#L140)
- [x] Config reload through IPC already exists. Source: [`docs/CONFIG.md:229`](CONFIG.md#L229), [`src/main.c:4677`](../src/main.c#L4677)
- [ ] Decide whether further actions are still missing in practice, or whether the current action surface is already sufficient for the intended workflow.
- [ ] Only add caching or alternate execution strategies for `when=` if real-world latency becomes measurable; the current design is explicit and easy to reason about.
- [ ] Keep the trusted-shell execution model documented clearly if the bind system grows further, so convenience additions do not obscure the security model.

# ----------------------------------------------------------------------------
# Configurable focus policy
# ----------------------------------------------------------------------------

## Current State

Morph supports configurable pointer-driven keyboard focus policies via **`[focus]`** in the INI config. The default remains **`ClickToFocus`**. **`FocusFollowsMouse`** and **`SloppyFocus`** are implemented, and interaction rules with layer-shell, root clicks, grabs, and future SSD are documented in **`docs/CONFIG.md`**.

Relevant implementation points:

- [`src/config.h`](../src/config.h) defines `enum comp_focus_policy` and `comp_config.focus_policy`.
- [`src/config.c`](../src/config.c) parses the **`[focus]`** section (`policy` / `mode` / `focus_policy`).
- [`src/main.c`](../src/main.c) applies enter/leave policy from pointer motion and gates empty-root click clearing for SloppyFocus.
- [`docs/CONFIG.md`](CONFIG.md) documents policies and interaction rules.

## What Still Needs Work

- [x] Default click-to-focus behavior exists for normal toplevel clicks and root-click focus clearing.
- [x] Config option for `ClickToFocus` as the explicit default policy. Source: [`src/config.c`](../src/config.c), [`docs/CONFIG.md`](CONFIG.md)
- [x] `FocusFollowsMouse`: pointer enter gives keyboard focus; leave to root clears focus.
- [x] `SloppyFocus`: pointer enter gives keyboard focus; root does not clear until another focus target is selected.
- [x] Focus policy interactions with layer-shell, root clicks, compositor-owned move/resize grabs, and future SSD are documented in `docs/CONFIG.md`.
- [ ] Manual nested/native session checks for FocusFollowsMouse / SloppyFocus against panels, grabs, and multi-window stack raise behavior (until a compositor input harness exists).

# ----------------------------------------------------------------------------
# Compositor input test coverage
# ----------------------------------------------------------------------------

## Current State

Morph has automated coverage for config parsing and shell runtime behavior, but compositor input behavior is still validated mostly through manual nested/native session checks. Bugs around keybind-consumed modifier state, pointer grabs, titlebar controls, and click-to-focus currently require a real wlroots seat to reproduce accurately.

Relevant implementation points:

- [`src/main.c:3577`](../src/main.c#L3577) handles keyboard key events and compositor keybind consumption.
- [`src/main.c:4827`](../src/main.c#L4827) handles pointer buttons, focus handoff, and compositor-owned move/resize grabs.
- [`tests/test_config.c`](../tests/test_config.c) covers config behavior.
- [`tests/test_shell_runtime.sh`](../tests/test_shell_runtime.sh) covers launcher and shell-runtime behavior.

## What Still Needs Work

- [x] Config and shell-runtime behavior have automated tests. Source: [`tests/test_config.c`](../tests/test_config.c), [`tests/test_shell_runtime.sh`](../tests/test_shell_runtime.sh)
- [ ] Add focused C tests for helper-level input state where practical, especially consumed modifier suppression after compositor-handled Logo keybinds.
- [ ] Consider extracting small pure helpers from keyboard/pointer handling only when it gives stable tests without hiding wlroots seat semantics.
- [ ] Keep full focus, titlebar, pointer grab, and click-to-focus scenarios in the manual nested/native test plan until Morph has a real compositor input harness.

# ----------------------------------------------------------------------------
# Plugin system and external control surface
# ----------------------------------------------------------------------------

## Current State

The old roadmap mixed three different ideas: native plugins, external control, and embedded scripting. External control is already present today through the local text IPC socket and Wayland-facing protocol objects. What does not exist is a true native plugin ABI or an embedded scripting layer.

Relevant implementation points:

- [`src/main.c:2621`](../src/main.c#L2621) builds and uses the local IPC socket path.
- [`src/main.c:2661`](../src/main.c#L2661) parses inbound IPC commands.
- [`src/ext_workspace.c:374`](../src/ext_workspace.c#L374) advertises `ext_workspace_manager_v1`.
- [`src/main.c:4188`](../src/main.c#L4188) creates the foreign-toplevel manager.
- [`docs/PROTOCOLS.md:22`](PROTOCOLS.md#L22) and [`docs/PROTOCOLS.md:25`](PROTOCOLS.md#L25) document the current protocol-facing surface.

## What Still Needs Work

- [x] Local external control via Unix-socket IPC already exists. Source: [`src/main.c:2621`](../src/main.c#L2621), [`src/main.c:2661`](../src/main.c#L2661), [`docs/COMPOSITOR.md:19`](COMPOSITOR.md#L19)
- [x] Wayland-facing external control already exists through workspace and foreign-toplevel protocols. Source: [`src/ext_workspace.c:374`](../src/ext_workspace.c#L374), [`src/main.c:4188`](../src/main.c#L4188), [`docs/PROTOCOLS.md:22`](PROTOCOLS.md#L22)
- [ ] If Morph should support native plugins, define a small versioned ABI instead of exposing compositor internals directly.
- [ ] Decide whether embedded scripting is actually worth the complexity compared with wrapper hooks and external IPC-driven tools.
- [ ] Keep the distinction between plugin ABI, local IPC automation, and Wayland-facing protocol integration explicit in the docs so future work does not collapse them into one vague “plugin system” bucket.

# ----------------------------------------------------------------------------
# Theming and desktop integration follow-up
# ----------------------------------------------------------------------------

## Current State

Some of the old notes in this area are already covered elsewhere. XKB environment handling is implemented and documented, wrapper-based environment layering exists, and portal/session integration has dedicated docs. What remains open is mostly compositor-owned theming and desktop-polish work.

Relevant implementation points:

- [`src/main.c:3501`](../src/main.c#L3501) reads `XKB_DEFAULT_*` rules from the environment.
- [`docs/ENVIRONMENT.md:109`](ENVIRONMENT.md#L109) documents the `XKB_DEFAULT_*` variables.
- [`docs/LAUNCHER.md:93`](LAUNCHER.md#L93) onward documents wrapper-driven portal and hook resolution.
- [`docs/PROTOCOLS.md:23`](PROTOCOLS.md#L23) documents the current `xdg-decoration` exposure.

## What Still Needs Work

- [x] XKB environment handling is implemented and documented. Source: [`src/main.c:3501`](../src/main.c#L3501), [`docs/ENVIRONMENT.md:109`](ENVIRONMENT.md#L109)
- [x] Wrapper-driven environment and session integration already exist. Source: [`docs/LAUNCHER.md:93`](LAUNCHER.md#L93), [`docs/ENVIRONMENT.md:9`](ENVIRONMENT.md#L9)
- [ ] Decide whether Morph should expose any compositor-owned theme surface beyond the current environment-driven client setup, for example cursor/theme defaults, future SSD styling, or session appearance integration.
- [ ] If SSD is implemented, define a small internal theme model for title bars and compositor-owned chrome instead of hardcoding visual choices.
- [ ] Keep desktop integration work tied to concrete goals such as settings propagation, cursor consistency, or color-management hooks, rather than treating “theming” as a catch-all bucket.
