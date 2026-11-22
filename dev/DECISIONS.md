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

## D-016 — Safe cloud sign-in (2026-11-21, owner: "implement R1 to R10")

Supersedes the rclone parts of D-014 (C3). Details and reasons:
`dev/CLOUD-SECURITY-AUDIT.md`.

- R1: Panero ships its own rclone (upstream release binary, version and
  SHA-256 pinned in `dev/rclone/`, signature checked when pinning) as
  `libexec/panero/rclone`; the distribution's rclone is never used; versions
  below 1.75.2 are refused (CVE-2026-116194). Weekly `rclone-watch` workflow.
  Owner did not pick between "bundle" and "download on demand"; the
  recommended bundle was taken.
- R2: Panero's own rclone configuration `~/.config/panero/cloud/rclone.conf`
  (0700/0600), always encrypted with rclone's config encryption. The key is 32
  random bytes in the desktop keyring (Secret Service, own minimal D-Bus client,
  no new library) and reaches rclone only via `--password-command
  panero-cloud-key`. No keyring -> no cloud sign-in (never a plain fallback).
- R3: no secret in argv or environment (WebDAV password via stdin); inherited
  RCLONE_* variables are removed.
- R4: callback port checks (127.0.0.1 and ::1), [::1]:53682 held and forwarded
  during sign-in, only rclone's loopback auth URL is opened, pCloud host
  allowlist, access explained before sign-in, read-only option (Drive,
  OneDrive), OneDrive limited to own files by default.
- R5: Remove = revoke (Google, Dropbox, Box, pCloud) or the provider page
  (Microsoft, Nextcloud), then delete remote and cache; Sign Out of All Clouds.
- R6: per-remote cache below `~/.cache/panero/cloud/`; secrets redacted from
  every rclone message shown.
- R7: MEGA removed from the guided list (still possible via Other services).
  Google Drive: no own-client-ID field yet (rclone's shared client still
  answers); if Google retires it, the user can set up a client in Other
  services or import.
- R8: only Panero's own remotes; import from the user's plain rclone config
  (assembled and encrypted in $XDG_RUNTIME_DIR); Other services = rclone's
  interactive setup on the encrypted configuration.
- R10: offline files (VFS cache with 30-day age and size limit; Keep on This
  Device = bisync to `~/Cloud Offline/<remote>/`; Free Up Space). Offline
  copies are deleted on sign-out by default (setting per cloud).

## D-017 — Smooth cloud browsing (2026-11-22, preview 9, owner: "never freeze", "smooth like OneDrive on Windows")

- Whole folder tree read after mounting through rclone's remote control
  (`vfs/refresh recursive=true`, one call per top folder so one unlistable
  folder such as OneDrive's Personal Vault cannot stop it), listings kept
  (`--dir-cache-time` 168h with polling backends, 24h others); the shown
  folder is revalidated in the background (`vfs/refresh dir=`) on open and
  every 30 s; Ctrl+R re-reads one folder (SIGHUP only as fallback).
- rc security: unix socket only, in `$XDG_RUNTIME_DIR/panero-cloud/.control`
  (0700), random 128-bit user and password per mount, rclone gets only an
  htpasswd `{SHA}` hash in a 0600 file. Not `--rc-no-auth` (would expose
  config/operations methods to any same-user process without a secret), not
  RCLONE_RC_USER/PASS (D-016 R3: no secret in an environment).
- Cloud/network folders are listed in a worker thread with a nested event
  loop, as Krusader already does for KIO listings; a new navigation kills the
  listing job (`PaneroListJob`, a KIO::Job so the panel's existing progress and
  cancel handling apply).
- Browsing root: the top folder of Panero's own mounts is `isRoot()` (no "..",
  Up stops). The path bar starts with the drive name through a place
  (KFilePlacesModel, `OnlyInApp=panero`) added while the drive is mounted and
  removed on unmount and at the next start (stale ones). This writes
  `~/.local/share/user-places.xbel`, the desktop-wide places file; the entries
  are hidden from other applications. Alternative (own breadcrumb widget) was
  far more work.
- Google Drive own client ID: supersedes D-016 R7's "no own-client-ID field".
  Measured: rclone's shared client gets 403 rateLimitExceeded and needs
  17–35 s per listing. Client ID on the command line (public); client secret
  only through a short-lived `rclone rcd` on a private socket (`config/update`
  with `config_refresh_token=false`), then `config update <r> client_id=...`
  signs in with the stored client.
- Rename = Panero's display label (existing "Remote Labels"), not the rclone
  remote name (mount folder, cache, offline copies and bisync state would move).

## D-018 — No drive access from the GUI thread on cloud drives (2026-11-22, preview 10, owner report: "panero just froze and I had to force quit it")

- Rule: on a cloud/network mount (`PaneroSlowPaths::isSlowPath`) the GUI
  thread does not touch the file system. While rclone is busy (a file being
  opened for another program, a folder being re-read from the provider under
  rclone's directory lock) any stat/chdir/inotify there waits, and the window
  freezes. Found with a stopped rclone (gdb: `chdir` in
  `ListPanelFunc::doRefresh`; `KFileItem::iconName` in the tree) and strace
  stacks of a normal session (163 GUI-thread accesses).
- Working directory: `PaneroSlowPaths::setCurrentDir` uses the home folder on
  slow paths (doRefresh, KCMDLine). Commands get their folder explicitly
  (runCommand, command line `startpath()`), so nothing relied on it; it also
  stops Panero's own working directory from keeping the mount busy.
- No KDirWatch on cloud folders (supersedes D-017's implicit watcher):
  `addDir(WatchFiles)` resolves the path and stats every file in the GUI
  thread. `hasAutoUpdate()` is then false, so Panero's own file operations
  refresh through `fileSystemChanged`; external changes come from the
  background check (worker thread).
- No `.directory` custom folder icons on slow paths (FileItem::getMime).
- Folder tree: KrDirModel answers icon, tooltip, child count, type and
  hasChildren from memory on slow paths; the tree's KDirLister runs without
  auto-update there. Left: KCoreDirLister resolves the folder (realpath) and
  checks `.hidden` once per listed folder, inside KIO, after its listing
  arrived (so normally answered from rclone's fresh cache). An own model for
  cloud trees would remove it; not worth it now.
- Revalidation (supersedes part of D-017): no forced `vfs/refresh` for
  providers rclone polls (drive, onedrive, dropbox, box). A non-recursive
  refresh holds rclone's directory lock for the whole network listing, so
  everything in that folder waited every 30 s (Google: 17–35 s). Changes there
  arrive by polling and the worker-thread signature check. Ctrl+R still
  forces a re-read; other providers keep the throttled background refresh.
- Regression check: `dev/scripts/gui-cloud-freeze-test.sh` (stopped rclone,
  /proc wchan of the main thread after each step, gdb stack on a freeze).
