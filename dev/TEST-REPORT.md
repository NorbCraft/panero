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
| Toolbar | Up, Home, Refresh, Compare, Pack, Unpack, Search, Trash; view strip; no Copy/Move/New Folder/Sync | Xvfb | pass |
| Sidebar | pins (Home default), tree follows active panel incl. after show; "Opens in" indicator | Xvfb | pass |
| Drive selector | "System (/)" label + device menu per panel | Xvfb | pass |
| Archives | browse zip via Panero's own krarc worker (process maps show the private plugin) while distro Krusader is installed | Xvfb | pass |
| KDiff3 | Compare launches kdiff3 with argument array, names with spaces/unicode, left panel first | Xvfb | pass |
| KDiff3 | kdiff3 hidden from PATH: "Install KDiff3?" offered; Install performs a real PackageKit resolve (system journal) and shows guidance + Retry | Xvfb + host PackageKit | pass |
| File operations | `dev/scripts/gui-fileops-test.sh` (real files, names with spaces/unicode, symlink): F5 copy; second copy → "File Already Exists" → Cancel closes it and keeps the target; F7 new folder; F2 inline rename; F6 move of a folder with a space; F8 → trash confirmation lists the target → symlink in Trash (isolated profile), target kept; Pack → tar archive whose contents were checked; Unpack into a chosen folder → byte-identical file; Toolbar Trash → context menu Restore brings the symlink back (pointing to its target); Ctrl+S search found 2 matches. Result: **17/17 pass** | Xvfb | pass |
| Wayland | starts via XWayland (Qt 5 default on GNOME) and natively (`QT_QPA_PLATFORM=wayland`) | host desktop | pass |

## Not yet verified (open)

- Real KDiff3 installation, cancelled authorisation and broken sources in an isolated system: `dev/scripts/test-kdiff3-install.sh` (pending .deb).
- .deb install/upgrade/remove in a clean container and on the host; coexistence with the distro Krusader package (pending packaging branch).
- Unmounted/removable drive behaviour of pins and the drive selector (no removable media attached on the build host).
- Native X11 (Xorg) session: only XWayland and Xvfb are covered (owner decision Q7).
