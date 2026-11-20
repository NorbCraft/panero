# Panero test report

Living document; every entry states where and how it was verified. "Xvfb" means
an X11 virtual framebuffer on the build host (Zorin OS 18.1), with an isolated
profile (`dev/scripts/run-dev.sh`), driven by xdotool. "Host desktop" means the
owner's real Zorin OS 18.1 GNOME Wayland session. Packaging/clean-system results
are in `dev/TEST-REPORT-packaging.md`.

## Environments

| Environment | Details |
|---|---|
| Build/test host | Zorin OS 18.1 (Ubuntu 24.04 base), x86_64, Qt 5.15.13, KF 5.115, GNOME Wayland session |
| CI | GitHub Actions `ubuntu-24.04` runner, build + ctest (offscreen/Xvfb) |
| Clean container | `ubuntu:24.04` Docker image (packaging, KDiff3 install flow, screenshots) |

## Automated tests (ctest)

| Suite | Covers | Result |
|---|---|---|
| `commandergesturetest` | right-button state machine: short click toggles on release, hold opens menu without toggle, no double trigger after hold, background menu, sweep marking, cancel on focus loss, release without press; operation/context-menu target selection (marks win, current fallback, `..` never, background never targets files) | pass (local + CI) |
| `panerothemetest` | appearance settings persistence: defaults, row height clamp 16–48, never-clip rule, light/dark colour overrides (`#rrggbb` and `r,g,b`), default scheme contrast, classic colour switch | pass (local + CI) |
| `paneroimporttest` | Krusader settings import: originals untouched (hash compare), copies + data files, backups of replaced Panero files, pending import runs once | pass (local + CI) |
| `appstreamtest` (ECM) | metainfo validity | pass |

## Functional checks

| Area | Check | Where | Result |
|---|---|---|---|
| Cursor/marks | left click moves cursor, keeps marks; arrows keep marks; old row loses blue treatment | Xvfb | pass |
| Cursor/marks | short right click toggles; Space, Insert toggle; mark shown red + check badge; marked cursor row shows both | Xvfb | pass |
| Hold menu | hold 600 ms opens native Krusader menu (Open With, User Actions, KDiff3 service menu, Trash, Delete, ...) for the marked set with "Marked: N items"; held row not toggled | Xvfb | pass |
| Hold menu | Escape closes menu, release after menu does not toggle | Xvfb | pass |
| Column chooser | header right-click opens upstream column chooser immediately (upstream code path, unchanged) | code review + Xvfb | pass |
| Details view | Ext column: folders/dotfiles/no-extension not split, `.tar.gz` kept, unicode names | Xvfb | pass |
| Thumbnails | grid with real KIO previews for JPEG, PNG, PDF, text; marks/cursor; Up/Down by row | Xvfb | pass |
| Appearance | light, dark (forced), dark following the host desktop via portal | Xvfb, host desktop | pass |
| Appearance | dark mode toolbar icons visible (Breeze Dark fallback) | Xvfb | pass |
| Density | compact/comfortable rows; text/icons never clipped; 2x scale factor | Xvfb (QT_SCALE_FACTOR=2) | pass |
| View & Colors | Density → Compact applied live from the dialog: rows 20 px, icons 16 px, config written (`Row Height=20`, `IconSize=16`); existing marks and cursor kept | Xvfb | pass |
| Toolbar | Up, Home, Refresh, Compare, Pack, Unpack, Search, Trash; view strip; no Copy/Move/New Folder/Sync | Xvfb | pass |
| Sidebar | pins (Home default), tree follows active panel incl. after show; "Opens in" indicator | Xvfb | pass |
| Drive selector | "System (/)" label + device menu per panel | Xvfb | pass |
| Archives | browse zip via Panero's own krarc worker (process maps show the private plugin) while distro Krusader is installed | Xvfb | pass |
| KDiff3 | Compare launches kdiff3 with argument array, names with spaces/unicode, left panel first | Xvfb | pass |
| KDiff3 | kdiff3 hidden from PATH: "Install KDiff3?" offered; Install performs a real PackageKit resolve (system journal) and shows guidance + Retry | Xvfb + host PackageKit | pass |
| File operations | `dev/scripts/gui-fileops-test.sh` (real files, names with spaces/unicode, symlink): F5 copy; second copy → "File Already Exists" → Cancel closes it and keeps the target; F7 new folder; F2 inline rename; F6 move of a folder with a space; F8 → trash confirmation lists the target → symlink in Trash (isolated profile), target kept; Pack → tar archive whose contents were checked; Unpack into a chosen folder → byte-identical file; Toolbar Trash → context menu Restore brings the symlink back (pointing to its target); Ctrl+S search found 2 matches. Result: **17/17 pass** | Xvfb | pass |
| KDiff3 install (isolated) | `dev/scripts/test-kdiff3-install.sh` in clean ubuntu:24.04 with own system bus, polkitd, packagekitd, Panero from the .deb: **allow** → kdiff3 installed from the Ubuntu archive, comparison resumed (`kdiff3 .../A/same name ü.txt .../B/same name ü.txt`); **deny** → nothing installed, no error (cancel); **broken sources** → "package not found" guidance with Retry | container | pass |
| ISO images | after backport (kio_iso QCoreApplication + vnd.efi.iso): genisoimage test image listed and read via Panero's worker | backport agent, host | pass |
| Backports | 39 upstream fixes; rebuild warning-clean, ctest 4/4, GUI file-operation test 17/17 on the combined branch | host | pass |
| Packaging | see `dev/TEST-REPORT-packaging.md`: lintian clean (justified overrides only); install, coexistence with krusader both orders, upgrade, remove/purge: 83 pass / 0 fail / 1 known (ISO, fixed by the backport) | container | pass |
| Wayland | starts via XWayland (Qt 5 default on GNOME) and natively (`QT_QPA_PLATFORM=wayland`) | host desktop | pass |

## Release v0.1.0-preview.1 (published 2026-11-17)

- Release workflow run 44174692541: build + ctest + lintian + package tests, publish,
  re-download and checksum verification — success.
- Independent check of the published assets: `sha256sum -c SHA256SUMS` OK for the .deb,
  source and licenses archives; package `Version: 0.1.0~preview.1-1`; source archive
  `SOURCE-INFO.txt` commit `13106ed09946a6382309c15c081267ec420704d5` = tag commit.
- Downloaded .deb in fresh ubuntu:24.04 containers: install scenario 57/57 pass,
  coexistence with the distro krusader package 13/13 pass.

## Preview 3 (2026-11-19, before release)

- ctest 5/5 (new: paneropathstest; accent colour case in panerothemetest).
- GUI file-ops test 17/17 under Xvfb with the new toolbar (Trash menu moved).
- Interactive checks in the staged build under Xvfb: Space on a folder shows its
  size and marks it; tree click unfolds, second click folds; tree stays rooted at
  Home in ~/Documents/Invoices; pins highlight only for the exact folder; Ctrl+wheel
  Details -> Thumbnails 48 -> 64 -> 48 -> Details with the toggle following; menu
  button lists all menus; View & Colors shows the accent and selection options;
  "Use Panero's look" off removes the style sheet; preview 1 local toolbar file
  with a custom shortcut is migrated (backup, new toolbar, shortcut kept).
- Container build (dpkg-buildpackage, ctest 4/4 in the build, lintian: only the
  documented overrides) and package tests: install, coexist, upgrade from
  preview 2, remove/purge — 85 pass, 0 fail.
- Host: the owner's preview 2 install showed the stale-toolbar problem that the
  migration fixes; preview 3 host install not yet confirmed.

## Release v0.1.0-preview.3 (published 2026-11-19)

- Release workflow run 44598245833 (build, ctest, lintian, package tests, publish) — success.
- Downloaded assets: `sha256sum -c SHA256SUMS` OK; package `Version: 0.1.0~preview.3-1`;
  source archive commit `ba26852269b4ec98a3e981bc39bf099adcbe24fe` = tag commit.

## Preview 4 (2026-11-20, before release)

- ctest 6/6 (new panerocloudtest; paneropathstest empty-URL case; theme test
  badge default), GUI file-ops 17/17 under Xvfb.
- Interactive under Xvfb: progress row in the destination panel during a 1.6 GB
  copy (no KIO window); text-under-icon toolbar; rename field; Quick Settings
  (all new options); theme change -> restart prompt -> restart (new process,
  settings kept, fully dark); Panero Line icons + folder colours (light/dark,
  system icons too); toolbar unlock grip; F9 opens the desktop terminal; new tab
  without error; F7 cursor on the new folder; Cloud section: rclone alias
  remote mounted by click, opened in the panel, unmounted on exit (rclone
  1.60.1 from the noble archive, extracted locally).
- Container build (ctest 5/5 in the build, lintian only documented overrides)
  and package tests incl. upgrade from preview 3: 85 pass, 0 fail.
- Not verified: real cloud sign-ins (no accounts); PackageKit install of
  rclone/fuse3 (needs authorisation; same code path as KDiff3).

## Release v0.1.0-preview.4 (published 2026-11-20)

- Release workflow run 44802311531 — success. Downloaded assets: checksums OK,
  `Version: 0.1.0~preview.4-1`, source commit b0e89cc95 = tag commit.

## Not yet verified (open)

- Preview 3 installed on the owner's Zorin host (owner installed preview 2; launcher and About worked, toolbar issue fixed in preview 3).
- Unmounted/removable drive behaviour of pins and the drive selector (no removable media attached on the build host).
- Native X11 (Xorg) session: only XWayland and Xvfb are covered (owner decision Q7).
