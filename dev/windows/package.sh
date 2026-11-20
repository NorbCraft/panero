#!/usr/bin/env bash
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

# Data: Panero UI files, layouts, colour schemes, icons, translations
for d in kxmlgui5 panero icons locale; do
    [ -d "$stage/share/$d" ] && cp -r "$stage/share/$d" "$out/data/"
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

# Licenses and source offer (GPL): Panero, Krusader, KDE Frameworks, Qt, MinGW runtime
cp "$root"/LICENSES/*.txt "$out/licenses/"
cp "$here/README-ZIP.txt" "$out/README.txt"

du -sh "$out"
ls "$out" | head -100
(cd "$dist/$name" && zip -qr9 "../$name.zip" Panero)
ls -la "$dist/$name.zip"
