#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Window test (Xvfb, temporary HOME): inside an archive no "krarc:/ does not
# exist" error pops up (the folder tree followed the panel to "krarc:/"), and
# a dialog without a parent (New Folder, F7) opens centred on the Panero window;
# the System drive button's right-click menu has Properties and Disks (Linux).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PANERO_DEV_HOME="$ROOT/build/dev-cloud"
"$ROOT/dev/scripts/dev-seed-config.sh" >/dev/null
DEVHOME="$PANERO_DEV_HOME"
SHOTS="$ROOT/build/dialog-shots"
rm -rf "$SHOTS" "$DEVHOME/Archives"; mkdir -p "$SHOTS" "$DEVHOME/Archives/src/inner"
echo hello > "$DEVHOME/Archives/src/inner/a.txt"
(cd "$DEVHOME/Archives/src" && zip -qr ../test.zip inner)
grep -q 'Show Drive Buttons' "$DEVHOME/config/panerorc" 2>/dev/null || printf '\n[Panero Appearance]\nShow Drive Buttons=true\n' >> "$DEVHOME/config/panerorc"
PASS=0; FAIL=0
check() { local name="$1"; shift
  if "$@"; then echo "PASS  $name"; PASS=$((PASS+1)); else echo "FAIL  $name"; FAIL=$((FAIL+1)); fi; }
shot() { import -window root "$SHOTS/$1.png" 2>/dev/null; }

STAGE="$ROOT/build/stage"
LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
export HOME="$DEVHOME" XDG_CONFIG_HOME="$DEVHOME/config" XDG_DATA_HOME="$DEVHOME/data" XDG_CACHE_HOME="$DEVHOME/cache" \
       XDG_RUNTIME_DIR="$DEVHOME/runtime" XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" \
       PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins" QT_QPA_PLATFORM=xcb GIO_USE_VFS=local
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; unset WAYLAND_DISPLAY
DISP=":$((80 + RANDOM % 9))"
Xvfb "$DISP" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1; export DISPLAY="$DISP"
# a window manager places windows as on a desktop (without one X11 puts every window at 0,0)
command -v xfwm4 >/dev/null && { xfwm4 --compositor=off >/dev/null 2>&1 & }
dbus-run-session -- "$STAGE/usr/bin/panero" --left="krarc:$DEVHOME/Archives/test.zip/inner/" --right="$DEVHOME" >"$DEVHOME/dialog-run.log" 2>&1 & APID=$!
sleep 8
# move the main window off-centre so "centred on the window" differs from "centred on the screen"
MAIN=""; best=0
for w in $(xdotool search --onlyvisible --class panero 2>/dev/null); do
  eval $(xdotool getwindowgeometry --shell "$w" | sed 's/^/W_/')
  if [ $((W_WIDTH*W_HEIGHT)) -gt $best ]; then best=$((W_WIDTH*W_HEIGHT)); MAIN=$w; fi
done
xdotool windowsize "$MAIN" 1000 700 windowmove "$MAIN" 40 60; sleep 1
eval $(xdotool getwindowgeometry --shell "$MAIN" | sed 's/^/M_/')
shot 01-start

# the left panel starts inside the archive (krarc:.../test.zip/inner/); make it
# the active panel so the folder tree follows it
xdotool windowactivate --sync "$MAIN" 2>/dev/null
xdotool mousemove $((M_X+450)) $((M_Y+400)) click 1; sleep 4; shot 02-in-archive
echo "title: $(xdotool getwindowname "$MAIN")"
check "the active panel is inside the archive" grep -q 'inner' <(xdotool getwindowname "$MAIN")
check "no error pop-up inside the archive" test -z "$(xdotool search --onlyvisible --name '^Error$' 2>/dev/null)"
grep -q 'krarc' "$DEVHOME/dialog-run.log" && grep -i 'does not exist' "$DEVHOME/dialog-run.log" | head -3

# New Folder (F7): centred on the main window
xdotool key Escape; sleep 0.5; xdotool key Tab; sleep 1; xdotool key F7; sleep 2; shot 03-new-folder
DLG=$(xdotool search --onlyvisible --name '^New Folder$' | head -1)
if [ -n "$DLG" ]; then
  eval $(xdotool getwindowgeometry --shell "$DLG" | sed 's/^/D_/')
  dx=$(( (D_X + D_WIDTH/2) - (M_X + M_WIDTH/2) )); dy=$(( (D_Y + D_HEIGHT/2) - (M_Y + M_HEIGHT/2) ))
  echo "main ${M_X},${M_Y} ${M_WIDTH}x${M_HEIGHT}; dialog ${D_X},${D_Y} ${D_WIDTH}x${D_HEIGHT}; offset ${dx},${dy}"
  check "New Folder centred on the Panero window" test ${dx#-} -le 30 -a ${dy#-} -le 40
  xdotool key Escape
else
  check "New Folder dialog opened" false
fi
# right-click on the left panel's System drive button (the first button of the drive bar)
SYS="$((M_X+305)) $((M_Y+70))"   # "System", first button of the left drive bar
if [ -n "$SYS" ]; then
  xdotool mousemove $SYS click 3; sleep 1.5; shot 04-drive-menu
  MENU=$(xdotool search --onlyvisible --class panero 2>/dev/null | while read w; do n=$(xdotool getwindowname $w); [ -z "$n" ] && echo $w; done | tail -1)
  # pick "Properties": first item after Open in Other Panel; type its accelerator-free way: arrow keys
  xdotool key Down Down Return; sleep 2; shot 05-properties
  PROP=$(xdotool search --onlyvisible --name 'Properties' 2>/dev/null | head -1)
  check "drive menu: Properties opens the drive's properties" test -n "$PROP"
  [ -n "$PROP" ] && xdotool key Escape
else
  check "found the System drive button" false
fi
kill $APID 2>/dev/null; sleep 2; kill -9 $APID 2>/dev/null; kill $XPID 2>/dev/null
echo "passed $PASS, failed $FAIL (screenshots: $SHOTS)"
[[ $FAIL == 0 ]]
