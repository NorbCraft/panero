# Work log

Append newest entries at the bottom. Each entry: date, who/what agent, what was
done, outcome, next step.

## 2026-11-17 — Claude Code (Opus 5.5) — pre-flight

- Read `START-HERE.md`, `PANERO_AGENT_PROMPT.md`, `references/REFERENCE-NOTES.md`.
- Host inspection: Zorin OS 18.1 / noble / x86_64 / Wayland GNOME. Distro
  Krusader 2.8.1 installed with a real user config (do not touch).
- Upstream tags: latest `v2.9.0` (KF6), `v2.8.1` (KF5). Noble has no KF6 dev
  packages → proposed base `v2.8.1` (D-001).
- Cloned `https://invent.kde.org/utilities/krusader.git` into
  `~/Projects/panero/panero` with remote name `upstream`
  (checked out `master` @ `fe91f0d47`; no branches created yet).
- GitHub: `NorbCraft` exists; `NorbCraft/panero` and
  `NorbCraft/panero-file-manager` return 404 publicly.
- Found missing tooling (no compiler, cmake, gh, Qt/KF dev packages), no
  passwordless sudo, no docker socket access → owner input needed.
- Created `AGENTS.md`, `CLAUDE.md`, `dev/{PLAN,LOG,DECISIONS,ENVIRONMENT,OPEN-QUESTIONS}.md`
  (not committed yet — git identity not configured).
- Next: owner answers Q1–Q7; then toolchain install, gh auth, baseline build.

## 2026-11-17 — Claude Code (Opus 5.5) — owner answers, repo restructure

- Owner answered Q1–Q7 (see `dev/OPEN-QUESTIONS.md` Resolved).
- Moved the clone so the git root is `~/Projects/panero`;
  moved `panero-handoff/` → `dev/handoff/`; redacted gmail in START-HERE copy.
- `git switch -c main v2.8.1`; deleted local `master`.
- Set per-repo identity (Norbert Szabo, GitHub no-reply e-mail).
- Confirmed v2.8.1 has no autotests (D-005); recorded D-004..D-006.
- Wrote `dev/scripts/bootstrap-host.sh` (all packages verified to have
  candidates in the configured noble/Zorin archives).
- Committed dev docs + handoff on `main` (local only; nothing pushed).
- Waiting on owner: run bootstrap script, re-login, `gh auth login`.

## 2026-11-17 — Claude Code (Opus 5.5) — GitHub access check

- Owner is logged in to GitHub in Firefox. Agent cannot use a browser session
  (no browser-control tool; reading Firefox cookies/credentials is off-limits).
- `gh` not installed yet (bootstrap script not run) → access not yet working.
- Recorded owner's account boundary rule in `AGENTS.md`.
- Waiting on owner: bootstrap script, then `gh auth login --web` (Firefox
  session makes approval a single click + one-time code).

## 2026-11-17 — Claude Code (Opus 5.5) — identity merged, commander input

- Owner asked to base on the latest Krusader; re-checked 2.9.0/master need
  KF6 (+Qt 6.7 for master), unavailable on noble. Owner chose 2.8.1 + backports
  (D-007). Backport analysis running in a background subagent (result to be
  written to dev/BACKPORTS.md).
- feature/identity: identity, coexistence (private KIO plugin dir, verified at
  runtime with distro Krusader installed), icon, settings import + test,
  rebranded strings/catalogs, handbook/man, CI. PR #1 merged (CI green).
- feature/commander-input: Panero Commander mode, CommanderGesture state
  machine + unit tests, background menu, marked-target header, Menu/Shift+F10,
  Panero colour scheme (light/dark defaults, stripes, red marks + check badge,
  blue cursor-row frame with accent bar, grey in inactive panel), no dotted
  focus rect, default window ~1280x800.
- Verified headless (Xvfb + xdotool): left click keeps marks, short right click
  toggles, hold opens native menu w/o toggling, Escape + release no double
  trigger, arrows keep marks.
- Found: katepart missing on a clean system triggers a startup warning → the
  .deb must depend on ktexteditor-katepart.

## 2026-11-17 — Claude Code (Opus 5.5) — appearance, thumbnails, sidebar, KDiff3

- Appearance: PaneroAppearance (Fusion + light/dark Panero palette following
  the GNOME colour scheme via the XDG portal, non-blocking 300 ms call without
  auto-start — an earlier QDBusInterface version stalled startup in headless
  sessions; gsettings fallback). Fixed invisible icons in dark mode: KIconThemes
  sets light Breeze as Qt's fallback theme → now set to the matching Breeze
  variant; Icon lookup prefers Breeze fallbacks over KIconLoader.
- View & Colors dialog; row height 16–48 (never clipping); compact preset.
- Thumbnails view type (QListView icon mode, KIO previews, custom tile paint).
  Verified real previews for JPEG, PNG, PDF, text.
- Toolbars per spec; job toolbar hidden by default; navigation dock (pins +
  KIO tree, follows active panel); drive label on media button.
- KDiff3: install offer via PackageKit D-Bus (enum values verified via
  PackageKitGlib GI); verified real kdiff3 launch and a real PackageKit resolve
  with kdiff3 hidden from PATH.
- Test tooling: xdotool window offsets — `xdotool getwindowgeometry` reports
  0,0 for undecorated Xvfb windows; use dev/scripts/xwin.sh.

## 2026-11-17 — Claude Code (Opus 5.5) — Debian packaging, licensing, release workflow

Branch `feature/packaging` (rebased on `main` `bfa10f64b`).

- Licensing audit with `reuse lint`, `licensecheck -r`, SPDX headers and Git
  history: added `LICENSES/FSFAP.txt` (metainfo) and
  `LICENSES/LGPL-3.0-or-later.txt` (`app/icons/icon-missing.svgz` is
  Breeze-derived per upstream commit 86115e3b6), extended `.reuse/dep5`
  (translations, Krusader artwork/data/build files, packaging) → tree is
  REUSE 3.0 compliant (891/891 files). Wrote `LICENSING.md` and
  `dev/PROVENANCE.md`; open items there (libisofs headers "From the linux
  kernel" may be GPL-2.0-only; Breeze icon status rests on a commit message;
  files without notices attributed to the project license).
- `debian/`: debhelper 13, `dh --buildsystem=cmake+ninja`, RelWithDebInfo,
  hardening, ctest via dh_auto_test, `3.0 (quilt)` (Debian revision, D-010),
  DEP-5 copyright, docs + LICENSES in `/usr/share/doc/panero`, handbook
  docbooks excluded from `dh_compress` (help:/panero), justified lintian
  overrides. Depends kio, kinit, ktexteditor-katepart, breeze-icon-theme +
  shlibs; Recommends kio-extras, kdegraphics-thumbnailers, ffmpegthumbs,
  kimageformat-plugins, kde-cli-tools, packagekit; Suggests kdiff3,
  qt5-gtk-platformtheme and krusader's archiver/tool list. No
  Conflicts/Replaces.
- `dev/scripts/build-deb.sh` (clean ubuntu:24.04 container build, lintian as
  non-root, outputs + BUILD-INFO in dist/) and `dev/scripts/test-deb.sh`
  (install, coexistence both orders, upgrade, remove/purge, KIO workers, GUI
  smoke) — both re-exec under `sg docker` when needed.
- `.github/workflows/release.yml`: tags `v*` + manual dispatch, read-only
  build job (build, ctest, lintian, container tests, `git archive` source
  archive + SOURCE-INFO.txt, licenses archive, SHA256SUMS, notes), publish
  job with `contents: write` only (`gh release create --prerelease` for
  preview versions, re-download + checksum verify). Actions pinned by SHA
  (checkout v7.0.1, upload-artifact v7.0.2, download-artifact v8.0.2).
  GitHub rewrites `~` in asset names to `.` → asset uploaded as
  `panero_0.1.0.preview.1-1_amd64.deb` (D-010).
- Results (`dev/TEST-REPORT-packaging.md`): build from commit 1f417981d,
  ctest 3/3, lintian only overridden tags, container tests PASS 83 /
  FAIL 0 / KNOWN 1 (kio_iso cannot open ISO images on noble — upstream bug,
  same in distro krusader; backport 4c85272cd + c6945e68e).
- Not done: desktop GUI install test on Zorin/VM; release notes file
  `dev/release-notes/v0.1.0-preview.1.md`; first real run of release.yml.

## 2026-11-17 (evening) — Claude Code (Opus 5.5) — packaging merged, verification

- Hit the account's usage limit once (agents and main session paused until
  20:20); resumed. The scratchpad (/tmp) had been cleared meanwhile; nothing in
  the repository was lost.
- PR #3 (appearance, thumbnails, toolbars, sidebar, KDiff3) and PR #4
  (packaging by a worktree agent: debian/, REUSE 891/891, LICENSING.md,
  PROVENANCE.md, build/test scripts, release workflow) merged.
- Backports running in a worktree agent (branch feature/backports); asked it to
  also port the kio_iso QCoreApplication fix (4c85272cd) because ISO images hang
  on noble (found by the packaging tests; distro Krusader has the same bug).
- Verified with real operations: dev/scripts/gui-fileops-test.sh 17/17 (copy,
  overwrite cancel, new folder, rename, move, trash, restore, pack/unpack,
  search). Found and fixed: Restore/Empty Trash missing for trash:/ items
  (upstream bug), empty pack type list without first-run setup.
- KDiff3 install flow in a clean container (own system bus, polkitd,
  packagekitd): allow → kdiff3 installed from the Ubuntu archive and the
  comparison resumed; deny → nothing installed, no error. Broken-sources run
  pending at time of writing.
- kde-cli-tools moved to Suggests (D-011).
- Next: merge backports; rebuild the .deb from final main; screenshots via
  dev/scripts/make-screenshots.sh; ask the owner once to install the .deb on the
  host (needs sudo) for the launcher/coexistence check; tag v0.1.0-preview.1.

## 2026-11-17 — Claude Code (Opus 5.5) — upstream backports (Phase 1b)

- Branch `feature/backports` from origin/main: 39 `Backport:` commits (one per
  upstream commit; the BookMan crash series as one set), each with
  `Upstream:` and `Original-Author:` lines. Full table, skips with reasons and
  the owner-decision list: `dev/BACKPORTS.md`.
- ISO images did not open at all on noble: kio_iso's `kdemain()` had no
  `QCoreApplication`, so the worker crashed in `SlaveBase::SlaveBase()`
  (distro Krusader too), and noble's ISO mime type `application/vnd.efi.iso`
  was not listed. Took the kdemain hunk of 4c85272cd (was on the skip list;
  taken at the coordinator's request) plus c6945e68e; verified with a genisoimage ISO via
  kioclient5 and the staged Panero (`--left=iso:/…`).
- KCrash (03fe3ae39) adds `KF5::Crash`: `libkf5crash-dev` added to
  dev/build-deps.txt, dev/scripts/bootstrap-host.sh and README. The host has
  no sudo, so the build was verified against the noble .deb extracted into the
  scratchpad (`-DKF5Crash_DIR=…`); the owner must
  `sudo apt install libkf5crash-dev` before the next local configure.
- Verified with kioclient5 against the built workers: .tar.bz2 mapping, .jar,
  reading inside a .deb, krarc on single .xz/.bz2/.lzma (crashed before),
  krarc→krarc copy error. Offscreen Panero runs with an isolated HOME:
  SIGTERM/SIGINT/SIGSEGV save panerorc; duplicate bookmarks + reloads, no crash.
- Clean rebuild: no new warnings (45 vs 47 at baseline); ctest 4/4.
- Owner decisions pending (not applied): cfae74772, 3ff36af32, 6713c4132,
  4a0d063a3+c7a4ac114, e78991813. Also note b89d6643d now shows a message when
  Enter is pressed in inline rename without changing the name.

## 2026-11-17 (night) — Claude Code (Opus 5.5) — release candidate

- PR #5 (release prep, trash/pack fixes, test tooling) and PR #6 (39 backports
  incl. kio_iso, rename-message revert, KCrash build dep, thumbnail tile fix,
  sidebar tree default "start from place", README screenshots) merged.
- KDiff3 isolated container tests: allow / deny / broken sources all as designed.
- RC .deb built in a clean container; test-deb: all scenarios pass, ISO check
  passes after correcting the test's ISO path.
- Next: tag v0.1.0-preview.1 → release workflow; verify assets/checksums; owner
  installs the .deb on the host (sudo) for the desktop launcher/coexistence check.

## 2026-11-18 — Claude Code (Opus 5.5) — v0.1.0-preview.1 published

- Tagged v0.1.0-preview.1 (13106ed09); release workflow succeeded and published
  the prerelease with .deb, source and licenses archives, SHA256SUMS.
- Verified the published assets independently (checksums, version, source commit,
  install 57/57 and coexistence 13/13 in fresh containers).
- Waiting on owner: host install with sudo for the desktop launcher/coexistence check.

## 2026-11-18 — Claude Code (Opus 5.5) — owner UI requests, v0.1.0-preview.2

- Owner requests (D-012): tree start Home / whole file system; Trash menu
  (Open / Empty); sidebar toggle at the far left, icon-only; one Details |
  Thumbnails toggle; resizable Pinned / Folders; drop folders onto Pinned.
  PR #7 merged (ctest 4/4, GUI file-ops 17/17, Xvfb checks of each control).
- Released v0.1.0-preview.2 (release workflow incl. upgrade test from preview 1);
  downloaded asset checksum verified.

## 2026-11-19 — preview 3 requests, toolbar fix, look mockups

- Owner host install of preview 2 showed the preview 1 toolbar: KXMLGUI kept the
  toolbars from the local `~/.local/share/kxmlgui5/panero/paneroui.rc` (written
  after a toolbar setting change). Owner fix: move that file away. Product fix:
  `PaneroToolBar::migrateLayout()` (app/panerotoolbar.cpp) backs it up as `.bak`,
  drops its `<ToolBar>` elements (shortcuts stay), forgets the saved toolbar style
  and window state when the layout version (gui version 32) changes.
- Owner preview 3 request list recorded in dev/PLAN.md (Phase 10).
- Toolbar (decided): main and view toolbars merged into one, icons only, 22 px,
  spacer action pushes "View & Colors" to the right; Details/Thumbnails control
  follows the toolbar icon size/style; sidebar icon back to view-list-tree.
- Prototypes for the owner's look decision (all real code, screenshots with
  dev/scripts/make-mockups.sh, styles in dev/mockups/):
  - `PANERO_DEV_STYLESHEET` (dev only) loads a style sheet at startup.
  - Panel layout "panero:modern" (app/layout.xml): drive chip + path in one row,
    totals + free space in the bottom row.
  - Settings > Show Menubar (no shortcut: Ctrl+M stays Media) + KHamburgerMenu
    button at the end of the toolbar (Breeze ☰ icon in the matching variant).
  - Sidebar button action with style "icon" or "header" (Panero Appearance /
    Sidebar Button): the header spans the sidebar width ("Places «" / "»").
  - ListPanelFrame exposes an `active` property for style sheets.
- Safety: dev scripts no longer use broad `pkill -x panero` / gnome-text-edit
  (would hit the owner's real instance now that Panero is installed).
- Mockup page for the owner (private artifact):
  https://claude.ai/artifact/CbNEyX8fJkTx7V4bwPV1bF — decisions S1/S2 and A/B/C.
- Next: owner decision → plan update → implement the rest of Phase 10.

## 2026-11-19 (evening) — preview 3 implementation

- Owner decisions (D-013): sidebar button S1 at double width, look B with the ☰
  menu button, no "Opens in" label.
- Implemented all Phase 10 items (see dev/PLAN.md); notable findings:
  - "Start at Home" was unreliable because upstream's start-from-place uses the
    closest KDE place (Documents, Downloads, ... are places). Now
    PaneroPaths::homeOrDriveRoot (unit-tested).
  - KXMLGUI keeps an older local file's toolbars even when they were stripped;
    the migration now rebuilds the local paneroui.rc from the installed one and
    keeps only ActionProperties (shortcuts). Tested with a preview 1 file plus a
    custom shortcut.
  - Global style sheet: toolbar toggles needed an explicit :checked style.
  - Ctrl+M is upstream's Media shortcut: Show Menubar has no shortcut.
  - System Qt highlight on the owner's Zorin session (no Qt platform theme) is
    Fusion's #308cc6 — the default accent ("Krusader colour").
- Interactive checks under Xvfb (short /tmp runtime dir: long XDG_RUNTIME_DIR
  paths break KIO worker sockets): Space size, tree unfold/fold, Home root in
  ~/Documents/Invoices, pin highlight, zoom + toggle, menu button, dialog
  options, Panero look off, layout migration.
- ctest 5/5, GUI file-ops 17/17 (Trash menu coordinates updated).
- Released v0.1.0-preview.3 (PR #8 merged, tag on ba2685226, release run
  44598245833); assets verified (checksums, version, source commit). The .deb
  was copied to the owner's ~/Downloads for installation.

## 2026-11-20 — preview 4 proposals

- Owner request list recorded in dev/PLAN.md (Phase 11).
- Prototypes (real code, kept): hold indicator (app/Panel/PanelView/
  paneroholdindicator.*, Look&Feel / Hold Indicator = pie|ring|bar|none,
  PANERO_DEV_HOLD_FREEZE for screenshots), Panero Appearance / Icon Theme
  (system or a chosen theme, -Dark variant in dark mode), symbolic icon
  recolouring (PaneroAppearance::symbolicIcon) for the gear (emblem-system-
  symbolic) and the menu button (open-menu-symbolic).
- Finding: live light/dark switch leaves missing toolbar icons and white list
  bases -> restart prompt planned.
- Cloud research: dev/CLOUD-RESEARCH.md.
- Proposal page (private artifact): https://claude.ai/artifact/4ZhX9vcCC9vc657gKXKhcL
  Decisions H (hold), N (settings name), I (icons), C (cloud), R1–R12.
- Next: owner decisions -> plan -> implement all Phase 11 items -> preview 4.

## 2026-11-20 (evening) — preview 4 implementation

- Owner decisions D-014. Implemented Phase 11 (see PLAN). Cloud backend built
  by a helper agent in a worktree (app/Cloud, commit d7985a979 after
  cherry-pick), integrated into the sidebar by me.
- Findings: F9 failed because konsole is the default terminal; the new-tab
  error came from an empty URL reaching PaneroPaths (preview 3 regression);
  F7 focus stayed on a clicked F-key button; KIO progress windows appear
  because no KUiServer runs on GNOME (now HideProgressInfo + panel row).
- Live light/dark switch is unreliable -> restart prompt; restart waits for the
  old process (single-instance safe). Verified under Xvfb.
- Folder colours: recoloured copies of the provider theme's folder icons in a
  generated theme under ~/.cache/panero/icon-themes (PNG pixel / SVG hex
  recolouring, sizes up to 256 px). Panero Line: Lucide 0.460.0 (ISC), installed
  to /usr/share/panero/icons, layered via a generated index.theme.
- Tests: ctest 6/6, GUI file-ops 17/17 (context menu navigation updated for
  Copy Path), interactive checks listed in the release notes.
- Released v0.1.0-preview.4 (PR #9, tag on b0e89cc95, run 44802311531); assets verified; .deb copied to ~/Downloads.

## 2026-11-21 — main back at preview 4; cloud security audit

- Owner was not happy with another agent's work on main. main/origin/main are
  at 62d024dd9 ("dev: preview 4 released and verified"); that agent's commit
  467b264f5 ("Use read-only desktop cloud mounts on Linux") is kept on the
  local branch experiment/linux-desktop-cloud (not pushed, not merged).
  experiment/windows (Windows preview) stays in its worktree, not merged.
- Owner request: audit the rclone cloud sign-in before signing in with real
  accounts (rclone version and distro patches, OAuth permissions/callbacks,
  credential storage, logs/argv exposure, provider-side revocation, encrypted
  config + keyring, pCloud CVE-2026-116194); recommendations first.
- Audit: dev/CLOUD-SECURITY-AUDIT.md. Main points: noble's rclone 1.60.1 is
  affected by CVE-2026-116194 (pCloud OAuth CSRF, fixed upstream in 1.75.2;
  confirmed on cve.org and in the 1.60.1 source) and other 2026 advisories;
  tokens/passwords are plain text in ~/.config/rclone/rclone.conf; MEGA/WebDAV
  obscured passwords appear in world-readable /proc/<pid>/cmdline; "Remove"
  does not revoke the token; the VFS cache keeps file contents after removal;
  Ubuntu disables rclone's mega backend.
- Recommendations R1–R9 (pinned rclone 1.76.0 in the .deb, Panero-owned
  encrypted config unlocked through the keyring via --password-command, no
  secrets in argv, hardened sign-in, real revocation, cache/log hygiene, drop
  guided MEGA) plus R10 offline files ("Files On-Demand"-style, owner request).
- Next: owner decisions on the recommendations, then implementation and tests
  with disposable accounts created by the owner.

## 2026-11-21 (afternoon) — preview 5: safe cloud sign-in (R1–R10)

- Owner: "implement all the requirements, from R1 to R10 and create a new
  preview", then re-inspect and say whether signing in to OneDrive is safe.
  Unanswered choices taken as recommended: rclone bundled in the .deb, MEGA
  removed from the guided list, offline copies deleted on sign-out by default
  (setting per cloud). D-016.
- rclone 1.76.0 pinned: SHA256SUMS signature checked (key FBF737EC…54FA, as on
  rclone.org/release_signing and keyserver.ubuntu.com), equal to GitHub's asset
  digests. Verified against 1.76.0: CVE-2026-116194 fixed (stateless callback ->
  400 and sign-in aborted; foreign pCloud host -> "invalid hostname").
- New code in app/Cloud: panerosecretstore (Secret Service over QtDBus, no new
  library, works without sudo), panerorclone (runner, encrypted store),
  panerocloudkey (helper), panerocloudrevoke (revocation + account identity),
  panerocloudsetup (WebDAV via stdin, import, port checks), panerooffline +
  dialog (offline files). Panel integration: badges, redirect, context menu.
- Tests: panerocloudtest 41 unit tests; panerocloudsecuritytest 15 tests via
  dev/scripts/cloud-security-test.sh (private dbus-run-session + gnome-keyring,
  temp HOME; never the owner's keyring), incl. a /proc scanner with positive
  control, at-rest scan, local WebDAV server, pCloud CSRF regression, mount,
  offline sync, repair, import. CI runs it on every push.
- Independent review (subagent, fresh eyes): no account-takeover path; fixed
  1 high (automatic --resync could overwrite local edits), 2 medium (cache with
  pending uploads deleted; offline folder/settings reused by a new account of
  the same name) and 5 low items; added account-identity confirmation after
  sign-in against a leaked OAuth state on multi-user machines.
- Package: 27.5 MB .deb (rclone 85 MB uncompressed), lintian clean (no E/W),
  test-deb checks the installed rclone is byte-identical to the pinned release.
- Re-review: 2 new medium issues (use-after-free in the upload wait, deletion
  of a folder kept from an earlier account) and 1 gap (plain resync after a
  broken first download) fixed; OneDrive account check shows the account id.
- CI fix: cloud-security-test.sh returned 1 from its cleanup trap.
- Released v0.1.0-preview.5: PR #10, tag on 9efc0eb7f, release run
  44899039001; assets verified (SHA256SUMS, rclone byte-identical to upstream
  1.76.0, licenses archive with rclone); .deb copied to ~/Downloads.
- Re-audit summary: dev/CLOUD-SECURITY-AUDIT.md section 6.

## 2026-11-21 (evening) — preview 6: OneDrive sign-in fix, provider icons

- Owner report: OneDrive sign-in in preview 5 stays at "Waiting for the
  sign-in to finish" after the browser showed rclone's "Success"; the sign-in
  link then fails (helper closed). Asked for real Google Drive / OneDrive icons
  if a fix is needed.
- Cause (reproduced with rclone 1.76 and a fake OAuth server): after the token,
  `config create` answers rclone's own questions with defaults; when OneDrive's
  drive listing fails, the backend returns ConfigError("choose_type") and rclone
  loops forever, printing the error to stdout, which Panero discarded. Likely
  trigger for a real account: OneDrive not provisioned yet, or a Graph error.
- Also found: `disable_site_permission=true` makes rclone replace access_scopes
  by Files.*.All (onedrive.go makeOauthConfig), so preview 5 asked for more
  than the dialog said. Removed.
- Fix: SetupOutputWatcher reads rclone's stdout (failure lines only, buffer
  wiped), stops after a repeated failure and shows the redacted message + a
  OneDrive hint; 3-minute limit after "Got code"; status "Signed in. Finishing
  the setup..."; ensureOneDriveMainDrive (/me/drive) after setup.
- Icons: own SVG drawings of the Google Drive and OneDrive marks
  (app/Cloud/icons, Qt resource), used in Add Cloud and the sidebar.
- Tests: new oauthSetupFinishesOrStops (fake OAuth server + fake browser:
  Drive completes, OneDrive loop detected); 42 unit / 16 security tests pass.
- Owner: keep the icons of the other clouds; only Google Drive and OneDrive
  change (sidebar keeps Panero's folder-cloud icon for the rest).
- Released v0.1.0-preview.6: PR #11, tag on 3e1b45dd9; assets verified
  (SHA256SUMS, bundled rclone unchanged 1.76.0); .deb copied to ~/Downloads.

## 2026-11-22 — preview 7: OneDrive "ObjectHandle is Invalid"

- Owner's preview 6 test showed the real error: after sign-in, rclone's wizard
  picked the first drive from /me/drives ("b!…") and GET /drives/{id}/root
  failed with 400 invalidRequest "ObjectHandle is Invalid". Known Microsoft
  change for some personal accounts (forum.rclone.org/t/54170: /me/drive works,
  set the drive id manually).
- Verified with rclone 1.76 + fake OAuth: the token is already in the config
  when the drive step loops, and `config update <r> drive_id=… drive_type=…
  config_refresh_token=false --non-interactive` stores values without running
  the wizard. Preview 6's ensureOneDriveMainDrive used a plain `config update`
  (would loop): fixed via PaneroCloudRevoke::setOneDriveDrive.
- SignInDialog: a stuck OneDrive setup after "Got code" is handed back for
  recovery (main drive from /me/drive), else revoke + delete with both errors.
- Tests: 42 unit, 16 security (new: drive set without loop after the stuck
  setup).
- Released v0.1.0-preview.7: PR #12, tag on 9775192b8; assets verified;
  .deb copied to ~/Downloads.

## 2026-11-22 (midday) — preview 8: fast cloud browsing, offline per file

- Owner: browsing is laggy; wants Make Available Offline / Free Up Space for
  files and folders, explanation of 0 B Google files, a progress window after
  adding a drive. Tests allowed only in <drive>:test-panero, files removed after.
- Measured (read-only, live mounts): Google root 8.6 s first / 0.45 s again,
  statfs 0.3 s each; fresh rclone listing 0.7 s. Panero on a temp read-only
  mount opened 48 files (QMimeDatabase content sniffing) per listing -> each
  open starts a download in --vfs-cache-mode full.
- Fixes: extension-only MIME on cloud/gvfs paths (app/FileSystem/
  paneroslowpaths), cached async free space, prefetch of the next level,
  --dir-cache-time 1h + --poll-interval 30s for notifying backends (5m others),
  --vfs-fast-fingerprint, Ctrl+R -> SIGHUP flush, badge state cache.
  After: 0 file opens while listing.
- 0 B Google files: native Docs/Sheets/Slides (rclone "Limitations of Google
  Docs": size unknown, unreadable via mount). Now opened in the browser
  (lsjson --metadata content-type + ID), Save a Copy via rclone copyto.
- Offline per file/folder: Make Available Offline (background read through
  the mount into the VFS cache, progress), Free Up Space (remove non-dirty cache
  files; rclone re-downloads, vfscache item.open checks existence).
- Progress windows: Add Cloud setup steps, "Connecting to ..." on mount;
  PaneroRclone::run keeps the GUI painted.
- Real-drive test (GoogleDrive + OneDrive, test-panero only): upload, free-up,
  identical read-back; cleaned up without trash; folder removed.
- rclone warns: the shared Google Drive client id is being retired during 2026
  -> owner decision needed (own client id).
- Released v0.1.0-preview.8: PR #13, tag on 36b51f366; assets verified; .deb copied to ~/Downloads.

## 2026-11-22 (afternoon) — preview 9: smooth cloud browsing

- Owner: still slower than OneDrive on Windows; never freeze; folder badge;
  rename clouds; navigation must not leave the cloud's top folder.
- Checked rclone 1.76 (vfs/dir.go): readDirTree walks without holding the
  directory lock, so a background recursive refresh does not block browsing;
  vfs/refresh dir=X forces a re-read (tested on a local mount); rc methods
  need auth -> random user/pass via RCLONE_RC_USER/PASS environment, socket in
  the 0700 mount base folder.
- Measured read-only on the owner's drives (separate --read-only mount with
  the new flags, own cache in the scratch folder, removed afterwards):
  Google Drive (7 folders, 240 items) whole tree 33 s, single folder 17–35 s:
  rclone -vv shows Google 403 "Quota exceeded ... rateLimitExceeded" for
  rclone's shared client id -> root cause of the Google slowness; own client
  id support added. OneDrive: recursive refresh failed on "Personal Vault"
  (ObjectHandle is Invalid) -> read per top folder (4.7 s, 1 of 9 failed).
  After warm-up, walking every folder: 6 ms (GD) / 6 ms (OD, vault excluded).
- Implemented: async cloud listing (DefaultFileSystem::refreshSlow), shown
  folder revalidation, parallel prefetch, rc client (app/Cloud/panerorcclient),
  folder badges (PaneroCloudUtils::folderCacheState), places + isCloudRoot,
  media button shows the drive name, tree root without QStorageInfo (statfs),
  Rename..., own Google client id (Add Cloud + sidebar), D-017.
- Bugs found while testing: rclone wants recursive="true" as a string; the
  test dialog check failed because KIOFileWidgets in the Cloud library turns
  on KDE's automatic accelerators ("&Read-only") - test made tolerant.
- First version passed rc credentials / client secret through environment
  variables; the security test's process scanner flagged it (D-016 R3) ->
  htpasswd hash file and secret only via rclone rcd socket.
- Tests: ctest 7/7 (46 cloud unit tests), security suite 16/16, new GUI test
  dev/scripts/gui-cloud-test.sh (Xvfb, stand-in alias drive): path bar,
  no "..", Up stops, Rename, folder badge - all as expected (screenshots
  build/cloud-shots, not committed).
- Released v0.1.0-preview.9: PR #14, tag on ef284a136, release run 45028537532;
  assets verified (SHA256SUMS); .deb copied to ~/Downloads.

## 2026-11-22 — Preview 10: cloud freeze

- Owner: Panero froze, force quit. Journal: Evince opened three PDFs from
  OneDrive (16:45:06–16:45:29), a second Panero start at 16:45:59 (window
  already unresponsive), scope killed 16:46:51. rclone stderr is only kept in
  memory, no coredump (SIGKILL); OneDrive mount left "Transport endpoint is
  not connected" (Panero lazily unmounts it at the next mount). BambuStudio
  messages in the same journal stream belong to VS Code's scope.
- Cleaned my own leftovers from the preview 9 GUI test: a dead TestDrive
  mount under build/dev-cloud and six at-spi dbus-daemons (gui-cloud-test.sh
  now kills its accessibility bus).
- Stopped-rclone hunt (scratch script, now dev/scripts/gui-cloud-freeze-test.sh):
  "up" froze in chdir (ListPanelFunc::doRefresh -> QDir::setCurrent); after
  that fix the sidebar repaint froze in KFileItem::iconName (KDirModel).
- A fully stopped rclone never finishes a listing, so code after a listing
  (startWatching) is invisible to that hunt -> strace -f -b execve -k of a
  normal session (-b execve: a traced setuid fusermount3 cannot mount).
  163 GUI-thread accesses: FileItem::getMime reading .directory per folder
  (panel icons, status bar), tree KDirLister (realpath, KDirWatch inotify,
  KFileSystemType statfs, .hidden), context menu.
- Fixed (D-018); 5 accesses left. Revalidation no longer forces vfs/refresh
  on polling providers (rclone holds the folder lock during the listing).
- Tests: ctest 7/7, security 16/16, gui-cloud-test 2/2, freeze test passed.
- Released v0.1.0-preview.10: PR #15 (CI green), tag on 1099ea025, release
  run 45044078622; SHA256SUMS verified, Version 0.1.0~preview.10-1; .deb
  copied to ~/Downloads.

## 2026-11-22 — Preview 11 planning

- Owner plan: remove legacy sidebar, cloud selection, drive buttons, TC-like
  Multi-Rename (Ctrl+M), Windows version up to date, duplicate Copy path.
- Done on feature/preview11 (not released): per-panel sidebar unreachable
  (button, Alt+Down, restore); cloud highlight follows the active panel
  (GUI test screenshots 09/10).
- Facts gathered: Krusader's HideSquashFS is off by default (snaps listed);
  KRename 5.0.2 (Ubuntu, not installed) features read from its binary (no
  handbook shipped, docs.kde.org pages 404); experiment/windows is 21 ahead,
  27 behind main; Windows CI screenshot shows "?" + grey close (dialog flags).
- Waiting for the owner's decisions before implementing the rest.
- Owner decisions (D-019): all recommendations; plus sort arrow ^/v.
- Done on feature/preview11: sort arrow drawn by KrStyleProxy (Fusion/Breeze
  drew v for ascending, Windows ^); Copy Path duplicates hidden; Ctrl+M =
  Multi-Rename, Alt+F1/F2 = drive lists (GNOME binds Alt+F1 panel-main-menu
  and Alt+F2 panel-run-dialog on this desktop: they reach Panero only when
  freed); drive buttons (no file access in the GUI thread, mountinfo
  notifier, Solid); Multi-Rename (TC rules, KIO renames, swap-safe plan,
  rollback on error, undo, presets). Tests: 9 suites incl. panerodrivestest
  and paneromultirenametest (2 real bugs found: literal brackets, plain
  replace doubled backslashes); gui-rename-test.sh rename+undo on disk.
- Windows merge running in a separate worktree (feature/windows-main).
- Windows merge done by a helper session on feature/windows-main (CI
  45059382401 Windows, 45059382411 Linux, both green); fast-forwarded
  feature/preview11 to it; local ctest 9/9. Fixed my drive bar building
  QIcons in a worker thread on Windows; AA_DisableWindowContextHelpButton on
  Windows. Open: the owner's "right panel without outline" was not
  reproduced on the runner (fixed what differed: 0 px to the window edge).
- Released v0.1.0-preview.11: PR #16 (Linux + Windows checks green), tag on
  6e82a2bfe, release run 45061474829; assets .deb, Windows ZIP, source,
  licenses; SHA256SUMS verified; .deb and ZIP copied to ~/Downloads.

## 2026-11-23 — Preview 12

- Drive bar label option and Network button (f3d4f49fa); Win11 icon theme
  for Linux (26532ddcc; grey outlined folders at panel sizes fixed by linking
  places/devices and inode-directory to the coloured artwork).
- Windows helper (feature/preview12-windows, d33ba1f4e): icons, no hydration,
  Cloud section, installer; Windows run 45161131550 and CI 45161110498 green.
  Merged into feature/preview12 (f66d0a8e2). Screenshots reviewed.
- Explorer menu not done; moved to preview 13 (D-021), helper started on
  feature/explorer-menu.
- Version bumped to 0.1.0-preview.12; release notes written.
- Explorer menu helper finished early (feature/explorer-menu, 7081d0aec);
  merged into preview 12 (D-021 superseded). Package: icon files were
  executable (lintian), fixed in fetch-win11-icons.sh; test-deb accepts the
  compressed licence; .deb lintian-clean, test-deb 96 checks incl. upgrade
  from preview 11.
- Released v0.1.0-preview.12: PR #18 merged (e98c4f8b7), tag pushed, release
  run 45177457275 green (Windows build + GUI test, .deb build + test-deb,
  publish). Assets: .deb, Windows ZIP, setup.exe, source, licences;
  SHA256SUMS verified after download.

## 2026-11-23 — Preview 13 planning

- Owner feedback after preview 12 (drag & drop, Windows cloud, drive
  right-click, icons, selection, app icon, splitter, back/forward, menu
  choice, viewer, colours, thumbnails, copy progress, Windows workflow,
  ideas, settings). Causes found:
  - Drag & drop: Firefox is a Flatpak (filesystem=xdg-download only); Qt 5
    drags pass plain paths, GTK file managers use the document portal
    (application/vnd.portal.filetransfer). Fix: offer the portal key too.
  - Windows Cloud section hidden: preview 11 Quick Settings wrote
    "Show Cloud=false" (hidden checkbox, default false on Windows).
  - OneDrive ticks on all folders: stateFromAttributes treats folders
    without flags as Available; use PKEY_StorageProviderState instead.
  - Special folder icons: only KDirModel (tree) maps XDG user dirs.
  - Folder colour recolours the active theme's folders (Win11 shape kept);
    a previously chosen colour persists across icon theme changes.
- Proposal page with 12 decisions given to the owner (private artifact):
  https://claude.ai/artifact/P3K5pkJobatqem4De4Uyoc. Waiting for decisions.

## 2026-11-23/24 — Preview 13 implementation (branch feature/preview13)

- Owner decisions recorded as D-022. VM set up: OpenSSH (host-only firewall
  rule, key auth), MSYS2 + pinned KF5, Python 3.12 + pywinauto, Inno Setup;
  GUI runs on the VM desktop via a scheduled task (/it) — dev/windows/vm.sh.
  The CI GUI test is not VM-safe (renames C:\msys64, real profile, Recycle
  Bin, fake sync root): VM mode is being added by a helper.
- Owner findings: "krarc:/ does not exist" (folder tree root inside an
  archive) fixed; parentless dialogs reparented to the main window on Polish
  (centred; Wayland transient). gui-dialog-test.sh (fails on the old code).
- Drag & drop: FileTransfer portal key (paneroportaldrag), verified that the
  Flatpak Firefox sandbox reads a file through the key while the plain path
  is "not found" there.
- Done by the main session: special folder icons, Windows Show Cloud
  migration, Linux drive menu, 50/50 double-click, Back/Forward, bigger icon
  (+ .ico), even thumbnail frame, startup (KMountMan allDevices/Block scans
  removed), thumbnails on cloud drives off, cloud listing cache, context
  menu freeze on cloud files (KFileItemListProperties stat).
- Four helpers in worktrees: p13-windows, p13-quickview, p13-jobs,
  p13-settings. All stopped at the 5-hour usage limit and were resumed.

## 2026-11-24/25 — Preview 13 integration

- Helpers stopped twice at the usage limit; their branches were merged by
  the main session (conflicts: kractions includes, autotests CMake, the old
  Quick Settings dialog deleted by p13-settings while p13-windows edited
  it — the right-click choice lives in panerosettings/paneroquicksettings,
  same key Look&Feel/Explorer Menu). Toolbar layout version 34.
- Linux: 14 suites, 87/87 window checks (gui-fileops-test opened the Trash
  by toolbar position, which became Unpack: now via the location bar).
- Windows VM: build + 11 suites (qt5-multimedia added to the VM). VM GUI
  runs failed: Panero's windows stay behind the owner's foreground app
  (Firefox) — a task-started process cannot take the foreground. Added
  dev/windows/fgguard.py (PANERO_VM_GUARD=1): input only while Panero is in
  front, else the test stops; nothing reached Firefox. The focused tests
  (quickview, jobs) now also run on the GitHub runner after gui-test.py.
- GitHub runner: Quick View test and jobs test pass, installer passes;
  gui-test.py's p13 checks failed already on feature/p13-windows: packing
  (UIA select() did not change QComboBox; archive jobs passed "/C:/..."
  paths to tar.exe) and Quick Settings checks (old dialog) — fixed.
- Package: lintian clean, 96 checks incl. upgrade from preview 12
  (knotifications5/panero.notifyrc allowed).
- Released v0.1.0-preview.13: PR #20 merged (3c882dd80), tag pushed,
  release run 45587307079 green after one re-run of the Windows GUI job
  ("Ctrl+Q opens Quick View" checked after 1 s on a slow runner; the test now
  waits up to 6 s). Assets: .deb, Windows ZIP, setup.exe, source, licences;
  SHA256SUMS verified after download. The image viewer continues on
  `feature/image-viewer` (D-023; next versions 0.2.0 / 0.3.0).
