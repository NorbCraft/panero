#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Install the pinned KDE Frameworks 5 packages into an MSYS2 UCRT64 environment.
# Every file is checked against dev/windows/kf5-packages.sha256 before pacman
# sees it. Run from the repository root inside an MSYS2 UCRT64 shell.
set -euo pipefail

list="$(cd "$(dirname "$0")" && pwd)/kf5-packages.sha256"
mirror="${PANERO_MSYS2_MIRROR:-https://repo.msys2.org/mingw/ucrt64}"
cache="${PANERO_KF5_CACHE:-$PWD/.kf5-packages}"
mkdir -p "$cache"

grep -v '^#' "$list" | while read -r sum file; do
    [ -n "$file" ] || continue
    if [ ! -f "$cache/$file" ]; then
        curl -fsSL --retry 3 -o "$cache/$file.part" "$mirror/${file//+/%2B}"
        mv "$cache/$file.part" "$cache/$file"
    fi
done

(cd "$cache" && grep -v '^#' "$list" | sha256sum -c --quiet -)

# Dependencies that are still in the live MSYS2 repository are resolved by pacman.
pacman -U --noconfirm --needed "$cache"/*.pkg.tar.zst
echo "Installed $(grep -vc '^#' "$list") pinned KF5 packages."
