# GitHub Workflows: Remote and Local

This project uses one CI workflow file:

- .github/workflows/ci.yml

The workflow contains two jobs:

1. build-and-test (self-hosted)
2. workflow-sanity (GitHub-hosted)

If you are new to GitHub Actions runner setup, start with
.github/README-ci.md.

## What runs where

1. build-and-test
   - Runner: self-hosted Linux
   - Purpose: real project build and tests with your wlroots toolchain
   - Command path: scripts/ci-local.sh build
   - Artifacts on every run: build/meson-logs/meson-log.txt and
     build/meson-logs/testlog.txt
2. workflow-sanity
   - Runner: ubuntu-latest
   - Purpose: lint workflow syntax and structure with actionlint

## Fast start (local)

Use the exact same command sequence as the CI build job:

```bash
./scripts/ci-local.sh build
```

This command does configure, compile, and tests in one step.

## Fast start (GitHub remote)

1. Push your branch.
2. Open the Actions tab.
3. Run CI (or wait for push and pull_request trigger).
4. Open the build-and-test job log.
5. If something fails, download meson logs from workflow artifacts.

## Local workflow emulation with act

You can execute the workflow file locally with act.

1. Install act from https://github.com/nektos/act
   - Recommended here: GitHub release binary install (see .github/README-ci.md)
2. Run the GitHub-hosted job locally:

```bash
act -j workflow-sanity
```

3. Run the build-and-test job:

```bash
act -j build-and-test
```

Important: the build-and-test job still needs a container image/environment
that provides compatible Meson, wlroots pkg-config metadata, and Wayland
tooling.

## Typical failures and fixes

1. Dependency wlroots-0.19 not found
   - Check: pkg-config --modversion wlroots-0.19
   - Fix: install the matching wlroots development package or fix
     PKG_CONFIG_PATH
2. wayland-scanner missing
   - Fix: install Wayland development tools and wayland-protocols
3. Works locally but fails on runner
   - Compare local output with artifact logs from GitHub run
   - Ensure runner environment exports expected PKG_CONFIG_PATH and
     LD_LIBRARY_PATH
