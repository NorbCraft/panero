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

## D-007 — Base confirmed: v2.8.1 + backported upstream fixes (2026-11-17, owner decision)

- Owner asked to start from the latest upstream (2.9.0 stable / 2.10.0-dev
  master per krusader.org). Re-checked: v2.9.0 requires Qt ≥ 6.4 + KF6
  ≥ 5.240; master requires Qt 6.7 + KF6 6.5 ("Increase these minimum required
  versions: Qt to 6.7 and KF to 6.5"). Ubuntu 24.04/Zorin 18.1 has Qt 6.4.2
  and **no KF6 packages at all** (`apt-cache search ^libkf6` → 0).
- 2.9.0's changelog is the KF6 port plus ~10 fixes; master adds mostly
  cleanups plus Synchronizer sorting, kio_iso fixes, crash/division fixes.
- Options presented: (1) 2.8.1 + backports, native distro Qt5/KF5;
  (2) 2.9.0 with a bundled KF6 (+Qt) stack; (3) master with bundled Qt 6.7 + KF6.
  **Owner chose (1).**
- Follow-up: backport functional fixes from `v2.8.1..v2.9.0` and
  `v2.9.0..upstream/master` that apply to KF5 (tracked in PLAN Phase 1b and
  `dev/BACKPORTS.md`), each as a separate commit referencing the upstream
  commit hash.

## D-008 — Rebranding scope for strings and catalogs (2026-11-17, accepted)

- User-visible translatable strings say "Panero"; the same substitution is
  applied to msgid/msgstr in `po/*/panero.po` (renamed from krusader.po) so
  translations keep working. Kept: credits, "Krusader Krew", upstream behaviour
  names ("Krusader Mode", "Krusader" sort method), the fork statement.
- Internal identifiers stay (class names, action names like `root krusader`,
  config keys, `krarc:` protocol) for compatibility with imported settings and
  to keep upstream merges feasible.
- Handbook content stays upstream (describes Krusader 2.8); it is installed as
  `help:/panero`. Translated man pages are not shipped.
- Known limitation: transliterated or inflected brand names in some
  translations may still read "Krusader".

## D-009 — Commander input model (2026-11-17, accepted)

- New selection mode 5 "Panero Commander Mode", default for new profiles;
  the upstream modes stay selectable (Configure > Panel > Mouse).
- Cursor (current row) and marks are independent: left click / arrows /
  focus / panel switches never clear marks. Short right click, Space, Insert
  toggle; Ctrl+click toggles, Shift+click marks a range (upstream behaviour).
  Space does not calculate folder size in this mode (avoids blocking on large
  or remote folders); the custom mode can enable it.
- Right button state machine (`CommanderGesture`, unit tested): toggle on
  release of a short click; hold ≥ "Context Menu Hold Delay" (default 600 ms,
  200–3000) opens the native Krusader context menu without toggling; moving
  beyond the drag distance becomes a sweep that marks/unmarks the range (as in
  Total Commander) and never opens the menu; focus loss / window deactivation /
  hide / drag cancel the gesture.
- Menu targets: marked set if any marks exist (also when the held row itself is
  unmarked — the spec's "applicable marked set"; the menu then starts with a
  disabled "Marked: N items" line so the target is explicit), otherwise the
  item under the pointer. Empty space or ".." → folder background menu even
  when marks exist.
- Keyboard: Menu key and Shift+F10 open the context menu. Upstream bound
  Shift+F10 to the "All Files" view filter; that filter stays in the View
  menu without a default shortcut.
- "Unselect All" (Alt+Minus) is labelled "Clear All Marks".

## D-010 — Packaging and release mechanics (2026-11-17, accepted)

- Debian source format `3.0 (quilt)` with `debian/` in this repository (no
  patches): D-003 versions carry a Debian revision, which `3.0 (native)`
  does not allow. One binary package `panero` (amd64); no `-data` split.
- Release builds run `dev/scripts/build-deb.sh` (clean `ubuntu:24.04`
  container, `dpkg-buildpackage -us -uc -b`, ctest, lintian) and
  `dev/scripts/test-deb.sh`, locally and in `.github/workflows/release.yml`.
- GitHub release asset name: GitHub replaces `~` in asset file names with
  `.`, so the workflow uploads `panero_0.1.0~preview.1-1_amd64.deb` as
  `panero_0.1.0.preview.1-1_amd64.deb` and uses that name in `SHA256SUMS`
  and the install command. The package itself (control `Version:`) stays
  `0.1.0~preview.1-1`. Supersedes the asset-name sentence of D-003.
- Releases are prereleases while the version contains "preview" (also
  alpha/beta/rc). Assets: the `.deb`, `panero-<version>-source.tar.gz`
  (`git archive` of the tagged commit + `SOURCE-INFO.txt` with the commit
  hash and the dated change list since Krusader 2.8.1),
  `panero-<version>-licenses.tar.gz`, `SHA256SUMS`; notes from
  `dev/release-notes/<tag>.md` plus generated build details.

## D-011 — kde-cli-tools only suggested (2026-11-17, accepted)

- kde-cli-tools (kdesu, used only by "Start Root Mode Panero" and root user
  actions) recommends systemsettings; apt installs recommends recursively, so
  recommending it would add a "KDE System Settings" launcher to a Zorin/GNOME
  menu just for a rarely used, discouraged feature. It is now in Suggests; the
  root mode action already tells the user to install kde-cli-tools if kdesu is
  missing.

## D-012 — Toolbar and sidebar refinements (2026-11-18, owner request)

- Folder tree start: only "Start at Home" (upstream "start from place": Home, or
  the drive / "/" when outside Home) and "Start at the Whole File System (/)";
  upstream "start from current folder" is not offered (old setting migrates to Home).
- Toolbar Trash is a menu: Open Trash / Empty Trash (upstream confirmation kept).
- Sidebar toggle is the first toolbar item, icon-only (`sidebar-show-symbolic`,
  present in Zorin and Breeze themes), checked while the sidebar is shown.
- Details/Thumbnails: one segmented control (both halves visible, active one
  highlighted); any click switches the active panel to the other view.
- Pinned and Folders share a vertical splitter (state saved); folders can be
  dropped on the pinned list.

## D-013 — Preview 3 look and toolbar (2026-11-19, owner decision from mockups)

Supersedes the toolbar parts of D-012. Mockups: real-app screenshots (dev/mockups,
dev/scripts/make-mockups.sh).

- One toolbar, icons only, 22 px; appearance button at the right edge; the
  Details/Thumbnails control is icon-only and follows the toolbar icon size.
- Sidebar button: option S1, the plain icon button with the preview 1 icon
  (`view-list-tree`), twice the width of a normal toolbar button. The "header"
  variant (S2) is not shipped.
- General look: option B "Modern" is the Panero look ("Use Panero's look"):
  panels as rounded cards (active one outlined), layout `panero:modern` (drive
  chip + path in one row, totals + free space in the bottom row), compact tabs,
  muted headers, flat function-key bar; the menu bar is hidden by default and a
  ☰ menu button (KHamburgerMenu) at the end of the toolbar offers all menus
  (Settings > Show Menubar brings it back). Without "Use Panero's look" Panero
  looks like Krusader (system style, no style sheet).
- The sidebar's "Opens in: … panel" label is removed (the active panel is visible).
- A stale local `paneroui.rc` from an older layout is backed up and its toolbars
  dropped on upgrade (shortcuts kept).

## D-014 — Preview 4 decisions (2026-11-20, owner, from the proposal page)

- H1: right-press feedback is a pie below-right of the pointer (Look&Feel /
  Hold Indicator; pie default, ring/bar/none remain options).
- N1: the cogwheel button and its dialog are called "Quick Settings"; the ☰
  button is "Menu"; Quick Settings offers menu bar or Menu button.
- I1: Quick Settings > Icons: System (default) or "Panero Line" (bundled
  Lucide icons, ISC). Plus a folder colour option (recoloured folder icons).
- C2 + C3: "Connect to Server…" (WebDAV/SMB/SFTP/FTP via KIO) and "Add Cloud…"
  with rclone (installed on demand via PackageKit like KDiff3; mounted with
  `rclone mount`). Both appear in a sidebar "Cloud" section that can be
  switched on/off in Quick Settings. C1 (GNOME Online Accounts) not chosen.
- R6: copy/move progress in the panel's bottom row (cancel; details on click).
- R9: context menu "Open Terminal Here" (Copy Path from the owner's list too).
- R11: tab bar only with two or more tabs; a new-tab button next to the path.
- R12: window title "<folder> — Panero".
- Theme change asks to restart (live switch is unreliable, see LOG 2026-11-20).
