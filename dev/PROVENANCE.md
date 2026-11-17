# Panero dependency and asset provenance

Inventory of everything Panero ships or relies on, where it comes from and
under which license. Companion to [`../LICENSING.md`](../LICENSING.md) (the
readable overview) and `debian/copyright` (machine-readable). Checked on
2026-11-17 against the tree of the `feature/packaging` branch (rebased on
`main` at `bfa10f64b`, which includes the commander input, appearance,
thumbnails, navigation sidebar and KDiff3 installer work) with `reuse lint` (compliant, REUSE 3.0),
`licensecheck -r`, the Git history and the copyright files of the Ubuntu
24.04 packages. Open items are listed at the end; nothing here is a legal
opinion.

## 1. Upstream base

| Item | Value |
|---|---|
| Project | Krusader, <https://krusader.org/> |
| Canonical source | <https://invent.kde.org/utilities/krusader> |
| Version / commit | tag `v2.8.1`, commit `dbc4540bc850a15d8c776b54fdecc0b326b4c469` (2024-03-03); kept in this repository as tag `krusader-v2.8.1` with full history |
| License | GPL-2.0-or-later (code), GFDL-1.1-or-later (handbook) |
| Choice | dev/DECISIONS.md D-001 and D-007 |

## 2. Bundled source code and assets (in the source tree)

### 2.1 Program code

| Group | Path | Origin | License | Installed as |
|---|---|---|---|---|
| Krusader application | `app/` | Krusader 2.8.1 (Shie Erlich, Rafi Yanai, Csaba Karai, Jan Lepper, Nikita Melnichenko and others, Krusader Krew), edited by Panero | GPL-2.0-or-later (SPDX headers) | `/usr/bin/panero` |
| Disk usage map | `app/DiskUsage/filelightParts/`, `app/DiskUsage/radialMap/` | Filelight by Max Howell (2003-2004), adapted by Krusader | GPL-2.0-or-later | in `/usr/bin/panero` |
| Fragments from other KDE code | `plugins/iso/iso.*`, `plugins/iso/kiso.*` ("heavily based on ktar from kdelibs"), `app/FileSystem/defaultfilesystem.cpp` (`.hidden` parsing from KIO, Bruno Nova), `app/GUI/terminaldock.cpp`, `app/Panel/PanelView/krinterdetailedview.cpp`, `app/Panel/panelcontextmenu.cpp` (Dolphin), `app/GUI/krremoteencodingmenu.*` (KRemoteEncodingPlugin, Dawit Alemayehu), `app/paneltabbar.*` (Sebastian Trueg) | noted in the file headers/comments | files are GPL-2.0-or-later; the originals were GPL/LGPL, which permit this | in the binaries |
| krarc KIO worker | `plugins/krarc/` | Krusader | GPL-2.0-or-later | `/usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/kio_krarc.so` |
| iso KIO worker | `plugins/iso/` | Krusader | GPL-2.0-or-later | `.../kio_iso.so`, `/etc/xdg/panero_kio_isorc` |
| libisofs 0.2 | `plugins/iso/libisofs/` | Szombathelyi György, <http://libcdrom.sourceforge.net/libisofs.html>, imported into Krusader 2004 (`dd9d71dd2`); `iso_fs.h`, `rock.h` "From the linux kernel", `el_torito.h` "borrowed from the bsd386 isofs", `bswap.h` "From the mplayer project" with glibc `bits/byteswap.h` lines | declared GPL-2.0-or-later (see open item O-1); `COPYING` = GPL-2.0 text | in `kio_iso.so` |
| Panero additions | `app/paneroimport.*` (settings import), `app/paneronavigation.*` (sidebar, folder tree, pins), `app/paneropackageinstaller.*` (KDiff3 install via PackageKit D-Bus), `app/Panel/panerotheme.*`, `app/Panel/paneroappearance.*`, `app/Dialogs/paneroviewcolorsdialog.*` (appearance), `app/Panel/PanelView/commandergesture.*` (cursor/marks, hold-for-menu), `app/Panel/PanelView/krinterthumbnailview.*` (thumbnails view), edits in Krusader files, `autotests/` | Panero contributors, 2026 (all new files: SPDX headers, GPL-2.0-or-later; written for Panero, no third-party code noted) | GPL-2.0-or-later | `/usr/bin/panero` (tests not installed) |

### 2.2 Icons, images and artwork

| Group | Path | Origin | License | Installed as |
|---|---|---|---|---|
| Panero application icon (SVG master, small variant, PNG 16-256) | `app/icons/panero/` | drawn from scratch for Panero (2026-11-17) after the owner-selected, AI-generated concept "icon A"; see [`ICON-PROVENANCE.md`](ICON-PROVENANCE.md) | GPL-2.0-or-later; no exclusivity or trademark claimed | `/usr/share/icons/hicolor/{16x16..256x256,scalable}/apps/io.github.norbcraft.panero.*` |
| Fallback "missing icon" | `app/icons/icon-missing.svgz` | Nikita Melnichenko 2018, based on the Breeze icons "unknown" and "edit-select-none" (upstream commit `86115e3b6`); the previous version (`e1828f60c`) derived from openclipart "ftfile-broken" (public domain) is no longer in the tree | LGPL-3.0-or-later (Breeze license) | compiled into `/usr/bin/panero` (Qt resource) |
| Krusader action icons `kr_combine`, `kr_comparedirs`, `kr_diskusage`, `kr_mountman`, `kr_syncbrowse_on/off`, `kr_unselect` (17 PNGs, 16/22/32 px) | `app/resources/*.png` | Krusader Krew, since 2003 (e.g. `09479eb73`, Csaba Karai); no per-file notice | GPL-2.0-or-later (project license) | `/usr/share/panero/icons/hicolor/*/actions/` (private, not in the global theme) |
| Krusader SVG artwork (logos, jump-back, contest icons) | `app/resources/SVGs/` | Shie Erlich 2005 (`460b216d5`) | GPL-2.0-or-later (project license) | not installed |
| Krusader repository logo | `logo.png` | Krusader icon, added 2020 (`4a969832f`) | GPL-2.0-or-later (project license) | not installed, not used as Panero branding |
| Handbook screenshots (18) | `doc/handbook/*.png` | Krusader handbook (2002 onwards), showing the Krusader UI | GFDL-1.1-or-later | `/usr/share/doc/HTML/en/panero/` |
| Translated handbook screenshots (12) | `po/uk/docs/panero/*.png` | KDE Ukrainian translation team | GFDL-1.1-or-later | `/usr/share/doc/HTML/uk/panero/` |
| Development hand-off references | `dev/handoff/references/` (`icon-a-reference.png`, `details-*.png`, `thumbnails.png`, `krusader-concept.html`) | supplied by the project owner; the icon concept is AI generated; the HTML prototype has no external fonts, scripts or images | GPL-2.0-or-later per `.reuse/dep5` | not installed (source archive only) |

No fonts are bundled. Icons used at runtime other than the above come
from the user's icon theme or from the `breeze-icon-theme` package.

### 2.3 Data, configuration and documentation

| Group | Path | Origin | License | Installed as |
|---|---|---|---|---|
| Colour schemes | `app/total_commander.color` (2005, Jonas Bähr via Shie Erlich), `app/midnight_commander.color` (2005, Shie Erlich) | Krusader | GPL-2.0-or-later | `/usr/share/panero/` |
| Total Commander keymap + note | `app/total_commander.keymap`, `.keymap.info` | Krusader, Shie Erlich 2004 | GPL-2.0-or-later | `/usr/share/panero/` |
| User action examples, layout | `app/useraction_examples.xml` (Jonas Bähr 2005), `app/layout.xml` | Krusader | GPL-2.0-or-later | `/usr/share/panero/` |
| KXMLGUI definitions | `app/panero*.rc` (renamed from `krusader*.rc`) | Krusader, edited by Panero | GPL-2.0-or-later | `/usr/share/kxmlgui5/panero/` |
| Desktop entry | `app/io.github.norbcraft.panero.desktop` | derived from Krusader's (translated comments), edited by Panero | GPL-2.0-or-later | `/usr/share/applications/` |
| AppStream metainfo | `app/io.github.norbcraft.panero.metainfo.xml` | derived from Krusader's appdata | FSFAP | `/usr/share/metainfo/` |
| UI translations (51 languages) | `po/*/panero.po` | KDE translation teams for Krusader (renamed catalogs) | GPL-2.0-or-later ("same license as the Krusader package") | `/usr/share/locale/*/LC_MESSAGES/panero.mo` |
| Handbook and man page | `doc/handbook/` | Krusader handbook: Shie Erlich, Rafi Yanai, Frank Schoolmeesters, Krusader Krew | GFDL-1.1-or-later, no invariant sections | `/usr/share/doc/HTML/en/panero/`, `/usr/share/man/man1/panero.1.gz` |
| Translated handbooks (12 directories; 7 languages complete enough to install: ca, de, it, nl, pt, sv, uk) | `po/*/docs/panero/` | KDE translation teams | GFDL-1.1-or-later | `/usr/share/doc/HTML/<lang>/panero/` |
| Panero documentation, user guide, packaging, CI, scripts | `*.md` (incl. `USER-GUIDE.md`), `debian/`, `.github/`, `dev/` | Panero contributors | GPL-2.0-or-later | `README.md`, `USER-GUIDE.md`, `LICENSING.md`, `copyright` in `/usr/share/doc/panero/` |

## 3. Build-time tools (not shipped)

Ubuntu 24.04 packages from `debian/control` `Build-Depends` (see
`dev/build-deps.txt`): GCC 13, CMake, Ninja, extra-cmake-modules,
gettext, Qt 5.15 and KDE Frameworks 5.115 development files, debhelper.
`kdoctools` (meinproc5) converts the handbook docbook into the man page;
Qt `rcc` embeds `layout.xml` and `icon-missing.svgz` into the executable.
The release build runs in the official `ubuntu:24.04` container image; the
image digest is recorded in each build's `BUILD-INFO.txt` and in
`SOURCE-INFO.txt` of the source release (e.g. on 2026-11-17:
`ubuntu@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8`).

## 4. Runtime dependencies (installed from Ubuntu, not bundled)

Versions are those of Ubuntu 24.04 "noble" (with updates) on 2026-11-17.
"License (copyright file)" lists the main `License:` entries of
`/usr/share/doc/<package>/copyright`; most files of each package use the
first one.

### 4.1 Depends

| Ubuntu package | Source package / version | Used for | License (copyright file) |
|---|---|---|---|
| `libqt5core5t64`, `libqt5gui5t64`, `libqt5widgets5t64`, `libqt5dbus5t64`, `libqt5xml5t64`, `libqt5printsupport5t64` | qtbase-opensource-src 5.15.13+dfsg-1ubuntu1 | Qt 5 toolkit | LGPL-3 or GPL-2 (Qt Company); parts BSD-3-clause, Expat |
| `libkf5archive5` | karchive 5.115.0 | archive access | LGPL-2+ (some LGPL-2.1+/3, BSD-2-clause) |
| `libkf5bookmarks5` | kbookmarks 5.115.0 | bookmarks | LGPL-2+ |
| `libkf5codecs5` | kcodecs 5.115.0 | encodings | LGPL-2+ (parts GPL-2+, Expat, BSD-3-clause) |
| `libkf5completion5` | kcompletion 5.115.0 | completion | LGPL-2+ |
| `libkf5configcore5`, `libkf5configgui5` | kconfig 5.115.0 | settings | LGPL-2+ (parts MIT) |
| `libkf5configwidgets5` | kconfigwidgets 5.115.0 | settings UI | LGPL-2+ |
| `libkf5coreaddons5` | kcoreaddons 5.115.0 | core utilities | LGPL-2+ |
| `libkf5guiaddons5`, `libkf5guiaddons-bin` | kguiaddons 5.115.0 | GUI utilities | LGPL-2+ |
| `libkf5i18n5` | ki18n 5.115.0 | translations | LGPL-2+ |
| `libkf5iconthemes5` | kiconthemes 5.115.0 | icon loading | LGPL-2+ |
| `libkf5itemviews5` | kitemviews 5.115.0 | views | LGPL-2+ |
| `libkf5jobwidgets5` | kjobwidgets 5.115.0 | job progress | LGPL-2+ |
| `libkf5kiocore5`, `libkf5kiogui5`, `libkf5kiowidgets5`, `libkf5kiofilewidgets5` | kio 5.115.0 | file I/O framework | LGPL-2+ (parts LGPL-2.1+, MIT/X11) |
| `libkf5notifications5` | knotifications 5.115.0 | notifications | LGPL-2+ |
| `libkf5parts5` | kparts 5.115.0 | viewer/editor parts | LGPL-2+ |
| `libkf5service5`, `libkf5service-bin` | kservice 5.115.0 | services/plugins | LGPL-2+ |
| `libkf5solid5` | solid 5.115.0 | devices | LGPL-2.1+ |
| `libkf5textwidgets5` | ktextwidgets 5.115.0 | text widgets | LGPL-2+ |
| `libkf5wallet5`, `libkf5wallet-bin` | kwallet-kf5 5.115.0 | password storage | LGPL-2+ |
| `libkf5widgetsaddons5` | kwidgetsaddons 5.115.0 | widgets | LGPL-2+ |
| `libkf5windowsystem5` | kwindowsystem 5.115.0 | window system | LGPL-2+ (parts Expat) |
| `libkf5xmlgui5` | kxmlgui 5.115.0 | menus/toolbars | LGPL-2+ |
| `libacl1` | acl 2.3.2 | POSIX ACLs | LGPL-2+ (tools GPL-2+) |
| `libc6` | glibc 2.39 | C library | LGPL-2.1+ (non-DEP-5 copyright file; some parts other free licenses) |
| `libstdc++6` | gcc-14 14.2.0 | C++ runtime | GPL-3 with GCC Runtime Library Exception |
| `zlib1g` | zlib 1.3 | compression | Zlib |
| `kio` | kio 5.115.0 | KIO workers (file, trash, ftp, http, ...), `kioslave5`, `kiod5` | LGPL-2+ (parts LGPL-2.1+, MIT/X11) |
| `kinit` | kinit 5.115.0 | klauncher used by KF5 KIO to start workers | LGPL-2+ |
| `ktexteditor-katepart` | ktexteditor 5.115.0 | internal text editor (KTextEditor part) | LGPL-2+ (parts LGPL-2.1+, GPL-2) |
| `breeze-icon-theme` | breeze-icons 5.115.0 | icon fallback theme | LGPL-3+ |

### 4.2 Recommends

| Ubuntu package | Source / version | Used for | License (copyright file) |
|---|---|---|---|
| `kio-extras` | kio-extras 23.08.5 | sftp, smb, fish, mtp, man/info, thumbnail worker and basic thumbnailers | GPL-2.0-or-later, LGPL-2.0-or-later (parts MIT, W3C) |
| `kdegraphics-thumbnailers` | kdegraphics-thumbnailers 23.08.5 | PDF/PostScript/RAW/Blender previews | GPL-2+ |
| `ffmpegthumbs` | ffmpegthumbs 23.08.5 | video previews | GPL-2+ (parts CC0-1.0, BSD-3-clause) |
| `kimageformat-plugins` | kimageformats 5.115.0 | additional image formats (AVIF, HEIF, JXL, PSD, XCF, ...) | LGPL-2+ (parts LGPL-2.1+) |
| `kde-cli-tools` | kde-cli-tools 5.27.12 | `kdesu` for root mode and root user actions | GPL-2+ (parts LGPL-2+) |
| `packagekit` | packagekit 1.2.8 | installing KDiff3 on request through PackageKit/polkit | GPL-2+ (parts LGPL-2.1+) |

### 4.3 Suggests (all optional, detected at runtime)

`kdiff3` (default compare tool; Panero offers to install it through
PackageKit), `kompare | xxdiff`, `7zip`, `arj`, `ark`, `bzip2`, `cpio`,
`hashdeep`, `kate`, `konsole` (embedded terminal), `krename`, `lhasa`,
`okteta` (hex viewer), `qt5-gtk-platformtheme` (GTK dialogs/fonts when
Panero's own look is off), `rpm`, `unace`, `unrar | unrar-free | rar`,
`unzip`, `zip`.

### 4.4 Provided by the desktop (not declared)

- The **Fusion** widget style used by Panero's look is built into Qt
  (`libqt5widgets5t64`); nothing extra is needed.
- Light/dark detection asks the XDG desktop portal
  (`org.freedesktop.portal.Settings`, `xdg-desktop-portal`) and falls back
  to `gsettings` (`libglib2.0-bin`); both are part of Zorin/Ubuntu desktops.
  Without them Panero uses its light look.
- The **polkit authentication agent** that authorises a PackageKit
  installation comes with the desktop session (GNOME Shell on Zorin). Their licenses are in their own
copyright files; none of them is required to install or run Panero.

## 5. Open items

- **O-1 libisofs headers.** `iso_fs.h` and `rock.h` say "From the linux
  kernel" (kernel: GPL-2.0-only), `el_torito.h` "borrowed from the bsd386
  isofs", `bswap.h` "From the mplayer project". The original 2004 import had
  no license notice in these four files; the GPL-2.0-or-later SPDX headers
  were added later by upstream. Treated as: `kio_iso.so` may be GPL-2.0-only,
  which is compatible with distributing Panero under GPL-2.0-or-later.
  Unresolved upstream question; not checked against the historical kernel
  and MPlayer sources.
- **O-2 Breeze-derived fallback icon.** The LGPL-3.0-or-later status of
  `icon-missing.svgz` rests on the upstream commit message; the file itself
  carries no license metadata. LGPL-3.0 obligations (notice, license texts,
  modifiable source) are met by `debian/copyright`, `LICENSES/` and the
  source release. The GPL-3.0 text it references is not in `LICENSES/`
  (REUSE would flag it as unused); it is in the release licenses archive and
  in `/usr/share/common-licenses/GPL-3`.
- **O-3 Files without per-file notices** (Krusader artwork, data and build
  files) are attributed to the Krusader Krew under Krusader's project
  license in `.reuse/dep5`, as the Ubuntu krusader package does; individual
  authors of these files were not traced beyond the Git history.
- **O-4 Translators** are named in each `.po`/docbook header but not
  enumerated in `debian/copyright` (it refers to the file headers).
- **O-5 AI-generated concept and Panero icon.** Copyright protection of
  AI-assisted artwork may be limited; no trademark search was done for the
  name or icon; other software named "PANERO" exists.
- **O-6 Handbook content** still documents and shows Krusader (screenshots,
  names); this is attribution-correct but not yet adapted to Panero.
