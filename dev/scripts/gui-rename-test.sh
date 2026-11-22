#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GUI check of Multi-Rename (Ctrl+M, D-019) on disposable files in an isolated
# profile under Xvfb: select all, pattern "[P] [C]", Start, then Undo; the
# results are checked on disk. Screenshots go to build/rename-shots.
#
#   dev/scripts/gui-rename-test.sh     (after a staged build: dev/scripts/run-dev.sh --no-build or ninja install)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PANERO_DEV_HOME="$ROOT/build/dev-cloud"
"$ROOT/dev/scripts/dev-seed-config.sh" >/dev/null
DEVHOME="$PANERO_DEV_HOME"
DATA="$ROOT/build/dev-cloud-data"
SHOTS="$ROOT/build/rename-shots"
LOG="$ROOT/build/cloud-test.log"
rm -rf "$DATA" "$SHOTS"; mkdir -p "$DATA/Documents/Reports" "$DATA/Photos" "$SHOTS"
for f in Documents/a.txt Documents/Reports/r1.txt Photos/p1.jpg Photos/p2.jpg notes.txt; do
  head -c 2000 /dev/urandom > "$DATA/$f"
done
mkdir -p "$DEVHOME/config/panero/cloud"; chmod 700 "$DEVHOME/config/panero/cloud"
printf '[TestDrive]\ntype = alias\nremote = %s\n' "$DATA" > "$DEVHOME/config/panero/cloud/rclone.conf"
chmod 600 "$DEVHOME/config/panero/cloud/rclone.conf"
: > "$LOG"
rm -rf "$DEVHOME/Holiday"; mkdir -p "$DEVHOME/Holiday"; for f in IMG_4021.jpg IMG_4022.jpg DSC_0007.jpg notes.txt; do echo x > "$DEVHOME/Holiday/$f"; done
mkdir -p "$DEVHOME/config"; printf '\n[Panero Appearance]\nShow Drive Buttons=true\n' >> "$DEVHOME/config/panerorc"
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
  exec "$0" --left="$1" --right="$2"' "$STAGE/usr/bin/panero" "$DEVHOME/Holiday" "$DEVHOME" >"$DEVHOME/cloud-run.log" 2>&1 & APID=$!
sleep 8
eval $("$ROOT/dev/scripts/xwin.sh")
shot 01-start



xdotool mousemove $((X+600)) $((Y+500)) click 1; sleep 0.5
xdotool key ctrl+a; sleep 0.5; xdotool key ctrl+m; sleep 2; "$ROOT/dev/scripts/xfocus-top.sh" "Multi-Rename"; sleep 0.5
xdotool type --delay 40 "[P] [C]"; sleep 1; shot 02-dialog
xdotool key Return; sleep 3; shot 03-renamed
check "renamed with [P] [C]" test "$(ls "$DEVHOME/Holiday" | tr '\n' ' ')" = "Holiday 1.jpg Holiday 2.jpg Holiday 3.jpg Holiday 4.txt "
xdotool key alt+u; sleep 3; shot 04-undo
check "undo restores the names" test "$(ls "$DEVHOME/Holiday" | tr '\n' ' ')" = "DSC_0007.jpg IMG_4021.jpg IMG_4022.jpg notes.txt "
kill $APID 2>/dev/null; sleep 3; kill -9 $APID 2>/dev/null; kill $XPID 2>/dev/null
for p in $(pgrep -f "^/usr/bin/dbus-daemon .*$XDG_RUNTIME_DIR/"); do kill "$p" 2>/dev/null; done
echo "passed $PASS, failed $FAIL (screenshots: $SHOTS)"
[[ $FAIL == 0 ]]
