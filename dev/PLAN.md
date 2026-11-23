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
- [x] License/provenance audit of the base tree (LICENSES/, SPDX headers,
      icons, docs, plugins) → `LICENSING.md` + `dev/PROVENANCE.md`
      (instead of `dev/LICENSE-AUDIT.md`); `reuse lint` compliant.

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

- [x] Debian packaging → `panero_0.1.0~preview.1-1_amd64.deb` (`debian/`,
      `dev/scripts/build-deb.sh`, D-010); lintian clean apart from justified
      overrides; coexists with `krusader` (no shared files, both orders).
- [x] CI: build+test on push/PR (`ci.yml`).
- [~] Release workflow on tags (`release.yml`, minimal perms, SHA-pinned
      actions, `gh release create`): written and statically checked; first
      real run happens with the first `v*` tag.
- [~] Source archive (+ `SOURCE-INFO.txt`), licenses archive, SHA256SUMS:
      generated by `release.yml`; `LICENSING.md`, `dev/PROVENANCE.md`,
      DEP-5 `debian/copyright` done. Still to write:
      `dev/release-notes/v0.1.0-preview.1.md`.
- [x] Clean-container install/coexistence/upgrade/remove/purge tests
      (`dev/scripts/test-deb.sh`, results in `dev/TEST-REPORT-packaging.md`).
- [ ] Install the .deb on the Zorin 18.1 host / VM and test the desktop
      launcher and GUI (container tests are not a desktop test).
- [ ] Backport upstream kio_iso fixes 4c85272cd + c6945e68e (ISO images do
      not open on noble; found by the package test, distro krusader same).
- [x] Publish `v0.1.0-preview.1` prerelease; verify downloads + checksums.

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

## Status notes (2026-11-17, 21:10)

- main: identity, commander input, appearance/thumbnails/sidebar/KDiff3,
  packaging + release workflow (PRs #1–#4).
- PR #5 feature/release-prep: trash restore fix, packer detection fix,
  kde-cli-tools → Suggests, GUI/container test scripts, release notes draft.
- feature/backports: agent in progress (incl. kio_iso fix).
- Remaining before release: merge #5 and backports; final .deb build + test-deb;
  container screenshots; host install check by the owner (sudo); tag
  v0.1.0-preview.1 → release workflow; verify published assets/checksums.

## Status notes (2026-11-17, 21:50)

- All feature/packaging/backport PRs (#1–#6) merged; main CI green.
- Release candidate verified (see dev/TEST-REPORT.md, dev/TEST-REPORT-packaging.md).
- In progress: tag v0.1.0-preview.1 and publish via .github/workflows/release.yml.
- After release: owner host install check (sudo needed), then update release notes
  if anything is found.

## Status notes (2026-11-18)

- v0.1.0-preview.1 published (prerelease): https://github.com/NorbCraft/panero/releases/tag/v0.1.0-preview.1
  Assets verified (checksums, commit, clean-container install + coexistence).
- Open: owner installs the .deb on the Zorin host (sudo) → check launcher/icon,
  coexistence with the installed Krusader, first-run import offer; then update
  release notes/test report. Owner-decision backports listed in dev/BACKPORTS.md.

## Status notes (2026-11-18, preview 2)

- v0.1.0-preview.2 published with the D-012 sidebar/toolbar refinements.
- Open: owner host install check; owner-decision backports (dev/BACKPORTS.md).

## Phase 10 — Preview 3 (owner requests 2026-11-19)

Step 1 is mockups for the owner to choose from; implementation starts after the
owner decides. Mockups must be screenshots of the real app (prototype styles),
not drawings: the owner wants only what can really be built.

Owner-found issue on upgrade: KXMLGUI kept the preview 1 toolbars from
`~/.local/share/kxmlgui5/panero/paneroui.rc` (saved after a toolbar setting
change). Fix: on a gui version bump, back up the local file and drop its old
`<ToolBar>` layout, keeping shortcuts (ActionProperties).

Toolbar
- [x] One toolbar for all buttons (one resize, no hidden separations); default
      icons only, medium size (22 px); appearance button pinned to the right.
- [x] Details/Thumbnails control: icon only, follows the toolbar icon size.
- [x] Sidebar button: revert to the preview 1 icon, or the owner-chosen embedded
      variant (mockup).
- [x] KXMLGUI stale-layout migration (see above).

Folder tree
- [x] "Start at Home" must be reliable (it sometimes follows the current folder).
- [x] Click on a folder opens it and expands it if it has subfolders; a second
      click on the same folder collapses it.
- [x] Defaults: icon size 22, row height 24 px.

Zoom
- [x] Ctrl+wheel in Details does not change rows/icons; zooming in switches to
      Thumbnails and keeps zooming; zooming fully out returns to Details. The
      view toggle button follows.

Colours
- [x] Look options: configurable active panel colour (also the tree's selected
      folder colour); default is the Krusader colour seen with "Use Panero's
      look" off.

Pins
- [x] A pin stays highlighted only while that exact folder is shown; moving
      anywhere else (also a subfolder) clears it.

Keyboard / selection
- [x] Space calculates and shows the folder size (Krusader/Total Commander
      behaviour) as well as marking.
- [x] Option to choose the selection model: Panero click-select (default) or the
      original Krusader style.

Modern look
- [x] Mockups of a more modern general look (several options, real screenshots).
- [x] Implement the owner-chosen option.

Owner decisions (D-013): sidebar button S1 at double width; look B "Modern"
with the ☰ menu button; remove the sidebar's "Opens in: … panel" label.

- [x] Sidebar button S1, double width; drop the header variant.
- [x] Look B built in (style sheet generated from the Panero palette/accent,
      only with "Use Panero's look"); default layout panero:modern; compact tabs;
      menu bar hidden by default + ☰ button.
- [x] Remove the "Opens in" label.

Release
- [x] Plan update with owner decisions.
- [x] Implement, tests (ctest, GUI file-ops, test-deb), screenshots/user guide,
      release notes, v0.1.0-preview.3 (PR #8, release run 44598245833).

## Status notes (2026-11-19, preview 3)

- v0.1.0-preview.3 published: https://github.com/NorbCraft/panero/releases/tag/v0.1.0-preview.3
- Open: owner host install of preview 3 (sudo) and feedback on the Modern look;
  owner-decision backports (dev/BACKPORTS.md).

## Phase 11 — Preview 4 (owner requests 2026-11-20)

Step 1: proposals with visuals (owner decides), then implementation.

Fixes / changes requested directly
- [x] Details | Thumbnails frame rounded; both icons equal size also with text under icons.
- [x] Window title shows "Panero".
- [x] Sidebar button same height as the other buttons when text is shown under icons.
- [x] Context menu: Copy Path.
- [x] Theme switch must work without restart (easy fix) or ask to restart (Restart / Cancel).
- [x] Menu button text "Menu"; option in Panero's settings: menu bar or menu button (Alt navigation).
- [x] F7 new folder: cursor lands on the new folder, keyboard focus in the panel (not the F-key bar).
- [x] Toolbar move handles clearly visible (light look).
- [x] Inline rename editor integrated with the row look.
- [x] F9 Term error.
- [x] Remove the empty toolbar.
- [x] No check badge on marked files (default off).
- [x] New tab: "malformed URL /." error.
- [x] Cursor row fill: "transparent" option (frame only).

Proposals (done, page https://claude.ai/artifact/4ZhX9vcCC9vc657gKXKhcL) —
owner decisions D-014:
- [x] H1 pie hold feedback (default) + setting.
- [x] N1 "Quick Settings" (gear), "Menu" button, menu bar / Menu button option.
- [x] I1 icons: System / Panero Line (bundled Lucide); folder colour option.
- [x] C2 Connect to Server; C3 rclone Add Cloud (install on demand, mount);
      sidebar Cloud section, switchable.
- [x] R6 progress in the panel bottom row.
- [x] R9 Open Terminal Here.
- [x] R11 tab bar only with 2+ tabs, new-tab button next to the path.
- [x] R12 window title "<folder> — Panero".
- [x] Docs, tests, release v0.1.0-preview.4 (PR #9, release run 44802311531).

## Status notes (2026-11-20, preview 4)

- v0.1.0-preview.4 published: https://github.com/NorbCraft/panero/releases/tag/v0.1.0-preview.4
- Open: owner host install + real cloud sign-in test (no accounts available to
  the agent); owner-decision backports (dev/BACKPORTS.md).

## Phase 12 — Safe cloud sign-in (owner request 2026-11-21)

Audit and recommendations: dev/CLOUD-SECURITY-AUDIT.md. Waiting for owner
decisions before implementation.

- [x] Audit preview 4 cloud code, rclone 1.60.1/1.76.0, CVE-2026-116194.
- [x] Owner decisions on R1–R10 (owner: implement all; D-016).
- [x] Implement agreed items; automated security tests in CI.
- [x] Independent security review; findings fixed and re-checked.
- [x] Release v0.1.0-preview.5 (PR #10, run 44899039001).
- [x] Preview 6: OneDrive sign-in hang fixed, OneDrive scopes as shown, provider icons.
- [x] Release v0.1.0-preview.6.
- [ ] Tests with disposable accounts (owner signs in in the browser).
- [ ] Owner signs in with real accounts.

## Phase 13 — Smooth cloud browsing (owner request 2026-11-22, preview 9)

Owner: still slow compared with OneDrive on Windows, must never freeze;
folder badge stays "cloud" after Make Available Offline; rename clouds;
navigation must stop at the cloud's top folder.

- [x] Whole folder tree read in the background after mounting (rclone rc on a
      private unix socket, hashed random credentials, vfs/refresh recursive per
      top folder); long --dir-cache-time; Ctrl+R / revisit -> vfs/refresh dir.
- [x] Cloud folders listed in a worker thread (panel stays usable, a new
      navigation cancels the listing); revalidate the shown folder in the
      background and update it when it changed (also every 30 s).
- [x] Parallel prefetch of the next level; folder tree root without statfs.
- [x] Folder badge: green when everything inside is in the cache (computed in
      the background).
- [x] Rename... for clouds (display name).
- [x] Cloud top folder is the root: no "..", Up stops, breadcrumb starts with
      the cloud name (place with OnlyInApp=panero while mounted).
- [x] Google Drive own client ID (shared client measured rate limited).
- [x] Tests, docs, release v0.1.0-preview.9 (PR #14, tag on ef284a136).

## Phase 14 — Cloud freeze fix (owner report 2026-11-22, preview 10)

Owner: "panero just froze and I had to force quit it" (preview 9, right after
opening three PDFs from OneDrive in Evince; journal: a second Panero start
30 s later, scope killed 16:46:51; OneDrive mount left dead, cleaned at the
next mount by isMountPoint()).

- [x] Reproduce with a stopped rclone (SIGSTOP) under Xvfb + gdb: chdir in
      ListPanelFunc::doRefresh, then KFileItem::iconName in the folder tree.
- [x] strace -k of a normal session: 163 GUI-thread accesses to the mount
      (FileItem .directory icons, tree's KDirLister/KDirWatch, context menu).
- [x] Fixes (D-018): setCurrentDir, no KDirWatch on cloud folders, no
      .directory icons there, tree answers from memory / no auto-update, no
      forced vfs/refresh for polling providers. Now 5 accesses left (context
      menu on a file, KIO's tree lister once per folder).
- [x] dev/scripts/gui-cloud-freeze-test.sh (regression check); gui-cloud-test
      no longer leaves its accessibility bus running.
- [x] Tests: ctest 7/7, security 16/16, GUI test 2/2, freeze test passed.
- [x] Release v0.1.0-preview.10 (PR #15, tag on 1099ea025, run 45044078622).

## Phase 15 — Preview 11 (owner plan 2026-11-22; done)

Owner asked to clarify the proposals before implementing. Proposal page with
a decision form was given to the owner (drive buttons, Multi-Rename, Windows).

- [x] Remove Krusader's per-panel sidebar (branch feature/preview11).
- [x] Cloud entry selected only while the active panel is inside it.
- [x] Second "Copy path": the owner's own KDE service menu
      (~/.local/share/kservices5/ServiceMenus/copy-path.desktop), not Panero.
- [x] Owner decisions (D-019): all recommendations taken; plus sort arrow
      ^ ascending / v descending on every platform.
- [x] Sort arrow drawn by KrStyleProxy (panel headers).
- [x] Hide service-menu duplicates of Copy Path (checked in the window).
- [x] Ctrl+M = Multi-Rename, Alt+F1/F2 = drive lists (GNOME owns Alt+F1/F2
      by default; Ctrl+Shift+Left/Right kept).
- [x] Drive buttons (PaneroDriveBar, PaneroDriveFilter + unit test; Windows
      letters coded, compiled after the merge).
- [x] Multi-Rename dialog (rules + unit tests, gui-rename-test.sh).
- [x] Windows in main (feature/windows-main, 73bf937b1): experiment/windows
      merged, cloud not built on Windows, windows.yml on every PR/main push,
      release attaches the ZIP; fixes: window flags (Qt::Window only), pins
      with shell icons, 6 px right margin (outline at the window edge),
      Windows wording (PANERO_TEXT), no first-run dialogs. Windows GUI test
      45/45; dev/windows/screenshots/p11-*.
- [x] Drive bar on Windows: icons created in the GUI thread (shell icons);
      no "?" in Windows dialog title bars.
- [x] Release v0.1.0-preview.11 (.deb + Windows ZIP): PR #16, tag on
      6e82a2bfe, release run 45061474829.
- Preview 12: Explorer right-click menu; Windows cloud via app folders.

## Phase 16 — Preview 12 (owner plan 2026-11-23, D-020; done)

- [x] Drive buttons: "letter only" (Windows) / "icon only" (Linux) option,
      Network button (smb:/ on Linux, network menu on Windows).
- [x] Linux: Win11 icon theme option (bundled at a pinned commit, SHA-256
      checked; coloured places/folder icons at panel sizes).
- [x] Windows (feature/preview12-windows, merged f66d0a8e2): folder icons via
      SHGetFileInfo with fallbacks, sync roots drawn normally; no hydration of
      placeholders (PaneroCloudFiles); Cloud section with auto-detected sync
      roots, pin, badges, Always Keep / Free Up Space; Inno Setup installer
      (per user, upgrade in place) + installer test; GUI test with a real
      Cloud Files sync root.
- [x] Quick Settings: Cloud section checkbox also on Windows.
- [x] Release v0.1.0-preview.12 (.deb, ZIP, setup.exe): PR #18, tag on
      e98c4f8b7, release run 45177457275.
- [x] Explorer right-click menu (D-019, D-021): shell IContextMenu with
      Panero's extras on top (panerowinshellmenu.*), merged from
      feature/explorer-menu (Windows run 45169504569 green).
- Needs the owner's PC: OneDrive names/icons, real download/free-up after
  the pin actions, the icon fix on his C:\, Google Drive detection.

## Phase 17 — Preview 13 (owner decisions D-022)

- [ ] Windows VM over SSH (key ~/.ssh/panero-vm_ed25519 on the host, setup
      script ~/VM-Share/setup-panero-ssh.ps1 run by the owner); MSYS2,
      Python, Inno Setup in the VM.
- [ ] Fixes: drag & drop via the document portal; Cloud section migration
      (Windows); folder ticks from the provider status (Windows); special
      folder icons everywhere; icon theme resets folder colour.
- [ ] Drive right-click (Windows shell menu; Linux Panero drive menu).
- [ ] Selection cleared after operations; app icon bigger; splitter
      double-click 50/50; Back/Forward on the toolbar; right-click menu
      choice; transparent marked background and cursor row; thumbnail frame.
- [ ] Quick View (Ctrl+Q) with images, text/code, folders, archives, PDF,
      audio/video.
- [ ] Copy progress window + toolbar jobs button.
- [ ] Settings window (Panero pages + Advanced) and short Quick Settings.
- [ ] Ideas: undo, folder size on Space, first-start assistant, bookmarks
      into Pinned, Tools menu cleanup, background thumbnails / faster start /
      cloud listing cache.
- [ ] Windows: zip/7z/tar packing and unpacking.
- [ ] Release v0.1.0-preview.13 (Linux + Windows round).
