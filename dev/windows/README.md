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
- `windows-build`: build, `ctest` (every suite except the two rclone cloud
  suites, which are Linux-only), package, upload the ZIP artifact
  `panero-windows-x64`; then the Inno Setup installer from the same folder
  (`panero.iss`, artifact `panero-windows-x64-setup`).
- `windows-gui-test`: a fresh runner. It renames `C:\msys64`, strips `PATH` to
  the Windows folders, starts `panero.exe` and checks that every module loaded
  by Panero and its KIO workers comes from the ZIP or `C:\Windows`. Then it runs
  `gui-test.py` (pywinauto) on disposable files: first run straight to the
  panels, the title bar buttons (minimize, maximize, restore, close), the active
  panel's outline with each panel active (keyboard and mouse, normal and
  maximized), the shell icons of the pins, the Windows words (Recycle Bin,
  Panero Settings) and the Total Commander habits (F5–F8, F2, Insert, Del, Tab,
  Enter/Backspace, Alt+F1/F2, Shift+F6, Ctrl+click, F3/F4/F9). Phase C (D-020)
  checks the folder icons on `C:\` (panel and tree), registers a real Cloud
  Files sync root with `cloud-provider.py` and browses it (see "Cloud
  folders"). `installer-test.ps1` then installs, starts, upgrades and
  uninstalls the installer. Screenshots, logs, `gui-results.md` and
  `installer-results.md` are uploaded as `panero-windows-test-results`.
- Release (`release.yml`, tags `v*`): its `windows` job calls `windows.yml` on
  the tag; the ZIP and the installer are added to the release as
  `panero-<version>-windows-x64.zip` and `panero-<version>-windows-x64-setup.exe`,
  next to the `.deb`, and listed in `SHA256SUMS`. A failing Windows build, GUI
  test or installer test stops the release.

## Windows-specific code

- `app/compat/panero_win32_compat.h`: POSIX names that MinGW lacks (uid/gid,
  `lstat`, mode bits). Force-included on Windows only.
- `app/Panel/panerowinshell.*`: Explorer drive names and the shell icons for
  drives, folders and files (cached per extension), used by the file list, the
  folder tree, the drive button and the pins.
- `app/Panel/panerowinshellmenu.*` (preview 12, D-019): Explorer's right-click
  menu, see "Explorer right-click menu" below. Built on Windows only.
- `app/panerowording.h`: `PANERO_TEXT(linux, windows)` for the words Windows
  uses: Recycle Bin (toolbar, menus, F8/Del question and button, progress,
  settings), "Panero Settings" for the Konfigurator window and in messages,
  Windows Terminal / PowerShell and Notepad in the F9/F4 tooltips.
- `app/GUI/mediabutton.cpp`: the drive menu lists drive letters (Alt+F1 /
  Alt+F2 open the left / right list, on every platform since preview 11).
- `app/Panel/panerodrivebar.cpp` (drive buttons, off by default): drive letters
  from `QStorageInfo` in a worker thread, polled every 5 s; no cloud entries.
- `app/main.cpp`: no D-Bus on Windows, so no single-instance mode or remote
  control. Plugins are found through `qt.conf`.
- `app/krusader.cpp`: the main window is a plain `Qt::Window` on Windows (see
  below); no welcome box, no first-run Konfigurator / tool search; the default
  window size also applies on the first run.
- `app/FileSystem/panerocloudfiles.*` (preview 12, D-020): the providers' sync
  folders and their Cloud Files placeholders, see "Cloud folders" below.
- Not built on Windows: the cloud backend `app/Cloud` (rclone mounts need FUSE;
  Secret Service keyring, `panero-cloud-key`, rclone's unix-socket remote
  control) and its two test suites, the ISO worker (Linux CD-ROM ioctls) and the
  handbook. Cloud on Windows uses the OneDrive / Google Drive / Dropbox app
  folders instead (D-020). `PaneroSlowPaths` is still built (it has no cloud
  mounts to find there).
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
- **Right panel outline**: the CI screenshots (`outline-*`, with each panel
  active, by Tab and by mouse, normal and maximized) show the outline drawn on
  both panels, but the right card sat on the last pixel column of the window
  (maximized: of the screen), so the window border or the screen edge hides
  its right side and corners, while the left card sits beside the dock
  separator. Windows now keeps a 6 px margin on the right
  (`krusaderview.cpp`); the GUI test requires the outline on both sides of the
  active panel, none on the other, and at least 3 px to the window edge.
- **Windows words and first run**: see above. GUI test:
  `toolbar-recycle-bin-menu`, `f8-confirm`, `context-menu-recycle-bin`,
  `settings-window`, `first-run-main-window`.

Screenshots from the CI run that verified these fixes are in
`dev/windows/screenshots/p11-*.png` (the older ones there show the
experiment, including the dialog-style title bar it fixed).

## Preview 12 (D-020)

### Folder icons (owner report: no icon on `inetpub`, `PerfLogs`, `Windows`)

The folders without an icon were exactly the plain ones: `Program Files` and
`Users` have a `desktop.ini`, so each got its own icon; every other folder
shared one cache entry, "folder". That entry held the `QIcon` that
`QFileIconProvider` made for the *first* plain folder Panero painted: a lazy
icon engine bound to that folder's path, which asks the shell (in Qt's helper
thread, with a time-out) only when a pixmap is drawn, with the shell's overlay
handlers (OneDrive installs several). When that call gave no icon (time-out
while the shell extensions load, a folder the shell cannot read), the panel
cached the empty pixmap for every plain folder, and the tree asked the same
path again. The runner does not show the failure (the baseline run of the old
code with this GUI test, run 45155135836, draws all icons), so this is the mechanism
that fits the report, not a reproduction. Fix (`panerowinshell.cpp`): Panero
calls `SHGetFileInfo` itself; the shared folder and file-type icons come from
the name only (`SHGFI_USEFILEATTRIBUTES`, nothing is read from disk, no
overlays), per-path icons are made at once and checked, an empty result falls
back to the kind's icon and then to the theme icon, and the panel never caches
an empty pixmap. CI: `p12-c-drive-*.png`.

### Cloud folders

- Found by Panero (`PaneroCloudFiles::syncRoots`, re-read every 5 s, registry
  and settings only): registered Cloud Files sync roots
  (`HKLM` and `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager\*`,
  `DisplayNameResource`, `UserSyncRoots\<user SID>`), `%OneDrive%`,
  `%OneDriveConsumer%` ("OneDrive - Personal"), `%OneDriveCommercial%`, Google
  Drive for desktop (`HKCU\Software\Google\DriveFS` `PerAccountPreferences`
  `mount_point_path`, else `DefaultMountPoint`; the drive letter is a streaming
  drive) and Dropbox (`%LOCALAPPDATA%\Dropbox\info.json`). Only folders that
  exist are listed.
- Sidebar: a Cloud section with each folder's shell icon and name while at
  least one is connected (Quick Settings' "Show the Cloud section" is still
  hidden on Windows; the setting defaults to on). Click: open in the active
  panel; right-click: Open in Active Panel, Open in Other Panel, Pin.
- Never downloading while browsing: inside a sync folder Panero takes the file
  type from the extension only, reads no `desktop.ini` / `.directory`, extracts
  no per-file icons (`.exe`, `.lnk`, ...) from placeholders (the type's icon
  instead), makes no previews and shows nothing in the per-panel preview/view
  page for placeholders (`RECALL_ON_DATA_ACCESS` or `OFFLINE`). Everything on
  Google Drive's streaming drive counts as a placeholder. Enter, F3 and F4 open
  the file and may download it, as in Explorer.
- Badges from the attributes (the Linux drawing, `PaneroCloudFiles::decorate`):
  cloud = online only, green outline check = available on this device, green
  filled check = always kept. Folders show their own pin state.
- Context menu (all selected items inside a sync folder): "Always Keep on This
  Device" (a check box as in Explorer; `CfSetPinState` PINNED, recursive for
  folders; unchecking clears the pin) and "Free Up Space" (UNPINNED). The
  provider then downloads / frees the files. Without the Cloud Files API the
  attributes are set with `SetFileAttributes`.
- Folder tree: a sync folder is never drawn faded as hidden.
- Tests: `panerocloudfilestest` (attributes -> placeholder / badge, pin
  actions, registry / JSON parsing, merge and order, badge drawing; Linux and
  Windows). GUI test phase C: `cloud-provider.py` registers a real sync root
  with `CfRegisterSyncRoot` in the runner's profile (`PaneroTestCloud`),
  creates placeholders with `CfCreatePlaceholders` and stays connected, logging
  every `FETCH_DATA` callback with the requesting process. Checks: the Cloud
  section lists it, browsing (painting, cursor over every file, entering a
  sub-folder, context menus, both offline actions on a file and a folder)
  causes no `FETCH_DATA` from Panero and leaves every placeholder online-only;
  F3 does download (positive control). The test's registry entry is written to
  `HKCU` when `CfRegisterSyncRoot` alone creates none (OneDrive registers
  through the shell's `StorageProviderSyncRootManager`, which needs an app
  package).

### Installer

`panero.iss` (Inno Setup 6): per user without administrator rights
(`%LOCALAPPDATA%\Programs\Panero`), Start menu entry, optional desktop icon,
uninstaller, fixed `AppId` {57A1DB5D-E2D2-41F6-804E-6A302AA64E0D} so a newer
installer upgrades in place (program `plugins`, `data` and DLLs are replaced
completely; a running Panero is closed first). Settings live in the profile
(`%LOCALAPPDATA%\panerorc`, ...), so upgrades and uninstalling keep them. No
background update service. `installer-test.ps1` (fresh runner, after the GUI
test): `/VERYSILENT /CURRENTUSER` install, files, Start menu entry, `HKCU`
uninstall entry, nothing machine-wide, start from the installed folder (every
module from there or Windows), install again (one uninstall entry, settings
with a marker kept), silent uninstall (folder, shortcut and entry gone,
settings kept).

### Needs the owner's PC (real OneDrive)

- Whether the OneDrive folder in the owner's tree carries the hidden attribute
  (`attrib "%OneDrive%"` in a command prompt); Panero now draws a sync folder
  normally even then, but a hidden folder is only listed when hidden folders
  are shown.
- The names and icons Panero shows for real OneDrive / Google Drive / Dropbox
  accounts, and that OneDrive downloads / frees files after "Always Keep on
  This Device" / "Free Up Space".

### Screenshots (CI run 45159458206)

`dev/windows/screenshots/p12-*.png`: `c-drive-folders`, `c-drive-folders-end`
(Windows, Users) and `c-drive-tree` (every folder with its icon, no overlay
arrows); `cloud-section`, `cloud-entry-menu` (Open in Active / Other Panel,
Pin); `cloud-folder-badges`, `cloud-badges-zoom` (always kept: filled check,
downloaded: outline check, online only: cloud); `cloud-context-menu`,
`cloud-always-keep-folder`; `cloud-f3-download` (F3 downloads, as in
Explorer); `profile-with-cloud-folder` (the sync folder as a normal folder in
the panel and the tree); `installed-panero` (started from
`%LOCALAPPDATA%\Programs\Panero`).
