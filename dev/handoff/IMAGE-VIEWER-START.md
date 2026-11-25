# Start here: implementing the Panero Image Viewer

For the agent (or developer) who implements the image viewer. You do not
need any earlier conversation or prompt: everything is in this repository.
Read this file completely before changing code.

## 1. What to read, in this order

1. `AGENTS.md` — project rules (short; they apply to you).
2. **`dev/IMAGE-VIEWER-PLAN.md`** — the specification: behaviour, technical
   design, milestones with acceptance criteria, open questions, tests. This
   is your source of truth for *what* to build.
3. `dev/DECISIONS.md` — at least **D-018** (no disk access in the GUI thread
   on cloud drives), **D-019/D-020/D-022** (recent product decisions) and
   **D-023** (the viewer). Never silently reverse a decision: add a new entry
   that supersedes it, with the owner's agreement.
4. `dev/handoff/image-viewer/README.md` — the visual references (mockups) and
   the approved icon.
5. `dev/PLAN.md` **Phase 18** — the milestone checklist you keep up to date.
6. `dev/LOG.md` — the chronological work log (newest at the bottom); read the
   last entries for the current state; append your own.
7. `USER-GUIDE.md` — what Panero looks like to its users (keep it current
   for user-facing changes).

## 2. What Panero is and how it was built so far

- Panero is an **independent fork of Krusader 2.8.1** (a KDE twin-panel file
  manager), **Qt 5 / KDE Frameworks 5**, C++, CMake. GPL-2.0-or-later. It is
  not affiliated with KDE. Upstream remote: `upstream`
  (invent.kde.org/utilities/krusader). Repository: `NorbCraft/panero`.
- Target users: people who want a simple, modern file manager that still has
  the power of Total Commander / Krusader. Main platform **Zorin OS 18 /
  Ubuntu 24.04** (GNOME, Wayland), shipped as a `.deb`; a **Windows** build
  from the same code (portable ZIP + Inno Setup installer) since preview 11.
- History: 13 "preview" releases (0.1.0-preview.1 … .13), each planned with
  the owner on a proposal page, decided (D-0xx entries), implemented, tested
  on Linux (unit + Xvfb window tests + package tests in clean containers) and
  on Windows (GitHub runner, and the owner's VM), then released through a
  tag. Highlights: Panero look and toolbar (D-012/13), cloud drives through
  rclone with safe sign-in (D-016/17), the freeze fix (D-018), drive buttons
  and Multi-Rename (D-019), Windows integration (D-020/21), Quick View, copy
  windows with Undo, one Settings window (D-022).
- From now on versions are **semantic**: the viewer release is **0.2.0**,
  the editor **0.3.0** (D-023).

Where things are:

| Path | Contents |
|---|---|
| `app/` | the application (Krusader's code + Panero's additions; Panero's own files are mostly named `panero*.cpp/.h`) |
| `app/QuickView/` | Quick View (preview 13) — the closest example of a self-contained Panero module: data library + widget library + unit test + window test |
| `app/Panel/`, `app/Panel/PanelView/` | panels, views, marks (`KrView`), panel functions (`ListPanelFunc`) |
| `app/FileSystem/` | file system layer, `paneroslowpaths.*` (cloud/network path rules), `panerocloudfiles.*` (Windows cloud placeholders) |
| `app/JobMan/` | copy/move jobs, progress windows, `PaneroUndo` |
| `app/Dialogs/` | dialogs incl. `panerosettings.*` (Settings) and `paneroquicksettings.*` |
| `app/icons/` | Panero's icons (render pipeline: `dev/scripts/render-icons.sh`) |
| `autotests/` | unit tests (`ecm_add_test`), run with `ctest` |
| `dev/scripts/` | build, run, test and packaging scripts (see section 4) |
| `dev/windows/` | Windows build/test scripts, README with Windows details |
| `debian/` | Debian packaging (`control`, `rules`, `copyright`, `changelog`) |
| `.github/workflows/` | `ci.yml` (Linux build + tests on pushes to `main`/`feature/**` and PRs), `windows.yml` (Windows build + GUI tests on PRs), `release.yml` (on tags) |
| `dev/` | plan, log, decisions, environment notes, release notes, test reports |

## 3. Rules that keep the project maintainable

These are the conventions the project has followed for 13 releases. Follow
them even where your usual style differs.

- **Scope:** implement what `dev/IMAGE-VIEWER-PLAN.md` says, milestone by
  milestone. If something is unclear or a product choice is not covered,
  **ask the owner** (keep working on what is clear); record answers in
  `dev/DECISIONS.md` and the plan's open questions.
- **Small, reviewable commits** with clear messages (what and why), pushed to
  `feature/image-viewer` (Date Taken: `feature/date-taken`). Do not merge
  into `main`, tag or publish releases unless the owner asks.
- **Do not rewrite or reformat unrelated code.** Krusader's code is kept
  close to upstream; when you change it, keep the change minimal and mark it
  with a `// Panero: …` comment explaining why (look at existing examples,
  e.g. `grep -rn "// Panero:" app | head`).
- **New files:** SPDX header like the existing Panero files:
  ```
  /*
      SPDX-FileCopyrightText: 2026 Panero contributors

      SPDX-License-Identifier: GPL-2.0-or-later
  */
  ```
  Classes prefixed `Panero…`, files lowercase `panero….cpp/.h`, Qt/KDE
  naming style (camelCase methods, `m_` members in new code), include
  guards like the neighbours.
- **C++11 only** (`gnu++11`, set by ECM's `KDE_COMPILERSETTINGS_LEVEL 5.68`
  in `CMakeLists.txt`): no `std::optional`, `if (init; cond)`, structured
  bindings, generic lambdas (`auto` parameters) etc. Use Qt containers and idioms as
  the surrounding code does.
- **User-visible text** through KI18n `i18n()` / `i18nc()` / `i18np()`.
  Plain, short wording; name things the way users know them. Platform
  wording differences: `PANERO_TEXT(linux, windows)` in
  `app/panerowording.h` (e.g. Trash vs Recycle Bin).
- **The GUI thread never waits for a disk on cloud/network paths** (D-018).
  Check `PaneroSlowPaths::isSlowPath(path)` (and on Windows
  `PaneroCloudFiles::avoidReading(path)`) before any file access in the GUI
  thread; do decoding, metadata and saving in worker threads
  (`QtConcurrent` + `QFutureWatcher`, as in Quick View). Never read cloud
  files automatically — the user asks (section 3 of the plan).
- **Windows must keep compiling.** Use Qt APIs, not POSIX, where possible;
  `#ifdef Q_OS_WIN` for platform parts. The Windows workflow runs on pull
  requests; Windows-specific features wait for a "Windows round".
- **Tests come with the feature:** unit tests in `autotests/` for logic
  (recipe, geometry, saver, metadata, decoder queue); a window test
  `dev/scripts/gui-imageviewer-test.sh` modelled on
  `dev/scripts/gui-quickview-test.sh`. Tests use **temporary HOME/XDG
  directories** and generated or licensed fixtures — never the owner's real
  files, settings (`~/.config/panerorc`, `~/.config/krusaderrc`) or Trash.
- **Dependencies:** only those in the plan (Exiv2; Qt/KDE image plugins).
  Anything else: ask first. Add build dependencies to `debian/control`, the
  CI workflow (`ci.yml` `apt-get install` line) and `dev/build-deps.txt`;
  licences to `debian/copyright`.
- **Records after every work session:** tick `dev/PLAN.md` Phase 18 boxes,
  append to `dev/LOG.md` (date, what, how verified, what is open). Keep them
  current so another agent can continue without your conversation.
- **Screenshots** of new UI for the owner (put them under `build/` or
  attach to your report; commit only small, deliberate ones under
  `dev/screenshots/`).
- **Never commit** credentials, tokens, private keys, personal paths or
  data, downloaded RAW samples, mockup sources, or binaries.
- **GitHub:** only the repository `NorbCraft/panero` (branches, PRs, Actions).
  No pull requests or issues against KDE/Krusader. Commit identity is set per
  repository (`dev/ENVIRONMENT.md`); keep the existing attribution style of
  the log if you add co-author lines.

## 4. Building, running, testing

On Ubuntu 24.04 / Zorin 18 (dependencies: `sudo bash dev/scripts/bootstrap-host.sh`,
or the package list in `dev/build-deps.txt`; Exiv2 for the viewer:
`libexiv2-dev`).

```bash
# configure and build (the existing build/ uses Ninja + RelWithDebInfo)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"

# unit tests (offscreen; use a throw-away HOME)
cd build && QT_QPA_PLATFORM=offscreen HOME=$(mktemp -d) ctest --output-on-failure; cd ..

# run Panero isolated from your real settings (HOME/XDG in build/dev-home)
dev/scripts/run-dev.sh            # on the desktop
dev/scripts/run-dev.sh --xvfb     # headless

# window tests (Xvfb, temporary HOME, screenshots under build/)
DESTDIR=$PWD/build/stage cmake --install build     # the tests run build/stage
dev/scripts/gui-quickview-test.sh                  # example; see all dev/scripts/gui-*-test.sh

# package (Docker, clean ubuntu:24.04) and package tests
dev/scripts/build-deb.sh --out /tmp/panero-deb
dev/scripts/test-deb.sh --deb /tmp/panero-deb/panero_*.deb
```

- Test window programs under **Xvfb with `QT_QPA_PLATFORM=xcb`** like the
  existing scripts; the owner's desktop is **Wayland**, so also check the
  taskbar identity in a headless GNOME Shell (plan section 10.5 / 13).
- CI: pushing to `feature/image-viewer` runs `ci.yml` (Linux). Pull requests
  to `main` also run `windows.yml`. Use `[skip ci]` in commit messages that
  only change documents.
- Windows locally: `dev/windows/README.md` (MSYS2 UCRT64). An optional
  Windows VM helper (`dev/windows/vm.sh`) needs the owner's local
  configuration and is usually not available to you — rely on the GitHub
  Windows workflow.

## 5. How to start

1. Create your working copy of `feature/image-viewer`; build and run the
   unit tests to see a green baseline.
2. Read `app/QuickView/` (especially `paneroquickviewwidgets.*` and
   `paneroquickviewdata.*`), `ListPanelFunc::execute()/view()`
   (`app/Panel/panelfunc.cpp`) and `KrView` (`app/Panel/PanelView/krview.h`).
3. Start **milestone M0** (plan section 11). Keep `dev/PLAN.md` Phase 18 and
   `dev/LOG.md` updated as you go.
4. After each milestone: tests green, screenshots, a short report to the
   owner (what works, what was verified and how, what is open).
