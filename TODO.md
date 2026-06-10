# TODO

## Server-side decorations for Java/Wayland windows

- Add SSD support so stackcomp can draw its own titlebar buttons.
- This is the long-term path for apps that only negotiate xdg-shell v3 and therefore cannot advertise minimize/maximize via `wm_capabilities`.
- Needed especially for Java apps like yEd where client-side decorations currently expose only close under native Wayland.
- Keep the existing xdg-shell version guards for stability; SSD should be additive, not a replacement for protocol checks.