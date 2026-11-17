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
- [!] Owner: `sudo bash dev/scripts/bootstrap-host.sh`, then re-login and
      restart VS Code (docker group).
- [!] Owner: `gh auth login ... --scopes workflow` + `gh auth setup-git`.
- [ ] Verify toolchain (`cmake`, `g++`, `gh auth status`, `docker ps`).
- [ ] Create public `NorbCraft/panero`, push `main` + upstream tags; enable
      issues; confirm Actions enabled.

## Phase 1 — Baseline

- [ ] Baseline build of v2.8.1 unchanged; run existing autotests; record
      exact commands and versions in ENVIRONMENT.md.
- [ ] Minimal CI workflow (build + tests in ubuntu:24.04 container).
- [ ] License/provenance audit of the base tree (LICENSES/, SPDX headers,
      icons, docs, plugins) → `dev/LICENSE-AUDIT.md` (draft).

## Phase 1b — Backports from upstream 2.9.0 / master (D-007)

- [ ] Review `git log v2.8.1..v2.9.0` and `v2.9.0..upstream/master`; list
      candidate functional fixes in `dev/BACKPORTS.md` (applied / skipped + why).
- [ ] Apply KF5-compatible fixes as individual commits citing upstream hashes.

## Phase 2 — Identity / rebrand (§3.5–3.6, §4)

- [ ] Executable `panero`, desktop file, AppStream metainfo, reverse-DNS id
      derived from owner (e.g. `io.github.norbcraft.panero`).
- [ ] Separate config (`panerorc`), data, cache, D-Bus/single-instance ids;
      coexistence with installed Krusader verified.
- [ ] Optional, explicit "Import Krusader settings" (copy, never modify).
- [ ] About dialog: fork statement, upstream credits, source/credits links,
      bug link → this repo. README rewrite with fork statement.
- [ ] Icon A redrawn as original SVG + PNG sizes 16–256; provenance doc.
- [ ] Record PANERO name note (existing third-party uses; no clearance claim).

## Phase 3 — Commander input model (§7)

- [ ] Separate current row vs. mark set; Commander profile default.
- [ ] Current row: strong blue treatment; marks: red + check indicator.
- [ ] Short right-click toggles mark; Space toggles; Insert preserved.
- [ ] Left-click / arrows / focus / panel switch keep marks; Clear Marks action;
      Escape does not clear marks.
- [ ] Marks keyed by stable identity across sort/refresh/view changes;
      pruned on disappearance; not carried across directories.
- [ ] Hold right-click (~600 ms, configurable) → native Krusader context menu
      (marked set / item / background rules); no double trigger; drag threshold,
      cancel, focus loss handled.
- [ ] Header short right-click → native column chooser immediately.
- [ ] Menu key / Shift+F10.
- [ ] Unit/GUI tests for event sequencing and target selection.

## Phase 4 — Views & appearance (§6)

- [ ] Extension column (dirs, hidden, no-ext, multi-suffix) without name
      duplication.
- [ ] Alternating rows, configurable stripe colors (light/dark).
- [ ] Row height 18–48 px, compact preset, comfortable default, HiDPI.
- [ ] Thumbnails/Details toggle using real KIO previews (async, bounded).
- [ ] Colored icon fallbacks working on Zorin (non-KDE); declared deps.
- [ ] Hidden-files toggle; "View & Colors" dialog with reset; command line
      hidden by default.
- [ ] Light/dark support without touching global theme.

## Phase 5 — Toolbar, drives, tree, pins (§5)

- [ ] Default toolbar: Up, Home, Refresh, Compare, Pack, Unpack, Search, Trash
      (opens `trash:/`), Hidden, Thumbnails/Details, Tree, View & Colors.
      Copy/Move/New Folder/Sync removed from default toolbar only.
- [ ] Per-panel drive selector (Solid/KIO), graceful missing/unmounted.
- [ ] Left folder-tree sidebar following the active panel, lazy, non-blocking.
- [ ] Pinned favorites with persistence and recoverable errors.

## Phase 6 — KDiff3 compare (§9)

- [ ] Compare by Content defaults to KDiff3; file-from-each-panel and two
      marked files; folder compare where supported.
- [ ] Detection + configured path; safe argv launch.
- [ ] Missing → "Install KDiff3?" via PackageKit/polkit; progress, cancel,
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
