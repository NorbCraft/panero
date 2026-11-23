#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Downloads the Win11 icon theme at the commit pinned in dev/icons/win11.sha256,
# checks its SHA-256 and installs the light and dark variants (D-020).
#
# Usage: dev/scripts/fetch-win11-icons.sh <output-dir>
# Result: <output-dir>/icons/Win11, <output-dir>/icons/Win11-dark (installed with
#         CMake -DPANERO_ICON_THEMES_DIR=<output-dir>/icons to share/panero/icons)
#         and <output-dir>/COPYING, <output-dir>/SOURCE (GPL-3.0, where from).
# Downloads are cached in <output-dir>/download/. Fails on any mismatch.
set -euo pipefail
out=${1:?usage: fetch-win11-icons.sh <output-dir>}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/../icons" && pwd)
commit=$(awk '$1 == "commit" {print $2}' "$here/win11.sha256")
sum=$(awk '$1 == "sha256" {print $2}' "$here/win11.sha256")
[ -n "$commit" ] && [ -n "$sum" ] || { echo "no pinned commit/checksum" >&2; exit 1; }
archive="Win11-icon-theme-$commit.tar.gz"
mkdir -p "$out/download"
if [ ! -f "$out/download/$archive" ] || ! echo "$sum  $out/download/$archive" | sha256sum -c --quiet - 2>/dev/null; then
    rm -f "$out/download/$archive" "$out/download/$archive.part"
    curl -fsSL --retry 3 --proto '=https' --tlsv1.2 -o "$out/download/$archive.part" \
        "https://github.com/yeyushengfan258/Win11-icon-theme/archive/$commit.tar.gz"
    mv "$out/download/$archive.part" "$out/download/$archive"
fi
echo "$sum  $out/download/$archive" | sha256sum -c --quiet -
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xzf "$out/download/$archive" -C "$tmp"
src="$tmp/Win11-icon-theme-$commit"
# the theme's installer calls gtk-update-icon-cache; the cache only speeds up
# lookups, so a missing tool is replaced by a no-op
mkdir -p "$tmp/bin"
if ! command -v gtk-update-icon-cache >/dev/null; then
    printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/gtk-update-icon-cache"
    chmod +x "$tmp/bin/gtk-update-icon-cache"
fi
rm -rf "$out/icons"
mkdir -p "$out/icons"
dest=$(cd "$out/icons" && pwd)
(cd "$src" && PATH="$tmp/bin:$PATH" DESKTOP_SESSION= bash ./install.sh -d "$dest" -t default >/dev/null)
# installer leftovers that point into this build folder: make them relative
while IFS= read -r link; do
    target=$(readlink "$link")
    ln -sfn "$(realpath --relative-to="$(dirname "$link")" "$target")" "$link"
done < <(find "$dest" -type l -lname '/*')
[ -z "$(find "$dest" -type l -lname '/*')" ] || { echo "absolute symlinks left" >&2; exit 1; }
# Panero change: the theme draws places and devices (folders, Home, drives)
# monochrome at 16/22/24 px, made for GTK side panels; a file manager shows
# Explorer's coloured icons at those sizes too, so the small ones use the
# coloured scalable artwork (also inode-directory, Krusader's folder icon)
for t in Win11 Win11-dark; do
    for ctx in places devices; do
        [ -d "$dest/$t/$ctx/scalable" ] && [ ! -L "$dest/$t/$ctx" ] || continue
        for size in 16 22 24; do
            [ -d "$dest/$t/$ctx/$size" ] || continue
            for f in "$dest/$t/$ctx/$size"/*.svg; do
                name=$(basename "$f")
                [ -e "$dest/$t/$ctx/scalable/$name" ] && ln -sfn "../scalable/$name" "$f"
            done
        done
    done
    if [ -d "$dest/$t/mimes" ] && [ ! -L "$dest/$t/mimes" ] && [ -e "$dest/$t/places/scalable/folder.svg" ]; then
        for size in 16 22 24; do
            [ -e "$dest/$t/mimes/$size/inode-directory.svg" ] && ln -sfn "../../places/scalable/folder.svg" "$dest/$t/mimes/$size/inode-directory.svg"
        done
    fi
    # the icon caches describe the files before this change
    rm -f "$dest/$t/icon-theme.cache"
done
# upstream ships some links to icons that do not exist: drop them
find "$dest" -xtype l -delete
for t in Win11 Win11-dark; do
    [ -f "$dest/$t/index.theme" ] || { echo "missing $t" >&2; exit 1; }
done
install -m 0644 "$src/COPYING" "$out/COPYING"
printf 'Win11 icon theme by yeyushengfan258, GPL-3.0\nhttps://github.com/yeyushengfan258/Win11-icon-theme\ncommit %s\nChanged by Panero: places/devices at 16/22/24 px and inode-directory use the\ncoloured scalable artwork (dev/scripts/fetch-win11-icons.sh).\n' "$commit" > "$out/SOURCE"
echo "Win11 icon theme $commit verified -> $dest"
