#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the portable Windows ZIP from an MSYS2 UCRT64 build tree.
#   dev/windows/package.sh <build-dir> <install-prefix> <dist-dir>
# Layout inside the ZIP (folder "Panero"):
#   panero.exe, kioslave5.exe, *.dll   executable, KIO helpers, all DLLs
#   qt.conf                            Plugins=plugins
#   plugins/                           Qt + KIO plugins (file, trash, ftp, krarc, ...)
#   data/                              UI files, Panero data, icons (Qt finds <exe dir>/data)
#   licenses/                          license texts and the source offer
set -euo pipefail

build=$1 stage=$2 dist=$3
P=${MINGW_PREFIX:-/ucrt64}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

cmake --install "$build" --prefix "$stage"

ver=$(sed -n 's/^set(VERSION "\(.*\)")/\1/p' "$root/CMakeLists.txt")
name="panero-$ver-windows-x64"
out="$dist/$name/Panero"
rm -rf "$dist"
mkdir -p "$out/plugins/kf5" "$out/data" "$out/licenses"

cp "$stage/bin/panero.exe" "$out/"
# KIO 5 runs its workers in kioslave5.exe; kiod5 hosts the password/SSL modules.
cp "$P/bin/kioslave5.exe" "$P/bin/kiod5.exe" "$out/"

# Qt plugins (platform, Windows styles, image formats, SVG icons)
for d in platforms styles imageformats iconengines; do
    cp -r "$P/share/qt5/plugins/$d" "$out/plugins/"
done
# KDE plugins: KIO workers (file, trash = Recycle Bin, ftp, http, ...) and kiod modules
cp -r "$P/share/qt5/plugins/kf5/kio" "$P/share/qt5/plugins/kf5/kiod" "$out/plugins/kf5/"
# Panero's own KIO worker (krarc: browse archives)
find "$stage" -iname 'kio_krarc.dll' -o -iname 'krarc.dll' | while read -r f; do
    cp "$f" "$out/plugins/kf5/kio/"
done
find "$out/plugins" -name '*.dll.a' -delete

# Data: Panero UI files, layouts, colour schemes, icons, translations.
# ECM installs data to bin/data on Windows (Qt looks in <exe dir>/data).
for d in "$stage/bin/data" "$stage/share"; do
    [ -d "$d" ] && cp -r "$d/." "$out/data/"
done
for f in kxmlgui5/panero/paneroui.rc panero/layout.xml; do
    [ -f "$out/data/$f" ] || { echo "missing data file: $f" >&2; exit 1; }
done
# Breeze icon theme as one resource file (KIconThemes loads data/icontheme.rcc)
cp "$P/bin/data/icontheme.rcc" "$out/data/"

cat > "$out/qt.conf" <<'EOF'
[Paths]
Prefix=.
Plugins=plugins
EOF

# Copy every DLL the executables and plugins import, until nothing new appears.
changed=1
while [ "$changed" = 1 ]; do
    changed=0
    while read -r f; do
        for d in $(objdump -p "$f" | sed -n 's/^\s*DLL Name: //p'); do
            [ -e "$out/$d" ] && continue
            if [ -e "$P/bin/$d" ]; then
                cp "$P/bin/$d" "$out/"
                changed=1
            fi
        done
    done < <(find "$out" -iname '*.exe' -o -iname '*.dll')
done

# 7-Zip for Pack / Unpack and archive browsing (zip, 7z, rar, gzip, bzip2, xz;
# Windows' own tar.exe does the tar family): the official x64 release at a
# pinned version, checked by SHA-256. 7z.exe and 7z.dll go to "7-Zip", where
# Panero looks first (KrArcBaseManager::findExecutable). Licence: GNU LGPL with
# the unRAR restriction (License.txt).
sz_ver=26.06 sz_tag=2606
sz_sha=9b697f65ecc2d9b0152f5ee96d72dde522a7827690349d9307935dacf40f42ec
sz_dl="$build/7z$sz_tag-x64.exe"
if [ ! -f "$sz_dl" ] || ! echo "$sz_sha  $sz_dl" | sha256sum -c --status; then
    curl -fsSL --retry 3 -o "$sz_dl" "https://github.com/ip7z/7zip/releases/download/$sz_ver/7z$sz_tag-x64.exe" \
        || curl -fsSL --retry 3 -o "$sz_dl" "https://www.7-zip.org/a/7z$sz_tag-x64.exe"
fi
echo "$sz_sha  $sz_dl" | sha256sum -c
mkdir -p "$out/7-Zip"
# the installer is a 7z self-extracting archive; libarchive's bsdtar reads it
sz_tar=$(command -v bsdtar || echo /c/Windows/System32/tar.exe)
"$sz_tar" -xf "$sz_dl" -C "$out/7-Zip" 7z.exe 7z.dll License.txt readme.txt History.txt
cp "$out/7-Zip/License.txt" "$out/licenses/7-Zip-License.txt"
cp "$here/7-Zip/LGPL-2.1.txt" "$out/licenses/7-Zip-LGPL-2.1.txt"
"$out/7-Zip/7z.exe" i > "$build/7z-info.txt" && head -3 "$build/7z-info.txt"

# Licenses and source offer (GPL): Panero, Krusader, KDE Frameworks, Qt, MinGW runtime
cp "$root"/LICENSES/*.txt "$out/licenses/"
cp "$here/README-ZIP.txt" "$out/README.txt"

du -sh "$out"
ls "$out" | head -100
(cd "$dist/$name" && zip -qr9 "../$name.zip" Panero)
ls -la "$dist/$name.zip"
