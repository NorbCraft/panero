#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Regenerate the committed PNG exports of the Panero icon from the SVG sources.
set -euo pipefail
cd "$(dirname "$0")/../../app/icons/panero"
ID=io.github.norbcraft.panero
for s in 16 22 24; do rsvg-convert -w $s -h $s $ID-small.svg -o $s-apps-$ID.png; done
for s in 32 48 64 128 256; do rsvg-convert -w $s -h $s $ID.svg -o $s-apps-$ID.png; done
cp $ID.svg sc-apps-$ID.svg
echo "rendered $(ls *-apps-$ID.png | wc -l) PNGs"
