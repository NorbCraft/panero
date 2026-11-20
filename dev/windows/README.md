# Panero on Windows (experiment)

Branch `experiment/windows`. A native Windows x64 build of Panero that keeps the
twin-panel layout and the Panero look, with Windows drive, folder and file icons.
It is an experiment, not a supported platform. See `dev/DECISIONS.md` D-015.

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

`.github/workflows/ci.yml`:

- `windows-build`: build, `ctest`, package, upload the ZIP artifact
  `panero-windows-x64`.
- `windows-launch`: a fresh runner. It renames `C:\msys64`, strips `PATH` to
  the Windows folders, starts `panero.exe` and checks that every module loaded
  by Panero and its KIO workers comes from the ZIP or `C:\Windows`. Then it runs
  `gui-test.py` (pywinauto) on disposable files: the Total Commander habits
  (F5–F8, F2, Insert, Del, Tab, Enter/Backspace, Alt+F1, Shift+F6, Ctrl+click).
  Screenshots, logs and `gui-results.md` are uploaded as
  `panero-windows-test-results`.

## Windows-specific code

- `app/compat/panero_win32_compat.h`: POSIX names that MinGW lacks (uid/gid,
  `lstat`, mode bits). Force-included on Windows only.
- `app/Panel/panerowinshell.*`: Explorer drive names and the shell icons for
  drives, folders and files (cached per extension).
- `app/GUI/mediabutton.cpp`: the drive menu lists drive letters, with Alt+F1 and
  Alt+F2 as on Total Commander.
- `app/main.cpp`: no D-Bus on Windows, so no single-instance mode or remote
  control. Plugins are found through `qt.conf`.
- Not built on Windows: the ISO worker (Linux CD-ROM ioctls) and the handbook.
