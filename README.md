# Panero

**Panero is an independent fork of Krusader. It is not affiliated with or endorsed by KDE or the Krusader maintainers.**

Panero is a native twin-panel (commander-style) file manager for Linux, built
with C++, Qt 5 and KDE Frameworks 5. It keeps Krusader's mature file-management
engine and adds a modernised appearance and a workflow familiar to Total
Commander users. The primary target is **Zorin OS 18.1 (Ubuntu 24.04 base)**
on x86_64; no KDE Plasma desktop is required.

- Source code and releases: <https://github.com/NorbCraft/panero>
- Bug reports for Panero: <https://github.com/NorbCraft/panero/issues>
  (please do not report Panero problems to the Krusader project)
- Based on Krusader 2.8.1 (<https://krusader.org/>,
  <https://invent.kde.org/utilities/krusader>)

## Installing

Download `panero_<version>_amd64.deb` from the
[Releases page](https://github.com/NorbCraft/panero/releases) and open it with
the Software installer, or install it from a terminal (this resolves
dependencies from the Ubuntu/Zorin archives):

```bash
sudo apt install ./panero_<version>_amd64.deb
```

Panero installs next to an existing Krusader without touching its files or its
settings. Its configuration lives in `~/.config/panerorc` and
`~/.local/share/panero/`. On the first start Panero offers to copy an existing
Krusader configuration; Krusader's own files are never changed.

## Building from source

```bash
sudo apt install build-essential cmake ninja-build extra-cmake-modules gettext \
  qtbase5-dev libkf5kio-dev libkf5parts-dev libkf5xmlgui-dev libkf5archive-dev \
  libkf5bookmarks-dev libkf5doctools-dev libkf5iconthemes-dev libkf5notifications-dev \
  libkf5textwidgets-dev libkf5wallet-dev libkf5windowsystem-dev libkf5guiaddons-dev \
  libacl1-dev libattr1-dev zlib1g-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
ninja -C build && (cd build && ctest --output-on-failure)
```

See `dev/ENVIRONMENT.md` for the full development setup and
`dev/scripts/run-dev.sh` to run a build in an isolated test profile.

## License and credits

Panero is free software, licensed like Krusader under the **GNU General Public
License version 2 or (at your option) any later version** (GPL-2.0-or-later).
Some files carry other compatible licenses; see `LICENSES/`, the SPDX headers in
each file, and `LICENSING.md` for the complete licensing and attribution
overview, including third-party components and assets.

Krusader was created by Shie Erlich and Rafi Yanai and developed by the
Krusader Krew and many contributors (see `AUTHORS`, `CREDITS` and the
original `README`). Panero preserves their copyright notices and credits.
The Panero name and icon are not trademark-cleared; other, unrelated software
named "PANERO" exists.

## For contributors and agents

Development documentation, the plan, the work log and decisions are in `dev/`
(start with `AGENTS.md`). Upstream's original README is kept as `README`.
