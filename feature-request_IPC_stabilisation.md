# Feature Request: IPC Stabilisation and wayctl Alignment

This document captures IPC stabilization proposals and alignment strategy between stackcomp and wayctl.

## Current Context

stackcomp and wayctl are both dual-surface systems, but with different priorities:

- stackcomp: local command socket + Wayland protocol servers
- wayctl: protocol-driven CLI + optional daemon IPC envelope

The sections below describe current differences and a proposed convergence strategy.

## stackcomp IPC vs wayctl IPC

wayctl itself is also dual-surface:

- Wayland protocol operations (`get` / `set/send`)
- Optional daemon IPC envelope (`hello`, `capabilities`, `monitor.subscribe`)

Practical differences today:

| Topic | stackcomp | wayctl |
|---|---|---|
| Primary local automation | Plain text Unix socket commands | CLI commands and optional daemon protocol envelopes |
| Workspace create/remove/assign | Not implemented in ext-workspace handlers yet | Commands exist, but only work if compositor supports capabilities |
| Window control | Via Wayland foreign-toplevel requests | Strong CLI abstraction over foreign-toplevel (`focus`, `close_window`, `fullscreen`, etc.) |
| Event streaming API | No dedicated JSON/event socket yet | Daemon monitor topics and subscription model |
| Stability contract | Command strings in compositor docs | Versioned CLI + daemon handshake/capabilities model |

## Strategy To Align stackcomp and wayctl

Goal: preserve stackcomp's simple local socket while exposing capability-driven behavior that wayctl can use consistently.

1. Define a capability matrix in stackcomp docs/code:
   - workspace: `activate`, `create`, `remove`, `assign`
   - toplevel: `focus`, `close`, `state`
2. Implement missing ext-workspace operations server-side (or explicitly reject with protocol errors if unsupported).
3. Add structured acknowledgements for local socket commands:
   - optional response line (`ok`, `error:<reason>`) to reduce silent failures.
4. Add optional event stream for local automation:
   - workspace-changed, layout-changed, reload-complete.
5. Keep command naming parallel to wayctl verbs where reasonable:
   - keep `workspace next/prev/move` and map cleanly to wayctl `switch_workspace` semantics.
6. Add integration tests using wayctl in CI (smoke-level):
   - `get workspaces`, `set switch_workspace`, `get toplevels`, `set focus`, `set close_window`.
7. Document a compatibility profile version in README/COMPOSITOR docs so tools can branch predictably.

## Short-Term Recommendation

- treat wayctl as the protocol-validation client and stackcomp socket as the fast local control plane.
- converge behavior first (capabilities and responses), then converge transport semantics.
