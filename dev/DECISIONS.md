# Decisions

Format: `D-NNN — title (date, status)`. Supersede rather than edit.

## D-001 — Base on Krusader v2.8.1 (Qt5/KF5) (2026-11-17, accepted)

- Target: Zorin OS 18.1 = Ubuntu 24.04 "noble" base (verified on the
  implementation host itself via `/etc/os-release`).
- Upstream `v2.9.0` (2024-12-16) and `master` require Qt6 + KF6
  (`KF6_MIN_VERSION 5.240.0`). Noble's archive ships Qt 6.4 but **no KF6
  development packages** (`libkf6kio-dev` has no candidate), so building them
  would mean bundling a large unofficial KF6 stack.
- `v2.8.1` (commit `dbc4540bc`, 2024-03-03) requires Qt5 ≥ 5.12 and KF5, which
  noble provides (Qt 5.15.13, KF5 5.115). Ubuntu itself ships Krusader
  2.8.1 for noble, which shows it builds and runs there.
- Consequence: all runtime libraries come from distro packages; no bundled
  third-party binaries. Future move to KF6 is possible once the target distro
  ships KF6 (likely Ubuntu 26.04 based Zorin); cherry-picking selected
  post-2.8.1 upstream fixes is an option.

## D-002 — Standalone GitHub repository, not a GitHub "fork" (2026-11-17, accepted)

- Create `NorbCraft/panero` as a normal public repository and push the full
  upstream history to it, with `upstream` remote pointing at invent.kde.org.
- Reason: GitHub fork networks default PR targets to the parent
  (`KDE/krusader`), risking accidental upstream PRs (explicitly forbidden),
  and forks have Actions/issues disabled by default. History, authorship
  and attribution are fully preserved either way.

## D-003 — Version mapping (2026-11-17, accepted)

- Git tag `v0.1.0-preview.1` ↔ Debian version `0.1.0~preview.1-1`
  (`~` sorts before `0.1.0`, so the final `0.1.0` upgrades cleanly).
  Asset file name: `panero_0.1.0~preview.1-1_amd64.deb`.

## D-004 — Repository layout (2026-11-17, accepted by owner)

- Git root: `~/Projects/panero` (owner's existing folder).
- Branch `main` starts at upstream tag `v2.8.1` (`dbc4540bc`) with full
  upstream history; remote `upstream` = invent.kde.org. Local `master` was
  removed to avoid confusion; `upstream/master` remains as remote-tracking.
- Panero development docs live in `dev/` (plan, log, decisions, environment,
  questions, scripts, handoff copy). Agent entry point: `AGENTS.md`
  (`CLAUDE.md` imports it).
- Feature work happens on `feature/*` branches merged via PRs in
  `NorbCraft/panero`.

## D-005 — Tests (2026-11-17, accepted)

- Upstream v2.8.1 contains no autotests (`autotests/` exists only on newer
  upstream branches). Panero adds its own QtTest-based tests under
  `autotests/` (ECM `ecm_add_test`), run headless under Xvfb/offscreen in CI.

## D-006 — Test isolation (2026-11-17, accepted by owner)

- Docker `ubuntu:24.04` containers for fast package build/install/upgrade/
  removal iterations; libvirt VM (Ubuntu 24.04 cloud image) for a clean GUI
  install test; the owner's Zorin 18.1 host (Wayland) for real-desktop GUI
  tests, with temporary `HOME`/`XDG_CONFIG_HOME` so real config is untouched.
  X11 covered via XWayland + Xvfb only.
