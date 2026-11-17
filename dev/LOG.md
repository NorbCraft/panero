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
