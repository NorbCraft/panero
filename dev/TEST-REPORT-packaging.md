# Packaging test report

Date: 2026-11-17. Author: Claude Code (Opus 5.5), `feature/packaging`.
Everything below was run in Docker containers on the Zorin OS 18.1 host;
results are copied verbatim from `dist/build.log`, `dist/lintian.txt` and
`dist/test-deb.log` (only the result lines are kept).

## Package under test

| Item | Value |
|---|---|
| File | `panero_0.1.0~preview.1-1_amd64.deb` (3 959 262 bytes, Installed-Size 16161 KiB) |
| SHA-256 | `221f128ea32bda925adbfad8fc0d87d8ac239ef963a13c7c50c44c1a051e6586` |
| Built from | commit `1f417981d836fa9240f04f2b02413b58a2da3790` (`feature/packaging`, rebased on `main` `bfa10f64b`) via `git archive` |
| Build command | `dev/scripts/build-deb.sh` → `dpkg-buildpackage -us -uc -b` in a fresh container |
| Image | `ubuntu@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8` (`ubuntu:24.04`) |
| Old package for the upgrade test | `panero_0.1.0~preview.0-1_amd64.deb`, built with `build-deb.sh --worktree --version 0.1.0~preview.0-1` from the pre-rebase tree (different file set: gzipped handbook, no user guide) |

The commits after `1f417981d` only add this report and the plan/log
updates; the packaged tree is unchanged.

## Build and autotests (dh_auto_test → ctest)

```
1/3 Test #1: paneroimporttest .................   Passed    0.05 sec
2/3 Test #2: commandergesturetest .............   Passed    0.00 sec
3/3 Test #3: panerothemetest ..................   Passed    0.03 sec
100% tests passed, 0 tests failed out of 3
```

Generated control fields (shared-library dependencies from `dh_shlibdeps`):

```
Depends: breeze-icon-theme, kinit, kio, ktexteditor-katepart, libacl1 (>= 2.2.23), libc6 (>= 2.38), libkf5archive5 (>= 5.85.0), libkf5bookmarks5 (>= 4.96.0), libkf5codecs5 (>= 4.96.0), libkf5completion5 (>= 5.81.0), libkf5configcore5 (>= 4.97.0), libkf5configgui5 (>= 4.97.0), libkf5configwidgets5 (>= 4.98.0), libkf5coreaddons5 (>= 5.37.0), libkf5guiaddons-bin, libkf5guiaddons5 (>= 4.96.0), libkf5i18n5 (>= 4.97.0), libkf5iconthemes5 (>= 5.11.0), libkf5itemviews5 (>= 4.96.0), libkf5jobwidgets5 (>= 5.70.0), libkf5kiocore5 (>= 5.96.0), libkf5kiofilewidgets5 (>= 4.96.0), libkf5kiogui5 (>= 5.83.0), libkf5kiowidgets5 (>= 5.92.0), libkf5notifications5 (>= 5.12.0), libkf5parts5 (>= 5.81.0), libkf5service-bin, libkf5service5 (>= 5.69.0), libkf5solid5 (>= 4.97.0), libkf5textwidgets5 (>= 4.96.0), libkf5wallet-bin, libkf5wallet5 (>= 4.96.0), libkf5widgetsaddons5 (>= 5.100.0), libkf5windowsystem5 (>= 4.96.0), libkf5xmlgui5 (>= 5.84.0), libqt5core5t64 (>= 5.15.1), libqt5dbus5t64 (>= 5.14.1), libqt5gui5t64 (>= 5.12.2) | libqt5gui5-gles (>= 5.12.2), libqt5printsupport5t64 (>= 5.0.2), libqt5widgets5t64 (>= 5.15.1), libqt5xml5t64 (>= 5.0.2), libstdc++6 (>= 13.1), zlib1g (>= 1:1.1.4)
Recommends: ffmpegthumbs, kde-cli-tools, kdegraphics-thumbnailers, kimageformat-plugins, kio-extras, packagekit
Suggests: 7zip, arj, ark, bzip2, cpio, hashdeep, kate, kdiff3, kompare | xxdiff, konsole, krename, lhasa, okteta, qt5-gtk-platformtheme, rpm, unace, unrar | unrar-free | rar, unzip, zip
```

## Lintian

`lintian --display-info --display-experimental --pedantic --show-overrides`
(Lintian v2.117.0ubuntu1.6, run as an unprivileged user on the `.changes`).
No E/W/I/P/X tags; only these overridden ones (justifications in
`debian/panero.lintian-overrides`):

```
O: panero: arch-dep-package-has-big-usr-share 12654kB 80%
O: panero: extra-license-file [usr/share/doc/panero/LICENSES/GPL-2.0-or-later.txt.gz]
O: panero: extra-license-file [usr/share/doc/panero/LICENSES/LGPL-3.0-or-later.txt.gz]
O: panero: initial-upload-closes-no-bugs [usr/share/doc/panero/changelog.Debian.gz:1]
O: panero: package-contains-documentation-outside-usr-share-doc [usr/share/panero/total_commander.keymap.info]
O: panero: spelling-error-in-binary Differents Different [usr/bin/panero]
O: panero: spelling-error-in-binary Entrys Entries [usr/bin/panero]
```

Fixed during the work: `debian/rules` not executable; lintian ran as root;
handbook `.docbook` files gzipped by `dh_compress` (would break
`help:/panero`; now excluded, as in the Ubuntu krusader package).

## Container package tests (`dev/scripts/test-deb.sh`)

Command:
`dev/scripts/test-deb.sh --deb dist/panero_0.1.0~preview.1-1_amd64.deb --old-deb dist/old/panero_0.1.0~preview.0-1_amd64.deb`
(exit status 0). Each scenario runs in its own fresh `ubuntu:24.04`
container without build dependencies or source tree; the image's dpkg
"minimize" excludes are removed first so docs, man pages and translations
install as on a desktop.

```
Panero package test: 2026-11-17T19:36:01Z
package: panero_0.1.0~preview.1-1_amd64.deb (sha256 221f128ea32bda925adbfad8fc0d87d8ac239ef963a13c7c50c44c1a051e6586)
old package: panero_0.1.0~preview.0-1_amd64.deb
image: ubuntu@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8
=== scenario: install (ubuntu:24.04) ===
INFO: apt install ./panero_0.1.0~preview.1-1_amd64.deb (with Recommends, as a desktop user would)
PASS: apt install ./panero_0.1.0~preview.1-1_amd64.deb resolves all dependencies from the Ubuntu archive
INFO: packages newly installed: 720
PASS: dpkg status is 'install ok installed'
INFO: installed version: 0.1.0~preview.1-1
PASS: dependency/recommendation kio installed
PASS: dependency/recommendation kinit installed
PASS: dependency/recommendation ktexteditor-katepart installed
PASS: dependency/recommendation breeze-icon-theme installed
PASS: dependency/recommendation kde-cli-tools installed
PASS: dependency/recommendation kio-extras installed
PASS: dependency/recommendation packagekit installed
PASS: kdiff3 NOT pulled in (optional)
PASS: krusader NOT pulled in (optional)
INFO: desktop launchers added by Panero and its dependencies (excluding NoDisplay):
    io.github.norbcraft.panero.desktop: Name=Panero 
    kdesystemsettings.desktop: Name=KDE System Settings NotShowIn=KDE;
    systemsettings.desktop: Name=System Settings OnlyShowIn=KDE;
    zutty.desktop: Name=Zutty 
    panero 0.1.0-preview.1
PASS: panero --version prints the Panero version
PASS: panero --help describes Panero as a fork of Krusader
PASS: installed: /usr/bin/panero
PASS: installed: /usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/kio_krarc.so
PASS: installed: /usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/kio_iso.so
PASS: installed: /etc/xdg/panero_kio_isorc
PASS: installed: /usr/share/applications/io.github.norbcraft.panero.desktop
PASS: installed: /usr/share/metainfo/io.github.norbcraft.panero.metainfo.xml
PASS: installed: /usr/share/icons/hicolor/scalable/apps/io.github.norbcraft.panero.svg
PASS: installed: /usr/share/icons/hicolor/16x16/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/icons/hicolor/22x22/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/icons/hicolor/24x24/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/icons/hicolor/32x32/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/icons/hicolor/48x48/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/icons/hicolor/64x64/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/icons/hicolor/128x128/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/icons/hicolor/256x256/apps/io.github.norbcraft.panero.png
PASS: installed: /usr/share/kxmlgui5/panero/paneroui.rc
PASS: installed: /usr/share/panero/layout.xml
PASS: installed: /usr/share/locale/de/LC_MESSAGES/panero.mo
PASS: installed: /usr/share/doc/HTML/en/panero/index.docbook
PASS: installed: /usr/share/man/man1/panero.1.gz
PASS: installed: /usr/share/doc/panero/copyright
PASS: installed: /usr/share/doc/panero/changelog.Debian.gz
PASS: installed: /usr/share/doc/panero/LICENSES/GPL-2.0-or-later.txt.gz
PASS: installed: /usr/share/doc/panero/LICENSES/GFDL-1.1-or-later.txt.gz
PASS: installed: /usr/share/doc/panero/LICENSES/LGPL-3.0-or-later.txt.gz
PASS: installed: /usr/share/doc/panero/LICENSES/FSFAP.txt
PASS: installed: /usr/share/doc/panero/LICENSING.md[.gz]
PASS: installed: /usr/share/doc/panero/USER-GUIDE.md[.gz]
PASS: installed: /usr/share/doc/panero/README.md[.gz]
PASS: handbook docbook files are not compressed (help:/panero)
PASS: copyright file is DEP-5 and names GPL-2+
PASS: copyright file carries the fork statement
PASS: /etc/xdg/panero_kio_isorc is a conffile
PASS: all shared libraries resolve for panero
PASS: all shared libraries resolve for kio_iso.so
PASS: all shared libraries resolve for kio_krarc.so
PASS: no file outside the expected Panero paths
PASS: desktop-file-validate
PASS: appstreamcli validate (no errors)
    krarc: hello panero "/tmp/kio/test.zip/hello.txt"
PASS: kio_krarc.so reads a file from a zip archive
    iso: <empty> 
KNOWN: kio_iso.so reads a file from an ISO image -- Krusader 2.8.1's kio_iso does not recognise noble's application/vnd.efi.iso mime type (distro krusader behaves the same); fixed upstream in 4c85272cd and c6945e68e, backport pending
INFO: GUI smoke start exit status 124 (124 = still running after 15 s)
PASS: GUI starts offscreen without crashing
PASS: no missing-editor (katepart) warning
RESULT: 0 failure(s)
=== scenario: coexist (ubuntu:24.04) ===
INFO: install the Ubuntu krusader package first, then Panero
PASS: apt install krusader (Ubuntu archive)
INFO: krusader version: 2:2.8.1-0ubuntu3
PASS: apt install ./panero_0.1.0~preview.1-1_amd64.deb next to krusader (no file conflicts)
PASS: no file is shared between krusader and panero
PASS: dpkg --verify krusader (krusader files untouched)
PASS: dpkg --verify panero
PASS: krusader --version still works
    panero 0.1.0-preview.1
PASS: panero --version works next to krusader
PASS: apt remove panero
PASS: krusader still intact after removing panero
INFO: reverse order: purge krusader, reinstall panero first, then krusader
PASS: apt purge krusader
PASS: apt install ./panero_0.1.0~preview.1-1_amd64.deb
PASS: apt install krusader after panero
PASS: dpkg --verify panero after krusader install
RESULT: 0 failure(s)
=== scenario: upgrade (ubuntu:24.04) ===
PASS: apt install ./panero_0.1.0~preview.0-1_amd64.deb (old version)
INFO: installed: 0.1.0~preview.0-1
PASS: apt install ./panero_0.1.0~preview.1-1_amd64.deb upgrades the package
INFO: installed: 0.1.0~preview.1-1
PASS: version changed 0.1.0~preview.0-1 -> 0.1.0~preview.1-1 and dpkg considers it newer
PASS: dpkg --verify panero after upgrade
PASS: no Panero file lost in the upgrade
    panero 0.1.0-preview.1
PASS: panero --version after upgrade
PASS: user configuration untouched by the upgrade
RESULT: 0 failure(s)
=== scenario: remove (ubuntu:24.04) ===
PASS: apt install ./panero_0.1.0~preview.1-1_amd64.deb
PASS: apt remove panero
PASS: status after remove is config-files
    left after remove: /etc/xdg/panero_kio_isorc
PASS: after remove only the conffile /etc/xdg/panero_kio_isorc remains
PASS: apt purge panero
PASS: package unknown to dpkg after purge
PASS: no file of the package remains after purge
    (none)
PASS: no path named *panero* left on the system after purge
RESULT: 0 failure(s)
=== summary ===
PASS: 83  FAIL: 0  KNOWN: 1  SKIP: 0
```

The `krusader --version` check printed `krusader 2.8.1 "A New Day"`.

## Findings

- **No file collisions with krusader** in either install order; removing
  Panero leaves krusader intact (`dpkg --verify`). Before packaging, a
  staged `DESTDIR` install was also compared with `dpkg -L krusader` on the
  host: no common file.
- **Upgrade** from an older Panero package works; user configuration in
  `~/.config/panerorc` is untouched.
- **Remove/purge**: after `apt remove` only the conffile
  `/etc/xdg/panero_kio_isorc` remains; after `apt purge` nothing named
  `*panero*` is left on the system.
- **KNOWN, not a packaging problem: ISO images do not open.** `kio_iso` of
  Krusader 2.8.1 hangs or crashes on ISO images on noble because it does not
  know the `application/vnd.efi.iso` mime type; the Ubuntu krusader
  package's own `kio_iso` behaves the same in the same container. Upstream
  fixed it in `4c85272cd` and `c6945e68e` (backport needed in
  `plugins/iso`). The test reports PASS automatically once fixed.
- **Footprint.** In a bare container `apt install` with Recommends adds 720
  packages (without Recommends the container holds 339 packages in total,
  ~90 of them from the base image); on a Zorin desktop many are already
  present. Visible side effect of the Recommends: `kde-cli-tools`
  recommends `systemsettings`, which adds a "KDE System Settings" launcher
  (`NotShowIn=KDE`, so it shows on GNOME/Zorin). The Ubuntu krusader
  package has the same Recommends (the owner's Zorin host already has
  `systemsettings` through krusader). `zutty` only appears because the bare
  container has no terminal emulator; on a desktop `gnome-terminal` already
  provides `x-terminal-emulator`.
- `desktop-file-validate` passes with one hint (Categories could add
  `System`); `appstreamcli validate --no-net` passes.

## Not covered here

- Real desktop session: launcher/icon in the Zorin menu, GNOME Software /
  Zorin's graphical package installer, Wayland and X11, the polkit dialog of
  the PackageKit KDiff3 flow. Container tests are not a desktop test; these
  need the Zorin host or a VM (D-006).
- The release workflow has not run on GitHub yet (no tag may be created
  from this branch). Its asset assembly (`git archive --add-file` source
  archive with `SOURCE-INFO.txt`, licenses archive, `SHA256SUMS` with the
  `~`→`.` asset name) was dry-run locally against commit `1f417981d`; all
  three checksums verified.
