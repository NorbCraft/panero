#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Screenshots with the build host's own desktop look (Zorin icon theme and fonts),
# rendered under Xvfb from the staged build (dev/scripts/run-dev.sh) or an
# installed /usr/bin/panero. A demo home under /tmp is used, so no personal paths
# or files appear in the images.
#
#   dev/scripts/make-screenshots-host.sh [outdir] [--installed]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(readlink -m "${1:-$ROOT/docs/screenshots}")"
BIN="$ROOT/build/stage/usr/bin/panero"
[[ "${2:-}" == "--installed" ]] && BIN=/usr/bin/panero
DEMO=/tmp/panero-shots
rm -rf "$DEMO"; mkdir -p "$DEMO/home" "$OUT"
export HOME="$DEMO/home" XDG_CONFIG_HOME="$DEMO/home/.config" XDG_DATA_HOME="$DEMO/home/.local/share" \
       XDG_CACHE_HOME="$DEMO/home/.cache" XDG_RUNTIME_DIR="$DEMO/runtime" GIO_USE_VFS=local QT_QPA_PLATFORM=xcb
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; unset WAYLAND_DISPLAY
if [[ "$BIN" != /usr/bin/panero ]]; then
  STAGE="$ROOT/build/stage"; LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
  export XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins"
fi

# demo files
cd "$HOME"
mkdir -p Documents Downloads Pictures "Projects/Enclosure" "Projects/Mounting brackets" "Projects/Reference" "Projects/Print profiles" .config
cd Projects
printf 'Assembly guide\n\n1. Fit the brackets\n2. Close the lid\n' > assembly-notes.md
printf 'part,qty\nbracket,4\nlid,1\nscrew M3,12\n' > parts-list.csv
printf '{"layer": 0.2, "infill": 20}\n' > printer-profile.json
convert -size 640x420 gradient:'#2a6df4'-'#1fd1c1' -fill white -pointsize 36 -gravity center -annotate 0 'reference front' reference-front.png
convert rose: -resize 400x300 photo-detail.jpg
convert -size 600x840 xc:white -fill '#1f2328' -pointsize 34 -annotate +50+90 'Assembly guide' -pointsize 22 -annotate +50+150 'Panero demo document' assembly-guide.pdf
head -c 48000 /dev/urandom > bracket-v4.step
head -c 23000 /dev/urandom > enclosure-lid.3mf
tar czf workshop-parts.tar.gz parts-list.csv printer-profile.json
printf '#!/bin/sh\necho build\n' > build.sh && chmod +x build.sh
printf 'Panero demo\n' > README
ln -s assembly-notes.md notes-link
cat > "$HOME/.config/panerorc" <<CFG
First Time=false
Config Version=1

[ViewerModule]
FirstRun=false

[Panero]
Krusader Import Offered=true

[Panero Pins]
Urls=file://$HOME,file://$HOME/Projects,file://$HOME/Documents
Labels=Home,Projects,Documents
CFG
cp "$HOME/.config/panerorc" "$DEMO/panerorc.pristine"
cd "$ROOT"

DISP=":$((70 + RANDOM % 9))"
Xvfb "$DISP" -screen 0 1440x900x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1; export DISPLAY="$DISP"
geom() { xwininfo -root -tree | python3 -I -c '
import re,sys
b=None
for l in sys.stdin:
    if "panero" not in l.lower(): continue
    m=re.search(r"(\d+)x(\d+)\+-?\d+\+-?\d+\s+\+(-?\d+)\+(-?\d+)",l)
    if m:
        w,h,x,y=map(int,m.groups())
        if b is None or w*h>b[0]: b=(w*h,x,y,w,h)
print("X=%d Y=%d W=%d H=%d"%(b[1:] if b else (0,0,0,0)))'; }
# Xvfb background is black: trimming the root window leaves the Panero window (+ open menus)
shot() { import -window root /tmp/panero-shot.png; convert /tmp/panero-shot.png -trim +repage "$OUT/$1.png"; }
start() { # $1: theme 1 light / 2 dark
  cp "$DEMO/panerorc.pristine" "$HOME/.config/panerorc"
  printf '\n[Panero Appearance]\nTheme=%s\n' "$1" >> "$HOME/.config/panerorc"
  N=$((${N:-0}+1)); dbus-run-session -- "$BIN" --left="$HOME/Projects" --right="$HOME/Projects/Reference" >"$DEMO/run-$N.log" 2>&1 & APID=$!
  sleep 8; eval $(geom)
  xdotool mousemove $((X+500)) $((Y+255)) click 1; sleep 0.3
}
stop() { pkill -P "$APID" -x panero 2>/dev/null || true; wait $APID 2>/dev/null || true; sleep 1; }
marks() { xdotool key Down Down space Down space Down Down; sleep 0.8; }

start 1; marks; shot details-light; stop
start 2; marks; shot details-dark; stop
start 1; marks; xdotool mousemove $((X+520)) $((Y+400)) mousedown 3; sleep 1.2; shot context-menu; xdotool key Escape; xdotool mouseup 3; stop
start 1; xdotool key alt+shift+t; sleep 4; xdotool key Right Right space Right space; sleep 1.5; shot thumbnails; stop
start 1; xdotool mousemove $((X+W-62)) $((Y+26)) click 1; sleep 1.5   # toolbar: View & Colors
shot view-colors; stop
kill $XPID 2>/dev/null
ls -la "$OUT"
