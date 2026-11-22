#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Freeze check of cloud drive browsing: a stand-in drive (rclone "alias" remote
# on a disposable folder, isolated profile, Xvfb), then rclone is stopped
# (SIGSTOP) so every access to the mount hangs - the worst case of a busy or
# slow provider. After each GUI action the main thread must not be waiting on
# FUSE (/proc wchan). On a freeze its stack is printed (Panero runs under gdb).
#
#   dev/scripts/gui-cloud-freeze-test.sh     (after a staged build, see gui-cloud-test.sh)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PANERO_DEV_HOME="$ROOT/build/dev-cloud"
"$ROOT/dev/scripts/dev-seed-config.sh" >/dev/null
DEVHOME="$PANERO_DEV_HOME"
DATA="$ROOT/build/dev-cloud-data"
OUT="$ROOT/build/cloud-freeze"; rm -rf "$DATA" "$OUT"; mkdir -p "$DATA/Documents/Reports" "$DATA/Photos" "$OUT"
for f in Documents/a.pdf Documents/b.pdf Documents/Reports/r1.txt Photos/p1.jpg notes.txt README; do
  head -c 4000 /dev/urandom > "$DATA/$f"
done
mkdir -p "$DEVHOME/config/panero/cloud"; chmod 700 "$DEVHOME/config/panero/cloud"
printf '[TestDrive]\ntype = alias\nremote = %s\n' "$DATA" > "$DEVHOME/config/panero/cloud/rclone.conf"
chmod 600 "$DEVHOME/config/panero/cloud/rclone.conf"
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
dbus-run-session -- bash -c '
  printf "%s" "panero-test-keyring" | gnome-keyring-daemon --unlock --components=secrets >/dev/null
  gnome-keyring-daemon --start --components=secrets >/dev/null
  exec gdb -batch -nx -ex "set pagination off" -ex "handle SIGPIPE SIGUSR1 SIGUSR2 SIG32 SIG33 SIG34 nostop noprint pass" \
    -ex run -ex "echo \n===MAIN THREAD===\n" -ex "thread 1" -ex "bt 60" -ex kill --args "$0" --left="$1" --right="$1"' \
  "$STAGE/usr/bin/panero" "$DEVHOME" >"$OUT/gdb.log" 2>&1 & DPID=$!
sleep 10
PAN=$(pgrep -f "^$STAGE/usr/bin/panero" | head -1)
echo "panero pid $PAN"
eval $("$ROOT/dev/scripts/xwin.sh")
import -window root "$OUT/01-start.png"
xdotool mousemove $((X+66)) $((Y+249)) click 1
for i in $(seq 1 40); do mountpoint -q "$MNT" && break; sleep 0.5; done
sleep 4
RC=$(pgrep -f "rclone.*mount.*TestDrive" | head -1)
echo "rclone pid $RC"
if [[ -z "$RC" ]]; then
  echo "FAIL  the stand-in drive did not mount (see $OUT)"
  kill "$PAN" 2>/dev/null; kill $DPID $XPID 2>/dev/null; exit 1
fi
import -window root "$OUT/02-opened.png"
# warm: into Documents and back
xdotool mousemove $((X+1000)) $((Y+500)) click 1; sleep 0.5
xdotool key Home Return; sleep 2; xdotool key BackSpace; sleep 2

blocked() { # main thread waiting in the kernel on FUSE?
  local w s; w=$(cat /proc/$PAN/task/$PAN/wchan 2>/dev/null); s=$(awk '{print $3}' /proc/$PAN/task/$PAN/stat 2>/dev/null)
  echo "    main thread state=$s wchan=$w"
  [[ "$w" == *fuse* || "$w" == *request_wait* ]]
}
step() { # name, xdotool args...  (or "sleep N")
  local name="$1"; shift
  echo "STEP $name"
  if [[ "$1" == sleep ]]; then sleep "$2"; else xdotool "$@"; fi
  sleep 3
  if blocked; then
    sleep 2; blocked && { echo "FROZEN after: $name"; FROZE="$name"; return 1; }
  fi
  return 0
}

echo "== rclone stopped =="; kill -STOP "$RC"
FROZE=""
step "idle" sleep 1 &&
step "cursor down" key Down &&
step "cursor down 2" key Down &&
step "enter Documents" key Home Return &&
step "wait 35s (timers)" sleep 35 &&
step "cursor in Documents" key Down &&
step "up" key BackSpace &&
step "enter Documents again" key Return &&
step "open a.pdf" key Home Down Down Return &&
step "wait 8s" sleep 8 &&
step "focus panel" mousemove $((X+1000)) $((Y+600)) click 1 &&
step "context menu" key Home Down Down shift+F10 &&
step "close menu" key Escape &&
step "ctrl+r" key ctrl+r &&
step "wait 35s" sleep 35 &&
step "up to root" key BackSpace &&
step "cursor at root" key Down &&
step "sidebar click drive" mousemove $((X+66)) $((Y+249)) click 1 &&
step "wait 25s" sleep 25
import -window root "$OUT/03-end.png"
echo "frozen: ${FROZE:-no}"
kill -INT "$PAN"; sleep 8
kill -CONT "$RC"; sleep 2
kill "$PAN" 2>/dev/null; sleep 2; kill -9 "$PAN" 2>/dev/null
kill "$RC" 2>/dev/null; sleep 1
fusermount3 -u "$MNT" 2>/dev/null || fusermount3 -uz "$MNT" 2>/dev/null
kill $DPID 2>/dev/null; kill $XPID 2>/dev/null
for p in $(pgrep -f "^[^ ]*(rclone|evince|papers)[^ ]* .*$MNT"); do kill $p 2>/dev/null; done
for p in $(pgrep -f "^/usr/bin/dbus-daemon .*$XDG_RUNTIME_DIR/"); do kill "$p" 2>/dev/null; done
if [[ -n "$FROZE" ]]; then
  echo "FAIL  window froze after: $FROZE"; sed -n '/===MAIN THREAD===/,$p' "$OUT/gdb.log" | head -40
  exit 1
fi
echo "PASS  no main-thread wait on the drive in any step (screenshots: $OUT)"
