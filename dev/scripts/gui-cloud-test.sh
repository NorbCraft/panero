#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GUI check of cloud drive browsing (preview 9) with a stand-in drive: an
# rclone "alias" remote pointing at a disposable local folder, in an isolated
# profile under Xvfb. Checks: the path bar starts with the drive's name, no
# ".." at the drive's top folder and Backspace stays there, Rename changes
# the shown name. Screenshots go to build/cloud-shots for a visual check.
#
#   dev/scripts/gui-cloud-test.sh     (after a staged build: dev/scripts/run-dev.sh --no-build or ninja install)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PANERO_DEV_HOME="$ROOT/build/dev-cloud"
"$ROOT/dev/scripts/dev-seed-config.sh" >/dev/null
DEVHOME="$PANERO_DEV_HOME"
DATA="$ROOT/build/dev-cloud-data"
SHOTS="$ROOT/build/cloud-shots"
LOG="$ROOT/build/cloud-test.log"
rm -rf "$DATA" "$SHOTS"; mkdir -p "$DATA/Documents/Reports" "$DATA/Photos" "$SHOTS"
for f in Documents/a.txt Documents/Reports/r1.txt Photos/p1.jpg Photos/p2.jpg notes.txt; do
  head -c 2000 /dev/urandom > "$DATA/$f"
done
mkdir -p "$DEVHOME/config/panero/cloud"; chmod 700 "$DEVHOME/config/panero/cloud"
printf '[TestDrive]\ntype = alias\nremote = %s\n' "$DATA" > "$DEVHOME/config/panero/cloud/rclone.conf"
chmod 600 "$DEVHOME/config/panero/cloud/rclone.conf"
: > "$LOG"
PASS=0; FAIL=0
check() { # name, command...
  local name="$1"; shift
  if "$@"; then echo "PASS  $name" | tee -a "$LOG"; PASS=$((PASS+1))
  else echo "FAIL  $name" | tee -a "$LOG"; FAIL=$((FAIL+1)); fi
}
shot() { import -window root "$SHOTS/$1.png" 2>/dev/null; }

STAGE="$ROOT/build/stage"
LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
export HOME="$DEVHOME" XDG_CONFIG_HOME="$DEVHOME/config" XDG_DATA_HOME="$DEVHOME/data" XDG_CACHE_HOME="$DEVHOME/cache" \
       XDG_RUNTIME_DIR="$DEVHOME/runtime" XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" \
       PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins" QT_QPA_PLATFORM=xcb GIO_USE_VFS=local
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; unset WAYLAND_DISPLAY
MNT="$XDG_RUNTIME_DIR/panero-cloud/TestDrive"
DISP=":$((80 + RANDOM % 9))"
Xvfb "$DISP" -screen 0 1400x900x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1; export DISPLAY="$DISP"
# a private session bus with a test keyring below the isolated HOME
dbus-run-session -- bash -c '
  printf "%s" "panero-test-keyring" | gnome-keyring-daemon --unlock --components=secrets >/dev/null
  gnome-keyring-daemon --start --components=secrets >/dev/null
  exec "$0" --left="$1" --right="$1"' "$STAGE/usr/bin/panero" "$DEVHOME" >"$DEVHOME/cloud-run.log" 2>&1 & APID=$!
sleep 8
eval $("$ROOT/dev/scripts/xwin.sh")
shot 01-start

# open the drive: the first entry of the sidebar's Cloud section
xdotool mousemove $((X+66)) $((Y+249)) click 1
for i in $(seq 1 40); do mountpoint -q "$MNT" && break; sleep 0.5; done
sleep 3
shot 02-opened
check "drive mounted" mountpoint -q "$MNT"

# the panel shows the drive: focus it, Backspace (up) must stay in the drive
xdotool mousemove $((X+1000)) $((Y+500)) click 1; sleep 0.5
xdotool key Home; sleep 0.3; xdotool key BackSpace; sleep 1.5
shot 03-after-backspace
# into Documents/Reports and back up twice: ends at the drive's top folder
xdotool mousemove $((X+1000)) $((Y+500)) click 1; sleep 0.5
xdotool key Home Return; sleep 1.5; xdotool key Home Down Return; sleep 1.5; shot 04-reports
xdotool key BackSpace; sleep 1.5; xdotool key BackSpace; sleep 1.5; xdotool key BackSpace; sleep 1.5
shot 05-up-three-times

# Rename: right-click the sidebar entry, "Rename...", type a new name
xdotool mousemove $((X+66)) $((Y+249)) click 3; sleep 1; shot 06-menu
xdotool mousemove $((X+126)) $((Y+339)) click 1; sleep 2; "$ROOT/dev/scripts/xfocus-top.sh" "Rename"; sleep 0.5
xdotool key ctrl+a; xdotool type --delay 30 "Work Drive"; xdotool key Return; sleep 2
"$ROOT/dev/scripts/xfocus-top.sh"; sleep 0.5
shot 07-renamed
check "new name stored" grep -q "Remote Labels=Work Drive" "$DEVHOME/config/panerorc"

# folder badge: read everything in Photos through the drive -> green badge
cat "$MNT"/Photos/* >/dev/null; sleep 22
xdotool mousemove $((X+1000)) $((Y+500)) click 1; sleep 1; xdotool mousemove $((X+1000)) $((Y+210)); sleep 3
shot 08-photos-offline

kill $APID 2>/dev/null; sleep 3; kill -9 $APID 2>/dev/null
fusermount3 -u "$MNT" 2>/dev/null
kill $XPID 2>/dev/null
echo "passed $PASS, failed $FAIL (screenshots: $SHOTS)"
[[ $FAIL == 0 ]]
