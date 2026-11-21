# Panero licensing and attribution

Panero is an independent fork of Krusader. It is not affiliated with or endorsed by KDE or the Krusader maintainers.

This document is a readable overview. The authoritative information is in
the per-file SPDX headers, in `.reuse/dep5` for files without headers, in the
license texts in `LICENSES/`, and in the machine-readable Debian copyright
file `debian/copyright` (installed as `/usr/share/doc/panero/copyright`).
The tree passes `reuse lint` (REUSE Specification 3.0). The inventory of
bundled assets and runtime dependencies is in
[`dev/PROVENANCE.md`](dev/PROVENANCE.md).

## The short version

- Panero is based on **Krusader 2.8.1**, which its authors publish under the
  **GNU General Public License, version 2 or (at your option) any later
  version** (GPL-2.0-or-later). Panero is a derivative work and is
  distributed under the same terms. Panero's own changes and new files are
  also GPL-2.0-or-later. The fork is not relicensed.
- The user manual (handbook and man page) is under the **GNU Free
  Documentation License 1.1 or any later version** with no Invariant
  Sections, no Front-Cover Texts and no Back-Cover Texts.
- A few individual files carry other GPL-compatible licenses (see the table).
- Panero does **not bundle** Qt, KDE Frameworks or any other runtime
  library. The `.deb` package depends on the libraries that Ubuntu 24.04
  (and Zorin OS 18.1) already ship; those packages carry their own
  copyright files and their source is available from the Ubuntu archive.
- There is no warranty, as stated in the GPL.

## Components and their licenses

| Component | Where | License | Copyright |
|---|---|---|---|
| Program sources (Krusader-derived) | `app/`, `plugins/krarc/`, `plugins/iso/*.cpp` | GPL-2.0-or-later | Shie Erlich, Rafi Yanai, Csaba Karai, Jan Lepper, Nikita Melnichenko and the Krusader Krew (see headers) |
| Panero additions (commander input/marks, appearance and View & Colors, thumbnails view, navigation sidebar and pins, KDiff3 install via PackageKit, settings import) | `app/panero*`, `app/Panel/panero*`, `app/Panel/PanelView/commandergesture.*`, `app/Panel/PanelView/krinterthumbnailview.*`, `app/Dialogs/paneroviewcolorsdialog.*`, plus edits in Krusader files | GPL-2.0-or-later | Panero contributors |
| Disk usage map (from Filelight) | `app/DiskUsage/filelightParts/`, `app/DiskUsage/radialMap/` | GPL-2.0-or-later | Max Howell, Krusader Krew |
| libisofs 0.2 (ISO 9660 reader for the `iso:/` worker) | `plugins/iso/libisofs/` | GPL-2.0-or-later as declared (see note 1) | Szombathelyi György, Krusader Krew |
| Fallback "missing icon", compiled into `panero` | `app/icons/icon-missing.svgz` | LGPL-3.0-or-later (see note 2) | Nikita Melnichenko; Breeze icon theme authors |
| Krusader action icons and SVG artwork | `app/resources/` | GPL-2.0-or-later (no per-file notice; project license) | Krusader Krew |
| "Panero Line" toolbar/sidebar icons (Lucide 0.460.0, recoloured and renamed) | `app/icons/panero-line/` | ISC | Cole Bemis (portions from Feather), Lucide Contributors; selection/colours Panero contributors |
| Panero application icon | `app/icons/panero/` | GPL-2.0-or-later | Panero contributors (see [`dev/ICON-PROVENANCE.md`](dev/ICON-PROVENANCE.md)) |
| AppStream metainfo | `app/io.github.norbcraft.panero.metainfo.xml` | FSFAP | Panero contributors, Krusader Krew |
| Handbook and man page | `doc/` | GFDL-1.1-or-later (no invariant sections) | Shie Erlich, Rafi Yanai, Frank Schoolmeesters, Krusader Krew |
| Translated handbooks | `po/*/docs/` | GFDL-1.1-or-later (no invariant sections) | Krusader authors and KDE translators |
| User-interface translations | `po/*/panero.po` | GPL-2.0-or-later | KDE translators (named in each file) |
| Colour schemes, keymaps, user action examples, UI layout files | `app/*.color`, `app/*.keymap*`, `app/*.rc`, `app/layout.xml`, `app/useraction_examples.xml` | GPL-2.0-or-later | Krusader Krew; Panero contributors for renamed/edited files |
| Panero tests, packaging, CI, scripts, user guide, development documents | `autotests/`, `debian/`, `.github/`, `dev/`, `*.md` (incl. `USER-GUIDE.md`) | GPL-2.0-or-later | Panero contributors |
| Development hand-off material | `dev/handoff/` | GPL-2.0-or-later | Panero project owner (the icon concept image in it is AI generated) |
| rclone (bundled program for cloud drives, unmodified upstream build) | not in the source tree; pinned in `dev/rclone/` | MIT (`LICENSES/MIT.txt`); the Go modules compiled into it carry their own free licenses (see note 4) | Nick Craig-Wood and the rclone contributors |

Notes:

1. **libisofs headers.** The SPDX headers of all libisofs files declare
   GPL-2.0-or-later. When the code was imported into Krusader in 2004
   (upstream commit `dd9d71dd2`), `iso_fs.h` and `rock.h` carried only the
   remark "From the linux kernel", `el_torito.h` "borrowed from the bsd386
   isofs", and `bswap.h` "From the mplayer project" (with a few lines from
   glibc's `bits/byteswap.h`, LGPL-2.1-or-later). The Linux kernel is
   licensed GPL-2.0-only. If those headers are regarded as derived from the
   kernel, the `kio_iso.so` worker built from them can be redistributed under
   GPL version 2 only. This is compatible with Panero's GPL-2.0-or-later
   terms and does not affect the rest of Panero; it is recorded here as an
   open provenance question inherited from upstream.
2. **Breeze-derived fallback icon.** According to upstream Krusader commit
   `86115e3b6` (2018), `icon-missing.svgz` "is based on 'unknown' and
   'edit-select-none' icons from Breeze and published under the same license
   as Breeze", which is LGPL-3.0-or-later. It is embedded in the `panero`
   executable as a Qt resource. The LGPL-3.0 incorporates the GPL-3.0; its
   text is in `LICENSES/LGPL-3.0-or-later.txt`, and the GPL-3.0 text is at
   `/usr/share/common-licenses/GPL-3` on Debian/Ubuntu systems, at
   <https://www.gnu.org/licenses/gpl-3.0.txt>, and in the licenses archive of
   each release. The icon's editable source is part of every source release,
   so it can be modified and the program rebuilt.
3. Several Krusader source files note that parts are based on other KDE code
   (kdelibs `KTar`, KIO, Dolphin, Dawit Alemayehu's KRemoteEncodingPlugin,
   code by Sebastian Trueg and Bruno Nova). Upstream distributes these files
   under GPL-2.0-or-later, which the GPL/LGPL terms of the original code allow.

4. **Bundled rclone.** Since preview 5 the package contains the official
   rclone release binary (version and SHA-256 pinned in
   `dev/rclone/rclone.sha256`, see `dev/rclone/README.md`), because the
   distribution's rclone is too old for safe sign-ins. It is installed
   unmodified as `/usr/lib/x86_64-linux-gnu/libexec/panero/rclone`. Its license
   is in `/usr/share/doc/panero/rclone/COPYING`; the Go modules compiled into
   it (various MIT, BSD, Apache-2.0, MPL-2.0 and similar licenses) are listed
   in `/usr/share/doc/panero/rclone/dependencies.txt` and in the licenses
   archive of each release. Its complete source code is at
   <https://github.com/rclone/rclone> under the pinned tag.

## License texts

`LICENSES/` holds the full texts of every license used in the tree:

- `GPL-2.0-or-later.txt`: GNU General Public License version 2
- `GFDL-1.1-or-later.txt`: GNU Free Documentation License version 1.1
- `LGPL-3.0-or-later.txt`: GNU Lesser General Public License version 3
- `FSFAP.txt`: FSF All Permissive License
- `ISC.txt`: ISC License (Lucide icons)
- `MIT.txt`: MIT License (bundled rclone)

The Debian package installs them, together with this file, `README.md`,
`USER-GUIDE.md`, `AUTHORS` and `CREDITS`, in `/usr/share/doc/panero/`. Debian's packaging
tools compress the larger files there (`.gz`); read them with `zless`.

## What the installed package contains

| Installed file(s) | Built from | License |
|---|---|---|
| `/usr/bin/panero` | `app/` + `app/icons/icon-missing.svgz` | GPL-2.0-or-later; contains one LGPL-3.0-or-later icon |
| `/usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/kio_krarc.so` | `plugins/krarc/` | GPL-2.0-or-later |
| `/usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/kio_iso.so` | `plugins/iso/` | GPL-2.0-or-later (GPL-2.0 only if note 1 applies) |
| `/usr/share/panero/`, `/usr/share/kxmlgui5/panero/`, `/etc/xdg/panero_kio_isorc` | `app/`, `plugins/iso/` | GPL-2.0-or-later |
| `/usr/share/icons/hicolor/*/apps/io.github.norbcraft.panero.*` | `app/icons/panero/` | GPL-2.0-or-later |
| `/usr/share/applications/io.github.norbcraft.panero.desktop` | `app/` | GPL-2.0-or-later |
| `/usr/share/metainfo/io.github.norbcraft.panero.metainfo.xml` | `app/` | FSFAP |
| `/usr/share/locale/*/LC_MESSAGES/panero.mo` | `po/*/panero.po` | GPL-2.0-or-later |
| `/usr/share/doc/HTML/*/panero/`, `/usr/share/man/man1/panero.1.gz` | `doc/`, `po/*/docs/` | GFDL-1.1-or-later |
| `/usr/lib/x86_64-linux-gnu/libexec/panero/panero-cloud-key` | `app/Cloud/panerocloudkey.cpp` | GPL-2.0-or-later |
| `/usr/lib/x86_64-linux-gnu/libexec/panero/rclone` | upstream rclone release (note 4) | MIT (+ licenses of its Go modules) |

Apart from rclone (note 4), nothing else is bundled: no fonts, no third-party
libraries and no Qt or KDE plugins other than Panero's own two KIO workers.

## Runtime dependencies are not bundled

Panero links dynamically against Qt 5, KDE Frameworks 5, glibc, libstdc++,
zlib and libacl from the Ubuntu archive, and uses further Ubuntu packages at
runtime (for example `kio`, `ktexteditor-katepart`, `breeze-icon-theme`
and, for the optional KDiff3 installation, the PackageKit D-Bus service of
`packagekit`). Panero's look uses Qt's built-in Fusion style; no style,
theme or font is bundled. These are installed by the package manager as separate
packages, each with its own `/usr/share/doc/<package>/copyright`. Their
licenses (mostly LGPL-2.1/LGPL-3.0, GPL-compatible) are listed in
[`dev/PROVENANCE.md`](dev/PROVENANCE.md). Their source code is available
from Ubuntu, for example with `apt source <package>` or from
<https://launchpad.net/ubuntu/+source/>.

## Getting the corresponding source

Every Panero release on <https://github.com/NorbCraft/panero/releases> is
published together with:

- `panero-<version>-source.tar.gz`: `git archive` of the exact tagged
  commit the `.deb` was built from, including the `debian/` packaging, the
  build scripts and the CI workflows. It contains `SOURCE-INFO.txt` with the
  tag, the full commit hash, and a dated list of Panero's changes to the
  Krusader 2.8.1 sources (GPL-2.0 section 2(a)).
- `panero-<version>-licenses.tar.gz`: this file, `LICENSES/`, the GPL-3.0
  text referenced by the LGPL-3.0, `debian/copyright`, `AUTHORS`, `CREDITS`
  and `dev/PROVENANCE.md`.
- `SHA256SUMS` for all assets.

The same source is available from the Git tag `v<version>` in the
repository. The `.deb` is built from that tree with
`dpkg-buildpackage -us -uc -b` in a clean `ubuntu:24.04` environment
(`dev/scripts/build-deb.sh`). These downloads stay available on the release
page for as long as the corresponding binary is distributed there.

The unmodified upstream Krusader sources are available from
<https://invent.kde.org/utilities/krusader> (tag `v2.8.1`). The Panero
repository keeps the complete upstream Git history, so every change made by
Panero can be inspected with `git log v2.8.1..`.

## Credits and modifications

Krusader was created by Shie Erlich and Rafi Yanai and is developed by the
Krusader Krew and many contributors and translators (see `AUTHORS`,
`CREDITS`, the original `README` and the About dialog). Panero keeps all
upstream copyright notices, credits, warranty disclaimers and license texts.

Panero's modifications are made by the Panero contributors. Files that
Panero changed keep their upstream notices; new files and substantially
rewritten files name "Panero contributors" in their SPDX headers. The dates
and authors of all changes are recorded in the Git history and summarised in
`SOURCE-INFO.txt` in every source release.

## Names and trademarks

"KDE" and "Krusader" are names of their respective owners and are used here
only to describe Panero's origin. Panero does not use the Krusader or KDE
logos as its branding. The Panero name and icon have not been
trademark-cleared and no exclusive rights in them are claimed; other,
unrelated software called "PANERO" exists. The Panero icon was drawn as an
original SVG from an AI-generated concept; see
[`dev/ICON-PROVENANCE.md`](dev/ICON-PROVENANCE.md).
