#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Look mockups from the real application: runs the staged build under Xvfb with a
# demo home (no personal data) and an optional development style sheet
# (PANERO_DEV_STYLESHEET), then saves a screenshot per variant.
#
#   dev/scripts/make-mockups.sh OUTDIR NAME:THEME[:QSS[:CFG[:STEP]]] ...
#   THEME 1 light / 2 dark; QSS = style sheet, CFG = lines appended to panerorc,
#   both relative to dev/mockups (empty = none); STEP = "click X Y" (window-relative)
#   done before the screenshot
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(readlink -m "${1:?outdir}")"; shift
STAGE="$ROOT/build/stage"; BIN="$STAGE/usr/bin/panero"
DEMO="$(mktemp -d /tmp/panero-mockups.XXXXXX)"
mkdir -p "$DEMO/home" "$OUT"
export HOME="$DEMO/home" XDG_CONFIG_HOME="$DEMO/home/.config" XDG_DATA_HOME="$DEMO/home/.local/share" \
       XDG_CACHE_HOME="$DEMO/home/.cache" XDG_RUNTIME_DIR="$DEMO/runtime" GIO_USE_VFS=local QT_QPA_PLATFORM=xcb
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; unset WAYLAND_DISPLAY
LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
export XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins"

cd "$HOME"
mkdir -p Documents Downloads Music Pictures Videos "Projects/Enclosure" "Projects/Mounting brackets" "Projects/Reference" "Projects/Print profiles" .config
cd Projects
printf 'Assembly guide\n\n1. Fit the brackets\n2. Close the lid\n' > assembly-notes.md
printf 'part,qty\nbracket,4\nlid,1\nscrew M3,12\n' > parts-list.csv
printf '{"layer": 0.2, "infill": 20}\n' > printer-profile.json
convert -size 640x420 gradient:'#2a6df4'-'#1fd1c1' -fill white -pointsize 36 -gravity center -annotate 0 'reference front' reference-front.png
convert rose: -resize 400x300 photo-detail.jpg
convert -size 600x840 xc:white -fill '#1f2328' -pointsize 34 -annotate +50+90 'Assembly guide' assembly-guide.pdf
head -c 48000 /dev/urandom > bracket-v4.step
head -c 23000 /dev/urandom > enclosure-lid.3mf
tar czf workshop-parts.tar.gz parts-list.csv printer-profile.json
printf '#!/bin/sh\necho build\n' > build.sh && chmod +x build.sh
printf 'Panero demo\n' > README
cd Reference
convert -size 300x200 xc:'#e9eef6' front.png; convert -size 300x200 xc:'#f6efe9' side.png
printf 'notes\n' > measurements.txt; head -c 9000 /dev/urandom > housing.stl
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

DISP=":$((60 + RANDOM % 9))"
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

for spec in "$@"; do
  IFS=: read -r name theme qss cfg step <<<"$spec"
  cp "$DEMO/panerorc.pristine" "$HOME/.config/panerorc"
  printf '\n[Panero Appearance]\nTheme=%s\n' "$theme" >> "$HOME/.config/panerorc"
  [[ -n "${cfg:-}" ]] && { echo; cat "$ROOT/dev/mockups/$cfg"; } >> "$HOME/.config/panerorc"
  rm -rf "$XDG_DATA_HOME/kxmlgui5"
  envargs=(PANERO_MOCKUP=1)
  [[ -n "${qss:-}" ]] && envargs+=("PANERO_DEV_STYLESHEET=$ROOT/dev/mockups/$qss")
  env "${envargs[@]}" dbus-run-session -- "$BIN" --left="$HOME/Projects" --right="$HOME/Projects/Reference" \
      >"$DEMO/$name.log" 2>&1 & APID=$!
  sleep 8; eval "$(geom)"
  xdotool mousemove $((X+700)) $((Y+300)) click 1; sleep 0.3
  xdotool key Home Down Down Down Down Down space Down space Down Down; sleep 1
  if [[ "${step:-}" == click* ]]; then
    read -r _ cx cy <<<"$step"; xdotool mousemove $((X+cx)) $((Y+cy)) click 1; sleep 1
    xdotool mousemove $((X+700)) $((Y+400)); sleep 0.5
  fi
  import -window root -crop "${W}x${H}+${X}+${Y}" +repage "$OUT/$name.png"
  pkill -P "$APID" -x panero 2>/dev/null || true; wait "$APID" 2>/dev/null || true; sleep 1
  echo "shot $name"
done
kill $XPID 2>/dev/null || true
rm -rf "$DEMO"
