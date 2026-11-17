# Build / test environment

## Implementation host (checked 2026-11-17)

| Item | Value |
|---|---|
| OS | Zorin OS 18.1 (`VERSION_CODENAME=noble`, Ubuntu 24.04 base) — this host **is** the target platform |
| Arch / CPU / RAM / disk | x86_64, 16 threads, 31 GiB, ~750 GiB free |
| Session | Wayland, `XDG_CURRENT_DESKTOP=zorin:GNOME` |
| Krusader | distro `2:2.8.1-0ubuntu3` installed; owner has a real `~/.config/krusaderrc` — **never modify it** |
| sudo | requires password (non-interactive agent cannot use it directly) |
| Docker | installed, but user not in `docker` group (socket permission denied) |
| libvirt/qemu | user in `libvirt` group; `qemu-system-x86_64` present |

### Tool inventory

Present: `git`, `make`, `dpkg-buildpackage`, `docker` (no access yet),
`qemu-system-x86_64`, `jq`, `curl`, `python3`, `desktop-file-validate`,
`appstreamcli`, `code`.

Missing (to install): `gh`, `cmake`, `ninja`, `g++`, ECM + Qt5/KF5 `-dev`
packages (`apt-get build-dep krusader` equivalent), `debhelper`, `devscripts`
(`debuild`), `lintian`, `licensecheck`, `reuse`, `inkscape`/`rsvg-convert`
(icon export), `xvfb`/`xvfb-run`, `xdotool`, `imagemagick` (screenshots),
`clang-format`, `kdiff3` (test environment), `packagekit-tools` (`pkcon`,
to exercise the PackageKit install path).

### VS Code extensions

None are required by the agent (it works through the CLI). Optional for a
human working in the repo: `ms-vscode.cmake-tools`, `llvm-vs-code-extensions.vscode-clangd`
(or `ms-vscode.cpptools`), `github.vscode-github-actions`. Not installed
unless the owner asks.

## Setup steps (status in dev/PLAN.md Phase 0)

1. Toolchain install — owner runs once: `sudo bash dev/scripts/bootstrap-host.sh`
   (distro packages only; also adds the user to the `docker` group).
   Then log out/in and restart VS Code so the agent process gets the group.
2. GitHub access — owner runs once in their own terminal:
   `gh auth login --hostname github.com --git-protocol https --web --scopes workflow`
   (browser device-code flow; token stored by gh in the GNOME keyring), then
   `gh auth setup-git` so `git push` over HTTPS uses gh's credentials.
   The `workflow` scope is needed to push `.github/workflows/*`.
   Verify with `gh auth status`. No tokens in chat or in the repo.
3. Git identity — set **per repository only** (done 2026-11-17):
   `git config user.name "Norbert Szabo"`,
   `git config user.email "349977413+NorbCraft@users.noreply.github.com"`.
   On a new machine, repeat these two commands after cloning.

## Continuing on another machine

```bash
git clone https://github.com/NorbCraft/panero.git && cd panero
git remote add upstream https://invent.kde.org/utilities/krusader.git
git fetch upstream --tags
git config user.name "Norbert Szabo"
git config user.email "349977413+NorbCraft@users.noreply.github.com"
sudo bash dev/scripts/bootstrap-host.sh   # Ubuntu 24.04 / Zorin 18 based hosts
```
Then read `AGENTS.md`.

## Reproducing the baseline build

_To be filled in Phase 1._
