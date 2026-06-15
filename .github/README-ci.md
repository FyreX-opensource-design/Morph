# CI Setup Guide (Beginner-Friendly)

This guide explains how CI works in this repository and how to run it both
on GitHub and locally.

Workflow file:

- .github/workflows/ci.yml

## Overview

The workflow has two jobs:

1. build-and-test
   - Runs on a self-hosted Linux runner
   - Runs configure, compile, and tests through scripts/ci-local.sh
   - Uploads Meson logs as artifacts
2. workflow-sanity
   - Runs on ubuntu-latest
   - Lints workflow files with actionlint

Why self-hosted for build-and-test:

- wlroots and related graphics stack dependencies can vary across distro images.
- A self-hosted runner gives a reproducible environment for this compositor.

## Step 1: Create a self-hosted runner

In GitHub repository settings:

1. Open Settings.
2. Open Actions and then Runners.
3. Click New self-hosted runner.
4. Choose Linux and follow the generated commands on your runner machine.
5. Ensure labels include self-hosted and linux.

## Step 2: Install dependencies on the runner machine

Minimum toolchain:

- meson
- ninja
- pkg-config
- wayland-scanner
- wayland-protocols
- wlroots 0.19 development files (pkg-config name should resolve as wlroots-0.19)
- pixman development files
- wayland-server development files
- xkbcommon development files

Quick checks:

```bash
meson --version
ninja --version
pkg-config --modversion wlroots-0.19
wayland-scanner --version
```

## Step 3: Validate CI locally before push

Run exactly what CI uses:

```bash
./scripts/ci-local.sh build
```

This runs:

1. meson setup build --reconfigure
2. meson compile -C build
3. meson test -C build --print-errorlogs

## Step 4: Run CI remotely on GitHub

Triggers:

- push
- pull_request
- workflow_dispatch (manual)

How to run manually:

1. Open Actions.
2. Open CI.
3. Click Run workflow.
4. Select branch and start.

## Step 5: Understand artifacts when something fails

The build-and-test job uploads:

- build/meson-logs/meson-log.txt
- build/meson-logs/testlog.txt

Use these files first for diagnosis.

## Optional: Run workflow locally with act

You can emulate workflow jobs locally:

Before running `act`, install and enable a container runtime API.
For Debian, the easiest path is `docker.io`.

Install and activate Docker on Debian:

```bash
sudo apt update
sudo apt install -y docker.io
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
```

Then log out and log in again, and verify Docker access:

```bash
docker version
docker run --rm hello-world
```

Important: `docker-cli` alone is not enough for `act`. `act` needs a running
Docker API/socket (`/var/run/docker.sock`) to start containers.

Install `act` (generalized GitHub binary variant):

```bash
TMP_DIR="$(mktemp -d)"
URL="$(curl -fsSL https://api.github.com/repos/nektos/act/releases/latest | grep -Eo 'https://[^\"]*act_Linux_x86_64\\.tar\\.gz' | head -n1)"
curl -fsSL "$URL" -o "$TMP_DIR/act.tar.gz"
tar -xzf "$TMP_DIR/act.tar.gz" -C "$TMP_DIR"
install -Dm755 "$TMP_DIR/act" "$HOME/.local/bin/act"
rm -rf "$TMP_DIR"
act --version
```

If `act` is still not found, reload your shell or ensure `$HOME/.local/bin` is in `PATH`.

```bash
act -j workflow-sanity
act -j build-and-test
```

Progress expectations:

- The first `act` run usually downloads one or more large Docker images.
- During image download/build phases, output can pause briefly and look stuck.
- This is normal as long as Docker activity continues.

Useful commands while waiting (second terminal):

```bash
docker ps
docker images | grep catthehacker
```

More verbose output from act:

```bash
act -j workflow-sanity -v
```

Optional pre-pull to reduce waiting time in future runs:

```bash
docker pull catthehacker/ubuntu:act-latest
```

Note:

- build-and-test still needs an act image/environment with compatible wlroots
  and Wayland dependencies.

## Troubleshooting

1. Error: Dependency wlroots-0.19 not found
   - Check: pkg-config --modversion wlroots-0.19
   - Fix: install the matching development package or update PKG_CONFIG_PATH
2. Error: wayland-scanner not found
   - Fix: install Wayland development tooling and wayland-protocols
3. Local works, runner fails
   - Compare local output and uploaded Meson logs
   - Check runner environment variables (PKG_CONFIG_PATH, LD_LIBRARY_PATH)
4. Job remains pending
   - Check runner is online and labels match self-hosted and linux
5. act fails with "failed to connect to the docker API" or missing
   /var/run/docker.sock
   - Ensure docker.io is installed
   - Ensure Docker service is running: systemctl status docker
   - Ensure current user is in docker group (re-login required after usermod)
