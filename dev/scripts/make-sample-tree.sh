#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Create a disposable sample folder for manual/automated GUI checks.
set -euo pipefail
D="${1:?target dir}"; rm -rf "$D"; mkdir -p "$D"
cd "$D"
mkdir -p "Projects" "Photos 2026" "Документы" ".hidden-dir" "Archive"
for f in "report final.pdf" "notes.txt" "budget.xlsx" "photo 01.jpg" "script.sh" "README" \
         "archive.tar.gz" "data.json" "drawing.dxf" ".bashrc-sample" "Übersicht.md" "music.flac"; do
  head -c $((RANDOM % 5000 + 100)) /dev/urandom > "$f"
done
chmod +x script.sh
printf 'hello\nworld\n' > notes.txt
ln -s notes.txt "link to notes"
ln -s missing-target "broken link"
(cd Archive && printf 'a\n' > "a file.txt" && zip -q ../bundle.zip "a file.txt")
echo "sample tree in $D"
