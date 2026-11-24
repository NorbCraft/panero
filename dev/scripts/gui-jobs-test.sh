#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GUI test for D-022 file operation features (preview 13): marks cleared after F5,
# Undo (Ctrl+Z) of trash, move, rename and new folder, folder size on Space, the
# progress window of a long copy, "Keep in Background", the jobs button and its
# menu, reopening the window from the menu and the notice when a background job
# ends (a fake notification server on the private D-Bus session logs it).
# Runs the staged build under Xvfb with an isolated profile on disposable files.
# XDG_RUNTIME_DIR is a short folder in /tmp: KIO's worker sockets live there and a
# deep worktree path exceeds the 108-byte socket path limit.
#
#   dev/scripts/gui-jobs-test.sh     (after dev/scripts/run-dev.sh has staged a build)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# the disposable files live in /tmp (short paths, no personal folder names in the screenshots)
OPS="$(mktemp -d /tmp/panero-jobs-ops-XXXXXX)"
LOG="$ROOT/build/jobs-test.log"
SHOTS="$ROOT/build/jobs-shots"
NOTES="$ROOT/build/jobs-notices.log"
"$ROOT/dev/scripts/dev-seed-config.sh" >/dev/null
DEVHOME="$ROOT/build/dev-home"
rm -rf "$SHOTS" "$NOTES"; mkdir -p "$OPS/L/many" "$OPS/L/sized" "$OPS/R" "$SHOTS"
for f in del mv one ren three two; do printf '%s\n' "$f" > "$OPS/L/$f.txt"; done
head -c 1048576 /dev/urandom > "$OPS/L/sized/one-mib.bin"
head -c 2097152 /dev/urandom > "$OPS/L/sized/two-mib.bin"
# a copy that runs long enough for the progress window: many small files
python3 -I -c "
import os, sys
d = sys.argv[1]
for i in range(int(sys.argv[2])):
    with open(os.path.join(d, 'f%05d.dat' % i), 'wb') as f:
        f.write(os.urandom(2048))
" "$OPS/L/many" "${PANERO_JOBS_TEST_FILES:-12000}"
: > "$LOG"; : > "$NOTES"
PASS=0; FAIL=0
check() { # name, command...
  local name="$1"; shift
  if "$@"; then echo "PASS  $name" | tee -a "$LOG"; PASS=$((PASS+1))
  else echo "FAIL  $name" | tee -a "$LOG"; FAIL=$((FAIL+1)); import -window root "$SHOTS/fail-${name// /_}.png" 2>/dev/null; fi
}
shot() { import -window root "$SHOTS/$1.png" 2>/dev/null; }

STAGE="$ROOT/build/stage"
LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
export HOME="$DEVHOME" XDG_CONFIG_HOME="$DEVHOME/config" XDG_DATA_HOME="$DEVHOME/data" XDG_CACHE_HOME="$DEVHOME/cache" \
       XDG_RUNTIME_DIR="$(mktemp -d /tmp/panero-jobs-XXXXXX)" XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" \
       PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins" QT_QPA_PLATFORM=xcb GIO_USE_VFS=local PANERO_DEBUG_JOBS=1 PANERO_JOB_WINDOW_DELAY=0
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; unset WAYLAND_DISPLAY
DISP=":$((70 + RANDOM % 9))"
Xvfb "$DISP" -screen 0 1400x900x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1; export DISPLAY="$DISP"
dbus-run-session -- bash -c "python3 -I '$ROOT/dev/scripts/fake-notifyd.py' '$NOTES' & sleep 0.7; exec '$STAGE/usr/bin/panero' --left='$OPS/L' --right='$OPS/R'" >"$DEVHOME/jobs-run.log" 2>&1 & APID=$!
sleep 8
eval $("$ROOT/dev/scripts/xwin.sh")
k() { xdotool key "$@"; sleep 0.6; }
dlg() { xdotool key "$@"; sleep 2; "$ROOT/dev/scripts/xfocus-top.sh"; sleep 0.3; }
main() { sleep 0.5; "$ROOT/dev/scripts/xfocus-top.sh" "Panero"; sleep 0.3; }
viewable() { # window title part -> exit 0 if a viewable window has it
  python3 -I - "$1" <<'PY'
import re, subprocess, sys
want = sys.argv[1].lower()
out = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True, text=True).stdout
for m in re.finditer(r'^\s+(0x[0-9a-f]+) "([^"]*)"', out, re.M):
    if want in m.group(2).lower():
        info = subprocess.run(["xwininfo", "-id", m.group(1)], capture_output=True, text=True).stdout
        if "IsViewable" in info:
            sys.exit(0)
sys.exit(1)
PY
}
xdotool mousemove $((X+500)) $((Y+174)) click 1; sleep 0.5
k Home
# rows: .. | many | sized | del.txt | mv.txt | one.txt | ren.txt | three.txt | two.txt

# 1. mark one.txt and two.txt, F5: copied, then the marks are gone (F8 trashes only the cursor row)
k Home; for i in 1 2 3 4 5; do k Down; done; k Insert; k End; k Insert
dlg F5; k Return; sleep 2; main
check "F5 copied both marked files" test -f "$OPS/R/one.txt" -a -f "$OPS/R/two.txt"
shot after-f5
k Home; for i in 1 2 3 4 5 6 7; do k Down; done   # three.txt
dlg F8; k alt+t; sleep 2; main
check "marks cleared after F5 (F8 took only the cursor file)" test ! -e "$OPS/L/three.txt" -a -f "$OPS/L/one.txt" -a -f "$OPS/L/two.txt"

# 2. Ctrl+Z restores the trashed file
k ctrl+z; sleep 2.5; main
check "Ctrl+Z restored the trashed file" test -f "$OPS/L/three.txt"

# 3. move and undo
k Home; for i in 1 2 3 4; do k Down; done   # mv.txt
dlg F6; k Return; sleep 2; main
check "F6 moved mv.txt" test -f "$OPS/R/mv.txt" -a ! -e "$OPS/L/mv.txt"
k ctrl+z; sleep 2.5; main
check "Ctrl+Z undid the move" test -f "$OPS/L/mv.txt" -a ! -e "$OPS/R/mv.txt"

# 4. rename and undo
k Home; for i in 1 2 3 4 5 6; do k Down; done   # ren.txt
dlg F2; k ctrl+a; xdotool type --delay 40 "renamed.txt"; k Return; sleep 1.5; main
check "F2 renamed" test -f "$OPS/L/renamed.txt"

k ctrl+z; sleep 2.5; main
check "Ctrl+Z undid the rename" test -f "$OPS/L/ren.txt" -a ! -e "$OPS/L/renamed.txt"

# 5. new folder and undo
dlg F7; xdotool type --delay 40 "newdir"; k Return; sleep 1.5; main
check "F7 created newdir" test -d "$OPS/L/newdir"
# (KIO asks before it deletes the folder again: "Delete Permanently")
k ctrl+z; sleep 2; "$ROOT/dev/scripts/xfocus-top.sh"; shot undo-new-folder-question; k alt+p; sleep 2; main
check "Ctrl+Z removed the new folder" test ! -e "$OPS/L/newdir"

# 6. Space on a folder: size instead of <DIR>, and the folder is marked
k Home; k Down; k Down   # sized
k space; sleep 2.5
shot space-folder-size
convert "$SHOTS/space-folder-size.png" -crop 700x260+$((X))+$((Y+120)) "$SHOTS/space-folder-size-crop.png" 2>/dev/null
k space; sleep 0.5   # unmark again

# 7. long copy: progress window, pause, keep in background, jobs button and menu, reopen,
#    resume, notice at the end. PANERO_JOB_WINDOW_DELAY=0 shows the window at once and the
#    copy is paused right away, so the steps do not race a fast disk.
N="${PANERO_JOBS_TEST_FILES:-12000}"
k Home; k Down   # many
dlg F5; xdotool key Return; sleep 0.4
"$ROOT/dev/scripts/xfocus-top.sh" "Copying many"; xdotool key alt+p; sleep 1.5
check "progress window shown for a long copy" viewable "Copying many"
shot progress-window-paused
check "copy paused (not all files copied yet)" test "$(ls "$OPS/R/many" 2>/dev/null | wc -l)" -lt "$N"
"$ROOT/dev/scripts/xfocus-top.sh" "Copying many"; k Escape; sleep 0.8
check "Keep in Background hides the window" bash -c "$(declare -f viewable); ! viewable 'Copying many'"
shot jobs-button
convert "$SHOTS/jobs-button.png" -crop 460x60+$((X+900))+$((Y)) "$SHOTS/jobs-button-crop.png" 2>/dev/null
# the jobs button: left of the colours (gear) and menu buttons at the right end of the toolbar
BX=$((X + ${PANERO_JOBS_BUTTON_X:-1159}))
xdotool mousemove "$BX" $((Y+27)) click 1; sleep 1.2
shot jobs-menu
# first entry: the job's description label (a click shows the window)
xdotool mousemove $((BX-150)) $((Y+60)) click 1; sleep 1.5
check "jobs menu entry reopened the progress window" viewable "Copying many"
shot progress-window-reopened
"$ROOT/dev/scripts/xfocus-top.sh" "Copying many"; xdotool key alt+r; sleep 0.5; k Escape; sleep 0.5
for i in $(seq 1 180); do [ "$(ls "$OPS/R/many" 2>/dev/null | wc -l)" -ge "$N" ] && break; sleep 1; done
sleep 3
check "long copy completed after Resume" test "$(ls "$OPS/R/many" | wc -l)" -ge "$N"
check "notice when the background job ended" grep -q "File operation finished" "$NOTES"
shot after-background-job

kill $APID 2>/dev/null; sleep 1; kill $XPID 2>/dev/null
rm -rf "$XDG_RUNTIME_DIR" "$OPS"
echo "notices:"; cat "$NOTES"
echo "RESULT: $PASS passed, $FAIL failed (log $LOG, screenshots $SHOTS)" | tee -a "$LOG"
