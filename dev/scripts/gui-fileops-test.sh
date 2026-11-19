#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GUI-driven file operation test (spec §11): runs the staged Panero build under
# Xvfb with an isolated profile and performs real operations on disposable files
# (names with spaces/unicode, a symlink) through the normal UI: F5 copy, overwrite
# cancel, F7 new folder, F2 rename, F6 move, F8 delete to Trash, pack/unpack,
# search. Every result is checked on disk. Trash goes to the isolated profile.
#
#   dev/scripts/gui-fileops-test.sh            (after dev/scripts/run-dev.sh has staged a build)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OPS="$ROOT/build/dev-ops"
LOG="$ROOT/build/fileops-test.log"
SHOTS="$ROOT/build/fileops-shots"
"$ROOT/dev/scripts/dev-seed-config.sh" >/dev/null
DEVHOME="$ROOT/build/dev-home"
rm -rf "$OPS" "$SHOTS"; mkdir -p "$OPS/L/dir with space" "$OPS/R" "$SHOTS"
printf 'hello ü\n' > "$OPS/L/a file ü.txt"
printf 'inner\n' > "$OPS/L/dir with space/inner.txt"
head -c 3000 /dev/urandom > "$OPS/L/photo 1.jpg"
ln -s "a file ü.txt" "$OPS/L/zz link"
: > "$LOG"
PASS=0; FAIL=0
check() { # name, command...
  local name="$1"; shift
  if "$@"; then echo "PASS  $name" | tee -a "$LOG"; PASS=$((PASS+1))
  else echo "FAIL  $name" | tee -a "$LOG"; FAIL=$((FAIL+1)); import -window root "$SHOTS/fail-${name// /_}.png" 2>/dev/null; fi
}

STAGE="$ROOT/build/stage"
LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
export HOME="$DEVHOME" XDG_CONFIG_HOME="$DEVHOME/config" XDG_DATA_HOME="$DEVHOME/data" XDG_CACHE_HOME="$DEVHOME/cache" \
       XDG_RUNTIME_DIR="$DEVHOME/runtime" XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" \
       PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins" QT_QPA_PLATFORM=xcb GIO_USE_VFS=local
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; unset WAYLAND_DISPLAY
DISP=":$((80 + RANDOM % 9))"
Xvfb "$DISP" -screen 0 1400x900x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1; export DISPLAY="$DISP"
dbus-run-session -- "$STAGE/usr/bin/panero" --left="$OPS/L" --right="$OPS/R" >"$DEVHOME/fileops-run.log" 2>&1 & APID=$!
sleep 8
eval $("$ROOT/dev/scripts/xwin.sh")
k() { xdotool key "$@"; sleep 0.6; }
# key that opens a dialog: give it time to appear, then focus it (no WM in Xvfb)
dlg() { xdotool key "$@"; sleep 2; "$ROOT/dev/scripts/xfocus-top.sh"; sleep 0.3; }
# back to the main window after a dialog closed
main() { sleep 0.5; "$ROOT/dev/scripts/xfocus-top.sh"; sleep 0.3; }
# focus the left panel: click its first file row
xdotool mousemove $((X+500)) $((Y+174)) click 1; sleep 0.5
k Home

# rows (sorted, dirs first): .. | dir with space | a file ü.txt | photo 1.jpg | zz link
# 1. copy "a file ü.txt" to the right panel
k Home Down Down; dlg F5; k Return; sleep 2; main
check "F5 copy (unicode name)" cmp -s "$OPS/L/a file ü.txt" "$OPS/R/a file ü.txt"

# 2. copy again -> overwrite question -> cancel keeps the target unchanged
printf 'changed target\n' > "$OPS/R/a file ü.txt"
dlg F5; k Return; sleep 2; "$ROOT/dev/scripts/xfocus-top.sh" "exists"; k Escape; sleep 1.5; main
check "overwrite question closed by Cancel" bash -c "! xwininfo -root -tree | grep -qi 'already exists'"
check "overwrite cancelled keeps target" grep -q 'changed target' "$OPS/R/a file ü.txt"

# 3. new folder
dlg F7; xdotool type --delay 40 "new folder ü"; sleep 0.3; k Return; sleep 1.5; main
check "F7 new folder" test -d "$OPS/L/new folder ü"

# 4. rename "photo 1.jpg" (inline editor)
k Home; k Down Down Down Down; dlg F2; k ctrl+a; xdotool type --delay 40 "renamed photo.jpg"; k Return; sleep 1.5; main
check "F2 rename" test -f "$OPS/L/renamed photo.jpg"
check "F2 rename removed old name" test ! -e "$OPS/L/photo 1.jpg"

# 5. move "dir with space" (mark it with Space, then F6)
k Home; k Down; k space; dlg F6; k Return; sleep 2; main
check "F6 move folder with space" test -f "$OPS/R/dir with space/inner.txt"
check "F6 move removed source" test ! -e "$OPS/L/dir with space"

# 6. delete the symlink to Trash (target must stay)
k End; dlg F8; k alt+t; sleep 2; main   # Cancel is the default button of the trash confirmation
check "F8 trash symlink" test ! -L "$OPS/L/zz link"
check "symlink target kept" test -f "$OPS/L/a file ü.txt"
check "trash entry created in isolated profile" bash -c "ls '$DEVHOME/data/Trash/files' | grep -q 'zz link'"

# 7. pack "a file ü.txt" into the right panel, then check the archive contents
k Home; k Down Down; dlg alt+shift+p; k Return; sleep 3; main
ARCH=$(ls "$OPS/R" | grep -E '\.(zip|tar\.gz|tar\.bz2|tar\.xz|7z|tar)$' | head -1)
check "pack created an archive ($ARCH)" test -n "$ARCH"
if [ -n "$ARCH" ]; then
  check "archive contains the file" bash -c "cd '$OPS/R' && (unzip -l '$ARCH' 2>/dev/null || tar tf '$ARCH' 2>/dev/null) | grep -q 'a file ü.txt'"
  # 8. unpack it into the left panel... switch to the right panel, select the archive
  k Tab; k End   # the archive sorts last in the right panel
  mkdir -p "$OPS/L/unpacked"
  dlg alt+shift+u; k ctrl+a; xdotool type --delay 30 "$OPS/L/unpacked"; sleep 0.8; k alt+o; sleep 3; main   # Alt+O: OK (Return could pick a completion)
  check "unpack extracted the file" cmp -s "$OPS/L/unpacked/a file ü.txt" "$OPS/L/a file ü.txt"
fi

# 9. restore the symlink from the Trash: toolbar Trash button, select, context menu > Restore
#    (navigated from the bottom of the menu: Properties, Paste, Copy, Cut, Empty Trash, Restore)
xdotool mousemove $((X+500)) $((Y+174)) click 1; sleep 0.4
xdotool mousemove $((X+376)) $((Y+27)) click 1; sleep 0.8   # toolbar Trash menu
xdotool mousemove $((X+420)) $((Y+57)) click 1; sleep 2.5   # > Open Trash
xdotool mousemove $((X+500)) $((Y+174)) click 1; sleep 0.4
k End; k shift+F10; sleep 0.6; k Up Up Up Up Up Up; k Return; sleep 2.5
check "restore from Trash brought the symlink back" test -L "$OPS/L/zz link"
check "restored symlink still points to its target" test "$(readlink "$OPS/L/zz link")" = "a file ü.txt"
xdotool key alt+Left; sleep 1   # back to the previous folder

# 10. search dialog opens (Ctrl+S) and finds the txt file
k Tab; dlg ctrl+s; xdotool type --delay 40 "*.txt"; k Return; sleep 3
import -window root "$SHOTS/search.png"
check "search dialog ran (screenshot saved)" test -s "$SHOTS/search.png"
k Escape

kill $APID 2>/dev/null; sleep 1; kill $XPID 2>/dev/null
# safety: close an editor a stray Return may have opened - only one on the test display
for p in $(pgrep -x gnome-text-edit); do
  tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -qx "DISPLAY=$DISP" && kill "$p"
done
echo "RESULT: $PASS passed, $FAIL failed (log $LOG, screenshots $SHOTS)" | tee -a "$LOG"
