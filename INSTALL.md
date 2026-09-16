# Morph Installation

This document collects the current build dependencies and the supported install
and uninstall flows for both runtime and development setups.

## Dependencies

### Building

Morph currently builds against the following primary dependencies:

| Arch | Debian | Fedora | >= version |
|---|---|---|---|
| base-devel | g++ | gcc-c++ | 16 |
| cmake | cmake | cmake | 3.20 |
| meson | meson | meson | 1.11 |
| pkgconf | pkg-config | pkgconf-pkg-config | 2.5 |
| wlroots0.20 | libwlroots-0.20-dev | wlroots-devel | 0.20.2 |
| lib32-wayland | libwayland-server0³ | libwayland-server³ | 1.25 |
| xwayland-satellite | not available¹ | xwayland-satellite | 0.8 |
| wayland-protocols | wayland-protocols | wayland-protocols-devel | 1.49 |
| wlr-protocols | not available² | wlr-protocols-devel | |
| xdg-desktop-portal-wlr | xdg-desktop-portal-wlr | xdg-desktop-portal-wlr | 0.84 |
| xdg-desktop-portal-gtk | xdg-desktop-portal-gtk | xdg-desktop-portal-gtk | 1.15 |
| wayland| wayland-scanner³ | wayland-scanner³ | 1.25 |
| libxkbcommon | libxkbcommon-dev | libxkbcommon | 1.13 |
| pixman | pixman-1-dev | pixman-1-dev | 0.44 |

- POSIX shell `sh`

¹ `xwayland-satellite` has no official package in Debian repositories. To use it on Debian, install Rust and Cargo, then compile it manually using Cargo.

Install build tools and XCB libraries via apt:
```bash
sudo apt install -y cargo rustc libxcb1-dev libxcb-composite0-dev libxcb-render0-dev libxcb-shape0-dev libxcb-xfixes0-dev libclang-dev
```

Ensure xwayland is installed on your system:
```bash
sudo apt install xwayland
```

Clone repository and build:
```bash
git clone https://github.com/Supreeeme/xwayland-satellite.git
cd xwayland-satellite
cargo build --release
```

Make available system-wide:
```bash
sudo cp target/release/xwayland-satellite /usr/local/bin/
```

² `wlr-protocols` has no official package in Debian repositories. To use it on Debian, it has to be build from source.

clone the repository:
```bash
git clone https://gitlab.freedesktop.org/wlroots/wlr-protocols.git
cd wlr-protocols
```

Build and install them system-wide:
```bash
make
sudo make install
```

³ `wayland-scanner` and `libwayland-server` is in Debian and Fedora included in `libwayland-dev`.

Version note:

- `meson.build` currently enforces `wayland-protocols >= 1.32` directly
- the other `>=` values are practical orientation points from the currently
  tested toolchain, not additional Meson-enforced version gates

For a native DRM session you also need the usual wlroots runtime stack,
especially DRM/libinput and their system libraries.

The authoritative build dependency declarations live in `meson.build`. This
document mirrors that information so installation steps stay in one place.

### Running

Further dependencies needed to use Morph:

- `alacritty` `>= 0.15`

#### Optional

- Background: `swaybg`
- Panel: `wbar` or `sfwbar`
- Sound: `pavucontrol`

### Testing

- `xdpyinfo` `>= 1.3.4`
- `xset` `>= 1.2.5`
- 'xterm` `>= 398`
- `rg` (ripgrep) `>= 14`
- `jq` `>= 1.8`

## Release Install

The standard runtime install uses Meson and installs into the system layout
under `/usr` and `/etc`.

Build and install:

```bash
# existing build dir (updates configured defaults such as prefix/sysconfdir)
# meson setup build --reconfigure

# fresh build dir
meson setup build
meson compile -C build

# install into system
sudo meson install -C build
```

Runtime library lookup note (local wlroots builds):

- some distributions do not provide `wlroots-0.20` runtime libraries in system
  paths; in that case Morph may depend on local libraries under `~/.local/lib`
- if `LD_LIBRARY_PATH` is empty, `morph-session` auto-adds existing local
  library paths as a fallback and logs a warning
- for an explicit persistent setup, uncomment the `LD_LIBRARY_PATH` block in
  `/etc/morph/environment` or in `~/.config/morph/environment`
- prefer system runtime library packages when available

## Release Uninstall

System installs are removed conservatively through
`scripts/system-uninstall.sh`.

Preview the current install manifest:

```bash
./scripts/system-uninstall.sh --builddir build
```

Remove **only unchanged** installed files:

```bash
sudo ./scripts/system-uninstall.sh --builddir build --remove
```

Important details:

- the script reads `meson introspect --installed`
- removal is opt-in; default mode is print-only
- files that were modified after install are kept with a warning
- missing or unverifiable files are kept with a warning
- empty parent directories are pruned afterwards when safe

This keeps local admin changes under `/etc`, `/usr/share`, or custom prefixes
from being deleted silently.

## Development Install

Development install is handled by `scripts/dev-install.sh`. It does not install
the compositor system-wide by default. Instead it creates safe symlinks for a
repository-backed developer workflow.

Base development install:

```bash
./scripts/dev-install.sh install
```

This links the development copies from `testing/config/` into
`$XDG_CONFIG_HOME/morph` or `~/.config/morph`:

- `morph.conf`
- `startup.sh`
- `reload.sh`
- `shutdown.sh`
- `environment`
- `portals`

Optional helper installs:

```bash
./scripts/dev-install.sh install --link-launcher
./scripts/dev-install.sh install --desktop-local
./scripts/dev-install.sh install --print-sudo-help
```

Options:

- `--link-launcher`
  - creates `~/.local/bin/morph-session_dbg` as a symlink to
    `testing/morph-session_dbg`
- `--desktop-local`
  - copies `sessions/morph.desktop` into
    `~/.local/share/wayland-sessions`
- `--print-sudo-help`
  - prints the commands needed to expose the development session through
    system paths such as `/usr/bin` and `/usr/share/wayland-sessions`

For display-manager-visible development sessions, the system launcher at
`/usr/bin/morph-session_dbg` is installed as a real wrapper file. The wrapper
executes the repository's `testing/morph-session_dbg` entry point instead of
being a symlink into the user's home directory. The local launcher created by
`--link-launcher` remains a symlink for fast repository-based shell testing.

Safety behavior:

- existing real user files are never replaced
- unrelated symlinks are never replaced
- such cases produce explicit warnings and the existing files stay in use

## Development Uninstall

Remove the development symlinks that `dev-install.sh` created:

```bash
./scripts/dev-install.sh uninstall
```

If you also linked the local launcher, remove that symlink too:

```bash
./scripts/dev-install.sh uninstall --link-launcher
```

Important details:

- only symlinks that point back to this repository are removed
- unrelated symlinks are left untouched
- real user files are never removed
- local desktop copies created with `--desktop-local` are not removed by the
  current helper and must be deleted manually if no longer needed

## Recommended Flows

Use the runtime install when you want a real system session:

```bash
meson setup build
meson compile -C build
sudo meson install -C build
```

Use the development install when you want repository-backed config and quick
iteration:

```bash
./scripts/dev-install.sh install --link-launcher
./testing/morph-session_dbg
```

For display-manager-visible development sessions, use the printed sudo guidance
from:

```bash
./scripts/dev-install.sh install --print-sudo-help
```

Some login managers, including LightDM setups, must be able to traverse the
repository path because the development session files are symlinks into your
checkout. If the display manager cannot start the development session because it
cannot access files below your home directory, allow directory traversal on your
home directory:

```bash
chmod o+x "$HOME"
```

This grants execute/traverse permission only. It does not grant permission to
list the contents of your home directory.

## Morph scripts for building, installing, and uninstalling

There are dedicated Morph helper scripts that make building, installing, and uninstalling a bit easier.

```bash
# build runtime + debug variants
./scripts/morph-build.sh --both

# install runtime + debug artifacts
./scripts/morph-install.sh --both

# uninstall runtime + debug artifacts
./scripts/morph-uninstall.sh --both

# preview install commands without changes
./scripts/morph-install.sh --both --dry

# preview uninstall commands without changes
./scripts/morph-uninstall.sh --both --dry
```

Build script modes:

- `--runtime` uses `build` with `buildtype=release` and `strip=true`
- `--debug` uses `build_dbg` with `buildtype=debug` and `strip=false`
- `--both` runs both build modes in order

Install script modes:

- `--runtime` runs `meson install -C build`
- `--debug` delegates to `dev-install.sh install --link-launcher --system-links`
  and creates repository-backed user config links, `~/.local/bin/morph-session_dbg`,
  and the display-manager-visible debug session links under `/usr`
- `--both` runs runtime install first, then debug install
- `--dry` prints install commands only

Uninstall script modes:

- `--runtime` runs `./scripts/system-uninstall.sh --builddir build --remove`
- `--debug` delegates to `dev-install.sh uninstall --link-launcher --system-links`
  and removes only matching repository-backed user links plus the system-visible
  debug session links
- `--both` runs runtime uninstall first, then debug uninstall
- `--dry` prints uninstall commands only

Installed artifacts:

- `morph` under `/usr/bin/morph`
- `morph-session` under `/usr/bin/morph-session`
- runtime files under `/etc/morph/`
- `morph.desktop` under `/usr/share/wayland-sessions/morph.desktop`
- selected documentation under `/usr/share/doc/morph/`

This is the right path for:

- system-wide testing through a display manager
- packaging
- any setup that should match the runtime file layout
