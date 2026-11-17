# Panero implementation plan

Status legend: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked
(see `dev/OPEN-QUESTIONS.md`). Requirement section numbers (§) refer to
`dev/handoff/PANERO_AGENT_PROMPT.md`.

## Phase 0 — Pre-flight (before any implementation)

- [x] Read handoff prompt, START-HERE and reference notes.
- [x] Inspect host: Zorin OS 18.1 (noble base), x86_64, Wayland/GNOME.
- [x] Check upstream tags and Qt/KF requirements (see DECISIONS D-001).
- [x] Clone upstream with history (`upstream` remote).
- [x] Check GitHub: account `NorbCraft` exists; `NorbCraft/panero` not
      publicly visible (404) — confirm after authentication.
- [x] Owner answers to `dev/OPEN-QUESTIONS.md` (Q1–Q7).
- [x] Repo root moved to `~/Projects/panero`; handoff → `dev/handoff/`.
- [x] Per-repo git identity; `main` branch created at `v2.8.1`.
- [x] Commit handoff material + these dev docs.
- [x] Owner ran bootstrap script (docker group active after re-login; `sg docker` meanwhile).
- [x] Owner authenticated gh (NorbCraft, repo+workflow scopes).
- [x] Verify toolchain (`cmake`, `g++`, `gh auth status`, `docker ps`).
- [x] Create public `NorbCraft/panero`, push `main` + upstream tags; enable
      issues; confirm Actions enabled.

## Phase 1 — Baseline

- [x] Baseline build of v2.8.1 unchanged; run existing autotests; record
      exact commands and versions in ENVIRONMENT.md.
- [x] Minimal CI workflow (build + tests in ubuntu:24.04 container).
- [ ] License/provenance audit of the base tree (LICENSES/, SPDX headers,
      icons, docs, plugins) → `dev/LICENSE-AUDIT.md` (draft).

## Phase 1b — Backports from upstream 2.9.0 / master (D-007)

- [x] Review `git log v2.8.1..v2.9.0` and `v2.9.0..upstream/master`; list
      candidate functional fixes in `dev/BACKPORTS.md` (applied / skipped + why).
- [x] Apply KF5-compatible fixes as individual commits citing upstream hashes
      (branch `feature/backports`, 39 commits; needs `libkf5crash-dev`).
- [ ] Owner decision on the 5 items listed under "Owner decision" in
      `dev/BACKPORTS.md`; then PR + merge `feature/backports`.

## Phase 2 — Identity / rebrand (§3.5–3.6, §4)

- [x] Executable `panero`, desktop file, AppStream metainfo, reverse-DNS id
      derived from owner (e.g. `io.github.norbcraft.panero`).
- [x] Separate config (`panerorc`), data, cache, D-Bus/single-instance ids;
      coexistence with installed Krusader verified.
- [x] Optional, explicit "Import Krusader settings" (copy, never modify).
- [x] About dialog: fork statement, upstream credits, source/credits links,
      bug link → this repo. README rewrite with fork statement.
- [x] Icon A redrawn as original SVG + PNG sizes 16–256; provenance doc.
- [ ] Record PANERO name note (existing third-party uses; no clearance claim).

## Phase 3 — Commander input model (§7)

- [x] Separate current row vs. mark set; Commander profile default.
- [x] Current row: strong blue treatment; marks: red + check indicator.
- [x] Short right-click toggles mark; Space toggles; Insert preserved.
- [x] Left-click / arrows / focus / panel switch keep marks; Clear Marks action;
      Escape does not clear marks.
- [x] Marks keyed by stable identity across sort/refresh/view changes;
      pruned on disappearance; not carried across directories.
- [x] Hold right-click (~600 ms, configurable) → native Krusader context menu
      (marked set / item / background rules); no double trigger; drag threshold,
      cancel, focus loss handled.
- [x] Header short right-click → native column chooser immediately.
- [x] Menu key / Shift+F10.
- [x] Unit/GUI tests for event sequencing and target selection.

## Phase 4 — Views & appearance (§6)

- [x] Extension column (dirs, hidden, no-ext, multi-suffix) without name
      duplication.
- [x] Alternating rows, configurable stripe colors (light/dark).
- [x] Row height 18–48 px, compact preset, comfortable default, HiDPI.
- [x] Thumbnails/Details toggle using real KIO previews (async, bounded).
- [ ] Colored icon fallbacks working on Zorin (non-KDE); declared deps.
- [x] Hidden-files toggle; "View & Colors" dialog with reset; command line
      hidden by default.
- [x] Light/dark support without touching global theme.

## Phase 5 — Toolbar, drives, tree, pins (§5)

- [x] Default toolbar: Up, Home, Refresh, Compare, Pack, Unpack, Search, Trash
      (opens `trash:/`), Hidden, Thumbnails/Details, Tree, View & Colors.
      Copy/Move/New Folder/Sync removed from default toolbar only.
- [x] Per-panel drive selector (Solid/KIO), graceful missing/unmounted.
- [x] Left folder-tree sidebar following the active panel, lazy, non-blocking.
- [x] Pinned favorites with persistence and recoverable errors.

## Phase 6 — KDiff3 compare (§9)

- [x] Compare by Content defaults to KDiff3; file-from-each-panel and two
      marked files; folder compare where supported.
- [x] Detection + configured path; safe argv launch.
- [x] Missing → "Install KDiff3?" via PackageKit/polkit; progress, cancel,
      failure, retry, resume pending comparison.

## Phase 7 — Preserve upstream (§8)

- [ ] Verify all original menus, F-key bar mappings (F10 Quit stays), shortcuts,
      Synchronizer, search, archives, remote protocols, viewer/editor.

## Phase 8 — Packaging, CI, release (§10)

- [ ] Debian packaging → `panero_0.1.0~preview.1-1_amd64.deb`; lintian clean
      as far as practical; coexists with `krusader`.
- [ ] CI: build+test on push/PR; release workflow on tags (minimal perms).
- [ ] Source archive, licenses/third-party notices, SHA256SUMS, release notes.
- [ ] Clean-container install/upgrade/remove tests; host GUI test on Zorin.
- [ ] Publish `v0.1.0-preview.1` prerelease; verify downloads + checksums.

## Phase 9 — Verification & final report (§11)

- [ ] Full verification matrix (`dev/TEST-REPORT.md`), screenshots, user guide.

## Status notes (2026-11-17 evening)

- Merged to main: identity (PR #1), commander input (PR #2).
- Branch `feature/appearance` (pushed): appearance/light-dark, View & Colors,
  thumbnails view, toolbars, navigation sidebar + pins, drive labels, KDiff3
  install flow. Next: PR + merge.
- Branch `feature/packaging` is being produced by a background agent
  (debian/, LICENSING.md, PROVENANCE.md, release workflow, deb tests).
- Branch `feature/backports` (pushed): 39 backport commits, see
  dev/BACKPORTS.md. Next: owner decisions, PR + merge.
- Remaining: Phase 7 review,
  packaging merge, VM/clean-system tests (KDiff3 real install, deb install),
  host Wayland GUI check, screenshots, user guide, release v0.1.0-preview.1.
