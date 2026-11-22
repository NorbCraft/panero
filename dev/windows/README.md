# Panero on Windows

A native Windows x64 build of Panero that keeps the twin-panel layout and the
Panero look, with Windows drive, folder and file icons. Since preview 11 it is
built from the same code base as the Linux version (D-019): Windows-specific
code sits behind `Q_OS_WIN` / CMake `WIN32`, and every pull request and every
push to `main` is built and tested on Windows. The experiment that started it
is D-015 (branch `experiment/windows`, merged through `feature/windows-main`).

## Toolchain

- **MSYS2 UCRT64**: GCC (MinGW-w64), CMake, Ninja, Qt 5.15 (KDE patch collection)
  from the live MSYS2 repository.
- **KDE Frameworks 5.116**, the final KF5 release. MSYS2 removed the KF5 (`*-qt5`)
  packages from its database after moving to KF6. The final builds are still
  served by `repo.msys2.org`. `install-kf5.sh` downloads the 31 files listed in
  `kf5-packages.sha256`, checks every SHA-256 and installs them with `pacman -U`.
  The dependency closure and DLL imports were checked against the live
  repository on 2026-11-20 (`kf5-closure.py`). The only unresolved import is
  `kwalletd5.exe` → `libgpgmepp-6.dll`, a daemon Panero does not use.
- If the files ever disappear from the mirror, rebuild them from the MSYS2
  `MINGW-packages` history, or move Panero to Qt 6 / KF6 (MSYS2 ships KF6).

## Build locally (Windows, MSYS2 UCRT64 shell)

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,extra-cmake-modules,qt5-base,qt5-svg,qt5-tools,qt5-winextras,qt5-imageformats,gettext-tools,zlib,bzip2,xz,dbus} curl zip
dev/windows/install-kf5.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
ninja -C build && (cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure)
dev/windows/package.sh build stage dist      # -> dist/panero-<version>-windows-x64.zip
```

## Portable ZIP

`package.sh` installs into a staging prefix and builds a self-contained folder:
`panero.exe`, `kioslave5.exe`/`kiod5.exe` (KIO 5 runs workers out of process),
every DLL the executables and plugins import (followed recursively with
`objdump -p`), `plugins/` (Qt platform/style/image/icon plugins, KIO workers
`file`, `trash` = Recycle Bin, `ftp`, `http`, ..., Panero's `krarc`), `data/`
(UI files, Panero data, Breeze icons as `icontheme.rcc`) and `qt.conf`.

## CI

`.github/workflows/windows.yml` (the Linux workflow `ci.yml` is separate and
unchanged):

- Triggers: every pull request, pushes to `main` (and to `feature/windows-main`),
  manual runs (`workflow_dispatch`), and `workflow_call` from the release
  workflow.
- `windows-build`: build, `ctest` (5 suites; the two cloud suites are
  Linux-only), package, upload the ZIP artifact `panero-windows-x64`.
- `windows-gui-test`: a fresh runner. It renames `C:\msys64`, strips `PATH` to
  the Windows folders, starts `panero.exe` and checks that every module loaded
  by Panero and its KIO workers comes from the ZIP or `C:\Windows`. Then it runs
  `gui-test.py` (pywinauto) on disposable files: first run straight to the
  panels, the title bar buttons (minimize, maximize, restore, close), the active
  panel's outline with each panel active (keyboard and mouse, normal and
  maximized), the shell icons of the pins, the Windows words (Recycle Bin,
  Panero Settings) and the Total Commander habits (F5–F8, F2, Insert, Del, Tab,
  Enter/Backspace, Alt+F1/F2, Shift+F6, Ctrl+click, F3/F4/F9). Screenshots, logs
  and `gui-results.md` are uploaded as `panero-windows-test-results`.
- Release (`release.yml`, tags `v*`): its `windows` job calls `windows.yml` on
  the tag; the ZIP is added to the release as
  `panero-<version>-windows-x64.zip`, next to the `.deb`, and listed in
  `SHA256SUMS`. A failing Windows build or GUI test stops the release.

## Windows-specific code

- `app/compat/panero_win32_compat.h`: POSIX names that MinGW lacks (uid/gid,
  `lstat`, mode bits). Force-included on Windows only.
- `app/Panel/panerowinshell.*`: Explorer drive names and the shell icons for
  drives, folders and files (cached per extension), used by the file list, the
  folder tree, the drive button and the pins.
- `app/panerowording.h`: `PANERO_TEXT(linux, windows)` for the words Windows
  uses: Recycle Bin (toolbar, menus, F8/Del question and button, progress,
  settings), "Panero Settings" for the Konfigurator window and in messages,
  Windows Terminal / PowerShell and Notepad in the F9/F4 tooltips.
- `app/GUI/mediabutton.cpp`: the drive menu lists drive letters. On Windows
  Alt+F1 / Alt+F2 open the left / right drive list (Total Commander), in
  addition to Ctrl+Shift+Left/Right (`listpanelactions.cpp`); Ctrl+M stays.
- `app/main.cpp`: no D-Bus on Windows, so no single-instance mode or remote
  control. Plugins are found through `qt.conf`.
- `app/krusader.cpp`: the main window is a plain `Qt::Window` on Windows (see
  below); no welcome box, no first-run Konfigurator / tool search; the default
  window size also applies on the first run.
- Not built on Windows: the cloud backend `app/Cloud` (rclone mounts need FUSE;
  Secret Service keyring, `panero-cloud-key`, rclone's unix-socket remote
  control) and its two test suites, the ISO worker (Linux CD-ROM ioctls) and the
  handbook. The sidebar has no Cloud section on Windows and Quick Settings hides
  "Show the Cloud section". Cloud on Windows (preview 12) will use the OneDrive /
  Google Drive app folders, not rclone. `PaneroSlowPaths` is still built (it has
  no cloud mounts to find there).
- Linux keeps its behaviour: experiment changes that would alter it (first-run
  window size, job error dialogs, the internal-editor warning, viewer and lister
  captions as local paths) are Windows-only.

## Preview 11 fixes (owner reports)

- **Window buttons**: the main window showed only "?" and a grey ×. Upstream
  creates it with `Qt::Window | Qt::WindowTitleHint |
  Qt::WindowContextHelpButtonHint`; any explicit title-bar hint makes Qt use
  exactly the hints given, so Windows got a title, a help button and a disabled
  close button, and UI Automation even reported the window as a dialog. X11 and
  Wayland window managers ignore these hints. Windows now gets a plain
  `Qt::Window` (minimize, maximize, close). GUI test: `title-bar-*`,
  `window-maximized`, `window-minimized`.
- **Pin icons**: pins used the theme icons (only drive pins had the shell
  icon). They now use the shell icons when Icons = System, like the file list
  and the tree. GUI test: `pins-shell-icons*`.
- **Right panel outline**: see the notes in the GUI test results
  (`outline-*`).
- **Windows words and first run**: see above. GUI test:
  `toolbar-recycle-bin-menu`, `f8-confirm`, `context-menu-recycle-bin`,
  `settings-window`, `first-run-main-window`.
