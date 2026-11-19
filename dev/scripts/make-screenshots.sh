#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Produce documentation/release screenshots of the actual application, installed
# from the .deb in a clean ubuntu:24.04 container (demo user, demo files, Xvfb).
# No personal paths or data end up in the images.
#
#   dev/scripts/make-screenshots.sh dist/panero_*.deb [outdir]
set -euo pipefail
DEB="$(readlink -f "${1:?path to panero .deb}")"
OUT="$(readlink -m "${2:-docs/screenshots}")"
mkdir -p "$OUT"
WORK="$(mktemp -d)"
cp "$DEB" "$WORK/panero.deb"
cat > "$WORK/inner.sh" <<'INNER'
#!/usr/bin/env bash
set -uo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq /work/panero.deb xvfb xauth xdotool x11-utils imagemagick fonts-inter procps python3 \
    kio-extras kdegraphics-thumbnailers >/dev/null 2>&1 || { echo "setup failed"; exit 2; }
userdel -r ubuntu 2>/dev/null || true
useradd -m -u 1000 -s /bin/bash demo   # uid 1000: may write the mounted /out
cat > /tmp/setup.sh <<'SETUP'
set -e
cd ~
mkdir -p Documents Downloads Pictures Projects/Enclosure Projects/Mounting\ brackets Projects/Reference "Projects/Print profiles" .config
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
cd ~/.config
printf 'First Time=false\nConfig Version=1\n\n[ViewerModule]\nFirstRun=false\n\n[Panero]\nKrusader Import Offered=true\n\n[Panero Pins]\nUrls=file:///home/demo,file:///home/demo/Projects,file:///home/demo/Documents\nLabels=Home,Projects,Documents\n' > panerorc
SETUP
su demo -c 'bash /tmp/setup.sh'

cat > /tmp/shoot.sh <<'SHOOT'
export HOME=/home/demo QT_QPA_PLATFORM=xcb
Xvfb :7 -screen 0 1440x900x24 -nolisten tcp >/dev/null 2>&1 & sleep 1
export DISPLAY=:7
win() { xwininfo -root -tree | python3 -I -c '
import re,sys
b=None
for l in sys.stdin:
    if "panero" not in l.lower(): continue
    m=re.search(r"(\d+)x(\d+)\+-?\d+\+-?\d+\s+\+(-?\d+)\+(-?\d+)",l)
    if m:
        w,h,x,y=map(int,m.groups())
        if b is None or w*h>b[0]: b=(w*h,x,y,w,h)
print("X=%d Y=%d W=%d H=%d"%(b[1:] if b else (0,0,0,0)))'; }
shot() { eval $(win); import -window root -crop ${W}x${H}+${X}+${Y} "/out/$1.png"; }
run() { # $1 theme (1 light, 2 dark)
  sed -i '/^\[Panero Appearance\]/,$d' ~/.config/panerorc
  printf '\n[Panero Appearance]\nTheme=%s\n' "$1" >> ~/.config/panerorc
  panero --left="$HOME/Projects" --right="$HOME/Projects/Reference" >/tmp/panero.log 2>&1 &
  sleep 9; eval $(win)
}
quit() { pkill -x panero; sleep 2; }

# 1/2: details view with marks and cursor, light and dark
for t in 1 2; do
  run $t
  xdotool mousemove $((X+500)) $((Y+255)) click 1; sleep 0.3
  xdotool key Down Down space Down space Down Down; sleep 0.8
  [ $t = 1 ] && shot details-light || shot details-dark
  quit
done
# 3: held right button -> native context menu for the marked files
run 1
xdotool mousemove $((X+500)) $((Y+255)) click 1; sleep 0.3
xdotool key Down Down space Down space Down Down; sleep 0.5
xdotool mousemove $((X+520)) $((Y+400)) mousedown 3; sleep 1.2
import -window root -crop ${W}x${H}+${X}+${Y} /out/context-menu.png
xdotool key Escape; xdotool mouseup 3; quit
# 4: thumbnails view
run 1
xdotool mousemove $((X+500)) $((Y+255)) click 1; sleep 0.3
xdotool key alt+shift+t; sleep 4
xdotool key Right Right space Right space; sleep 1.5
shot thumbnails; quit
# 5: View & Colors dialog
run 1
xdotool mousemove $((X+500)) $((Y+255)) click 1; sleep 0.3
xdotool mousemove $((X+W-62)) $((Y+26)) click 1; sleep 1.5   # toolbar: View & Colors
import -window root /tmp/vc.png; convert /tmp/vc.png -trim /out/view-colors.png
quit
ls -la /out
SHOOT
su demo -c 'bash /tmp/shoot.sh'
INNER
docker run --rm -v "$WORK:/work" -v "$OUT:/out" ubuntu:24.04 bash /work/inner.sh
rm -rf "$WORK"
echo "screenshots in $OUT"
