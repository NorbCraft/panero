#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Downloads the rclone release pinned in dev/rclone/rclone.sha256, checks its
# SHA-256 and unpacks the binary (dev/CLOUD-SECURITY-AUDIT.md R1).
#
# Usage: dev/scripts/fetch-rclone.sh <output-dir> [amd64|arm64]
# Result: <output-dir>/rclone (executable), <output-dir>/rclone-dependencies.txt
#         (Go modules compiled into it, from `rclone version --deps`) and
#         <output-dir>/COPYING (rclone's MIT license).
# Downloads are cached in <output-dir>/download/. Fails on any mismatch.
set -euo pipefail
out=${1:?usage: fetch-rclone.sh <output-dir> [amd64|arm64]}
arch=${2:-$(dpkg --print-architecture 2>/dev/null || echo amd64)}
case "$arch" in amd64|arm64) ;; *) echo "unsupported architecture: $arch" >&2; exit 2 ;; esac
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/../rclone" && pwd)
version=$(awk '$1 == "version" {print $2}' "$here/rclone.sha256")
zip="rclone-v${version}-linux-${arch}.zip"
sum=$(awk -v f="$zip" '$2 == f {print $1}' "$here/rclone.sha256")
[ -n "$version" ] && [ -n "$sum" ] || { echo "no pinned checksum for $zip" >&2; exit 1; }
mkdir -p "$out/download"
if [ ! -f "$out/download/$zip" ] || ! echo "$sum  $out/download/$zip" | sha256sum -c --quiet - 2>/dev/null; then
    rm -f "$out/download/$zip" "$out/download/$zip.part"
    curl -fsSL --retry 3 --proto '=https' --tlsv1.2 -o "$out/download/$zip.part" "https://downloads.rclone.org/v${version}/${zip}"
    mv "$out/download/$zip.part" "$out/download/$zip"
fi
echo "$sum  $out/download/$zip" | sha256sum -c --quiet -
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
unzip -q -j "$out/download/$zip" "rclone-v${version}-linux-${arch}/rclone" -d "$tmp"
install -m 0755 "$tmp/rclone" "$out/rclone"
install -m 0644 "$here/COPYING" "$out/COPYING"
got=$("$out/rclone" version | head -n 1)
[ "$got" = "rclone v${version}" ] || { echo "unexpected version: $got" >&2; exit 1; }
"$out/rclone" version --deps > "$out/rclone-dependencies.txt"
echo "rclone ${version} (${arch}) verified -> $out/rclone"
