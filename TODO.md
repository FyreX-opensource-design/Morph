# TODO

## Server-side decorations for Java/Wayland windows

- Add SSD support so stackcomp can draw its own titlebar buttons.
- This is the long-term path for apps that only negotiate xdg-shell v3 and therefore cannot advertise minimize/maximize via `wm_capabilities`.
- Needed especially for Java apps like yEd where client-side decorations currently expose only close under native Wayland.
- Keep the existing xdg-shell version guards for stability; SSD should be additive, not a replacement for protocol checks.

## Workspaces: Current Architecture & Scalability

By default, `stackcomp` is limited to `9` workspaces. This limit is currently defined as a compile-time constant (`COMP_WORKSPACE_COUNT`).

This is a purely internal soft limit and not a restriction imposed by the Wayland or `wlroots` protocols.

### Technical Background & Scaling

The compositor uses static arrays based on `COMP_WORKSPACE_COUNT` to manage workspace-specific states, for example:
- Per-output scroll states (`workspace_scroll_slot`) in `src/server.h`
- Extension handles (`handles`) in `src/ext_workspace.c`

Since navigation, validation, and configuration boundary checks (e.g., in `src/main.c` and `src/config.c`) already dynamically check against this constant, the underlying logic is well-prepared for variable sizes.

### Dynamic Config (`workspace_count = N`)

To make the workspace count flexible and controllable via the configuration file, two main architectural adjustments are required:

1. **Dynamic Memory Allocation:** Replace the fixed-size arrays in the server structure and extension sessions with dynamic arrays or vectors initialized during configuration parsing.
2. **Runtime Variable Instead of Macro:** Replace the `COMP_WORKSPACE_COUNT` macro in loops and boundary checks with a new runtime field (e.g., `server->workspace_count`).

### Technical & UX Recommendations

Even when opening up the limit dynamically, the following guidelines have proven practical:

* **1–9 (Default):** Ideal for direct keybinding mapping (keys 1–9) and keeping status bars like Waybar clean and readable.
* **10–16 (Extended Range):** Perfectly fine for setups that primarily rely on sequential (`next` / `prev`) navigation.
* **Hard Cap at 32:** Even with dynamic allocation, enforcing an internal safety limit in the code (e.g., a maximum of 32) is recommended to catch unreasonable configuration values and ensure UI stability, even though the protocols could theoretically handle more.