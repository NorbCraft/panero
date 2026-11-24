#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Window test (Xvfb, temporary HOME/XDG, D-022): the first-start assistant on
# an empty profile; the Settings window by Ctrl+, and by the gear's "All
# Settings..." (an option changed and its config key checked); the Quick
# Settings popup; Krusader bookmarks imported into Pinned; Ctrl+D shows the
# Pinned list; the Tools menu. Screenshots go to build/settings-shots.
#
#   dev/scripts/gui-settings-test.sh   (after a staged build: DESTDIR=build/stage ninja -C build install)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEVHOME="$ROOT/build/dev-settings"
SHOTS="$ROOT/build/settings-shots"
STAGE="$ROOT/build/stage"
RC="$DEVHOME/config/panerorc"
PASS=0; FAIL=0
check() { local name="$1"; shift
  if "$@"; then echo "PASS  $name"; PASS=$((PASS+1)); else echo "FAIL  $name"; FAIL=$((FAIL+1)); fi; }
shot() { import -window root "$SHOTS/$1.png" 2>/dev/null; }
key() { xdotool key --clearmodifiers "$@"; sleep 0.6; }
win() { xdotool search --onlyvisible --name "$1" 2>/dev/null | head -1; }
waitwin() { local i; for i in $(seq 1 "${2:-20}"); do [ -n "$(win "$1")" ] && return 0; sleep 0.5; done; return 1; }
rcvalue() { # group key
  awk -v g="[$1]" -v k="$2" '$0==g{f=1;next} /^\[/{f=0} f && index($0,k"=")==1{print substr($0,length(k)+2)}' "$RC"; }
mainwin() { local w best=0 main=""
  for w in $(xdotool search --onlyvisible --class panero 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" | sed 's/^/W_/')"
    if [ $((W_WIDTH*W_HEIGHT)) -gt $best ]; then best=$((W_WIDTH*W_HEIGHT)); main=$w; fi
  done; echo "$main"; }

LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
rm -rf "$DEVHOME" "$SHOTS"; mkdir -p "$DEVHOME"/{config,data,cache,runtime} "$SHOTS"; chmod 700 "$DEVHOME/runtime"
export HOME="$DEVHOME" XDG_CONFIG_HOME="$DEVHOME/config" XDG_DATA_HOME="$DEVHOME/data" XDG_CACHE_HOME="$DEVHOME/cache" \
       XDG_RUNTIME_DIR="$DEVHOME/runtime" XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" \
       PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins" QT_QPA_PLATFORM=xcb GIO_USE_VFS=local
unset WAYLAND_DISPLAY PANERO_NO_FIRST_START_ASSISTANT
# a free display chosen by Xvfb itself (other tests may run at the same time)
Xvfb -displayfd 3 -screen 0 1600x1000x24 -nolisten tcp 3>"$DEVHOME/display" >/dev/null 2>&1 & XPID=$!
for i in $(seq 1 20); do [ -s "$DEVHOME/display" ] && break; sleep 0.3; done
export DISPLAY=":$(cat "$DEVHOME/display")"; echo "display $DISPLAY"
APID=""
start() { dbus-run-session -- "$STAGE/usr/bin/panero" "$@" >>"$DEVHOME/run.log" 2>&1 & APID=$!; }
# (only the Panero of this staged build: its full path is unique to this checkout)
stop() { pkill -f "^$STAGE/usr/bin/panero" 2>/dev/null; sleep 2; pkill -9 -f "^$STAGE/usr/bin/panero" 2>/dev/null
         [ -n "$APID" ] && kill "$APID" 2>/dev/null; APID=""; sleep 1; }
trap 'stop; kill $XPID 2>/dev/null' EXIT

# ---- 1. first start: the assistant on an empty profile ---------------------------------
start --left="$DEVHOME" --right="$DEVHOME"
waitwin '^Welcome to Panero$' 40
check "first start (empty profile) opens the first-start assistant" test -n "$(win '^Welcome to Panero$')"
shot 01-assistant-look
key Return                                   # look: defaults -> Next
shot 02-assistant-drives
key space                                    # drive buttons on (first control of the page)
shot 03-assistant-drives-on
key Return; shot 04-assistant-mouse
key Return; shot 05-assistant-done
key Return                                   # Start Panero
sleep 8; shot 06-first-main-window
check "the main window opens after the assistant" test -n "$(mainwin)"
check "the assistant is remembered (First Start Assistant Shown)" test "$(rcvalue Panero 'First Start Assistant Shown')" = true
check "the assistant wrote the mouse selection (Panero mode 5)" test "$(rcvalue 'Look&Feel' 'Mouse Selection')" = 5
echo "      Show Drive Buttons after the assistant: $(rcvalue 'Panero Appearance' 'Show Drive Buttons')"
check "the assistant's drive buttons choice is stored" test "$(rcvalue 'Panero Appearance' 'Show Drive Buttons')" = true
stop

# ---- 2. returning user: no assistant; bookmarks imported into Pinned -------------------
mkdir -p "$DEVHOME/data/panero" "$DEVHOME/Work/Reports" "$DEVHOME/Music"
cat > "$DEVHOME/data/panero/krbookmarks.xml" <<XML
<!DOCTYPE xbel>
<xbel>
 <bookmark href="$DEVHOME/Work" icon="folder"><title>Work</title></bookmark>
 <folder icon="folder"><title>More</title>
  <bookmark href="file://$DEVHOME/Music" icon="folder"><title>My Music</title></bookmark>
  <bookmark href="$DEVHOME" icon="folder"><title>Home again</title></bookmark>
 </folder>
</xbel>
XML
cp "$DEVHOME/data/panero/krbookmarks.xml" "$DEVHOME/bookmarks.orig"
# a returning user who had Krusader bookmarks (pins never saved: the default list)
sed -i '/^\[Panero Pins\]/,/^$/d' "$RC"
printf '\n[Startup]\nShow menubar=true\n' >> "$RC"
start --left="$DEVHOME" --right="$DEVHOME/Work"
sleep 9
check "a returning user gets no assistant" test -z "$(win '^Welcome to Panero$')"
URLS=$(rcvalue 'Panero Pins' Urls)
echo "      pins: $URLS"
check "bookmark Work imported into Pinned" grep -q "$DEVHOME/Work" <<< "$URLS"
check "bookmark in a folder (My Music) imported into Pinned" grep -q "My Music" <<< "$(rcvalue 'Panero Pins' Labels)"
check "Home is not pinned twice" test "$(tr ',' '\n' <<< "$URLS" | grep -c "^file://$DEVHOME\$")" -le 1
check "import flag set (Bookmarks Imported=true)" test "$(rcvalue 'Panero Pins' 'Bookmarks Imported')" = true
check "bookmarks file kept unchanged" cmp -s "$DEVHOME/data/panero/krbookmarks.xml" "$DEVHOME/bookmarks.orig"
check "bookmarks backup written" test -f "$DEVHOME/data/panero/krbookmarks.xml.before-pinned"
MAIN=$(mainwin)
xdotool windowactivate --sync "$MAIN" 2>/dev/null; xdotool windowsize "$MAIN" 1500 900; sleep 1
eval "$(xdotool getwindowgeometry --shell "$MAIN" | sed 's/^/M_/')"
xdotool mousemove $((M_X+1100)) $((M_Y+500)) click 1; sleep 1
key ctrl+d; shot 10-ctrl-d-pinned-menu
key Escape

# ---- 3. Tools menu (menu bar shown) ------------------------------------------------------
key alt+t; shot 11-tools-menu
key Escape; key Escape

# ---- 4. Ctrl+, opens Settings; Drives & Cloud: drive buttons off, Apply ----------------
key ctrl+comma; sleep 2
waitwin '^Panero Settings' 10
SET=$(win '^Panero Settings')
check "Ctrl+, opens the Settings window (Panero Settings)" test -n "$SET"
shot 20-settings-appearance
if [ -n "$SET" ]; then
  xdotool windowactivate --sync "$SET" 2>/dev/null
  eval "$(xdotool getwindowgeometry --shell "$SET" | sed 's/^/S_/')"
  # the page tree: Appearance, Panels, Mouse & Keys, Drives & Cloud, Advanced (~20 px rows from the top)
  xdotool mousemove $((S_X+60)) $((S_Y+28+3*20)) click 1; sleep 1
  shot 21-settings-drives
  echo "      settings window at ${S_X},${S_Y} ${S_WIDTH}x${S_HEIGHT}"
  xdotool mousemove $((S_X+340)) $((S_Y+102)) click 1; sleep 0.8   # "Show drive buttons above each panel"
  shot 22-settings-drives-changed
  xdotool mousemove $((S_X+S_WIDTH-150)) $((S_Y+S_HEIGHT-37)) click 1; sleep 2; shot 23-settings-applied   # Apply
  check "Settings > Drives & Cloud: drive buttons switched off and applied" \
    test "$(rcvalue 'Panero Appearance' 'Show Drive Buttons')" = false
  xdotool mousemove $((S_X+60)) $((S_Y+28+4*20)) click 1; sleep 1; shot 24-settings-advanced
  xdotool mousemove $((S_X+70)) $((S_Y+28+7*20)) click 1; sleep 1; shot 25-settings-advanced-colors
  key Escape; sleep 1
fi
check "the Settings window closes" test -z "$(win '^Panero Settings')"

# ---- 5. the gear: Quick Settings popup, "All Settings..." opens Settings at Appearance ----
MAIN=$(mainwin); eval "$(xdotool getwindowgeometry --shell "$MAIN" | sed 's/^/M_/')"
xdotool windowactivate --sync "$MAIN" 2>/dev/null
xdotool mousemove $((M_X+M_WIDTH-26)) $((M_Y+50)) click 1; sleep 1.5   # gear (menu bar shown: last button, 2nd row)
shot 30-quick-settings
key alt+a; sleep 2.5
check "Quick Settings > All Settings... opens the Settings window" waitwin '^Panero Settings' 6
shot 31-all-settings-appearance
key Escape; sleep 1

# ---- 6. Mount Manager stays on Alt+/ although it left the menus ---------------------------
xdotool mousemove $((M_X+1100)) $((M_Y+500)) click 1; sleep 1
key alt+slash; sleep 2; shot 40-mount-manager
check "Alt+/ still opens the Mount Manager" waitwin 'Mount' 6
key Escape
stop

echo "passed $PASS, failed $FAIL (screenshots: $SHOTS)"
[[ $FAIL == 0 ]]
