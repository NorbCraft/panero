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
