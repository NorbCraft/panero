#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GUI check of Quick View (D-022) under Xvfb with an isolated profile:
#  1. Ctrl+Q turns the right panel into a viewer of the left panel's cursor;
#     the cursor moves over a folder, a photo, an animated GIF, code, Latin-1
#     text, a zip, a PDF, a WAV and a binary file; each must show its page.
#  2. Fast cursor movement loads only the item where the cursor stops.
#  3. Tab moves the focus into the viewer, Esc back; Ctrl+Q again shows the
#     right panel exactly as before (screenshot comparison of that half).
#  4. Cloud rule: a "cloud drive" folder (Panero's mount base below
#     XDG_CACHE_HOME, a slow path) shows "Press Space to load"; strace checks
#     that the GUI thread never opens the file, Space loads it in a worker.
# Screenshots: build/quickview-shots. Uses the staged build (build/stage).
# Extra libraries (e.g. a local sysroot with Poppler-Qt5 / Qt Multimedia) can
# be passed with LD_LIBRARY_PATH and QT_PLUGIN_PATH; they are kept.
#
#   dev/scripts/gui-quickview-test.sh
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PANERO_DEV_HOME="$ROOT/build/dev-quickview"
"$ROOT/dev/scripts/dev-seed-config.sh" >/dev/null
DEVHOME="$PANERO_DEV_HOME"
DATA="$ROOT/build/dev-quickview-data"
SHOTS="$ROOT/build/quickview-shots"
LOG="$ROOT/build/quickview-test.log"
rm -rf "$DATA" "$SHOTS"; mkdir -p "$DATA/L/0-Project/src" "$DATA/R/right-side-folder" "$SHOTS"
: > "$LOG"
PASS=0; FAIL=0
check() { # name, command...
  local name="$1"; shift
  if "$@"; then echo "PASS  $name" | tee -a "$LOG"; PASS=$((PASS+1))
  else echo "FAIL  $name" | tee -a "$LOG"; FAIL=$((FAIL+1)); fi
}
shot() { import -window root "$SHOTS/$1.png" 2>/dev/null; }

# ---- test files (sorted: folder first, then 1..8)
L="$DATA/L"
head -c 300000 /dev/urandom > "$L/0-Project/src/blob.bin"
head -c 20000 /dev/urandom > "$L/0-Project/readme.md"
convert -size 1600x1000 gradient:'#1e3a8a'-'#f59e0b' -fill white -pointsize 90 -gravity center \
  -annotate 0 'Quick View' "$L/1-photo.jpg"
convert -delay 25 -size 160x160 xc:'#dc2626' xc:'#16a34a' xc:'#2563eb' -loop 0 "$L/2-anim.gif"
cat > "$L/3-code.cpp" <<'EOF'
// Quick View syntax colours
#include <iostream>

int main(int argc, char **argv)
{
    for (int i = 0; i < argc; ++i)
        std::cout << "argument " << i << ": " << argv[i] << '\n';
    return 0; // done
}
EOF
printf 'Gr\xf6\xdfe und Gewicht: sch\xf6n, Stra\xdfe, \xc4rger, \xd6l, \xdcbung.\nCaf\xe9 cr\xe8me br\xfbl\xe9e.\n' > "$L/4-notes.txt"
python3 -I - "$L" <<'EOF'
import sys, zipfile, struct, wave, math, os
d = sys.argv[1]
with zipfile.ZipFile(os.path.join(d, "5-archive.zip"), "w") as z:
    z.writestr("docs/readme.txt", "hello from the zip\n")
    z.writestr("docs/manual.txt", "x" * 5000)
    z.writestr("image/data.bin", os.urandom(2048))
# a small PDF with two pages, written by hand
objs = []
objs.append(b"<< /Type /Catalog /Pages 2 0 R >>")
objs.append(b"<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")
def page(contents):
    return b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents %d 0 R /Resources << /Font << /F1 7 0 R >> >> >>" % contents
def stream(text):
    s = b"0.15 0.35 0.75 rg 50 600 495 180 re f BT /F1 40 Tf 1 1 1 rg 80 680 Td (" + text + b") Tj ET"
    return b"<< /Length %d >>\nstream\n" % len(s) + s + b"\nendstream"
objs.append(page(4)); objs.append(stream(b"Panero Quick View"))
objs.append(page(6)); objs.append(stream(b"Page two"))
objs.append(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
out = b"%PDF-1.4\n"
offsets = []
for i, o in enumerate(objs, 1):
    offsets.append(len(out))
    out += b"%d 0 obj\n" % i + o + b"\nendobj\n"
xref = len(out)
out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
for off in offsets:
    out += b"%010d 00000 n \n" % off
out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, xref)
open(os.path.join(d, "6-doc.pdf"), "wb").write(out)
w = wave.open(os.path.join(d, "7-sound.wav"), "wb")
w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(i / 10))) for i in range(22050)))
w.close()
EOF
head -c 5000 /dev/urandom > "$L/8-data.bin"
for i in 1 2 3; do echo "right $i" > "$DATA/R/right file $i.txt"; done

STAGE="$ROOT/build/stage"
LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero | head -1)
export HOME="$DEVHOME" XDG_CONFIG_HOME="$DEVHOME/config" XDG_DATA_HOME="$DEVHOME/data" XDG_CACHE_HOME="$DEVHOME/cache" \
       XDG_RUNTIME_DIR="$(mktemp -d /tmp/panero-qv-run.XXXXXX)" XDG_DATA_DIRS="$STAGE/usr/share:/usr/local/share:/usr/share" \
       PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins" QT_QPA_PLATFORM=xcb GIO_USE_VFS=local \
       PANERO_QUICKVIEW_TRACE=1
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; unset WAYLAND_DISPLAY
# (a short runtime folder: socket paths below a deep build folder exceed 108 bytes)
# the "cloud drive": a folder below Panero's mount base is a slow path
CLOUD="$XDG_CACHE_HOME/panero/mounts/TestDrive"
mkdir -p "$CLOUD"
seq 1 20000 > "$CLOUD/a-cloud-notes.txt"
cp "$L/1-photo.jpg" "$CLOUD/b-cloud-photo.jpg"

DISP=":$((80 + RANDOM % 9))"
Xvfb "$DISP" -screen 0 1400x900x24 -nolisten tcp >/dev/null 2>&1 & XPID=$!
sleep 1; export DISPLAY="$DISP"
RUNLOG="$DEVHOME/quickview-run.log"
STRACE="$DEVHOME/quickview.strace"
k() { xdotool key "$@"; sleep 0.4; }
lines() { wc -l < "$RUNLOG"; }
# last page line of the trace
page_is() { # expected page, item
  grep 'quickview: page=' "$RUNLOG" | tail -1 | grep -q "page=$1 item=$2"
}
wait_page() { # page item: up to 8 s
  for _ in $(seq 1 40); do page_is "$1" "$2" && return 0; sleep 0.2; done
  return 1
}
start() { # left folder, strace?
  : > "$RUNLOG"
  if [[ "${2:-}" == strace ]]; then
    dbus-run-session -- strace -f -qq -e trace=openat,open -o "$STRACE" "$STAGE/usr/bin/panero" --left="$1" --right="$DATA/R" >>"$RUNLOG" 2>&1 & APID=$!
    sleep 14
  else
    dbus-run-session -- "$STAGE/usr/bin/panero" --left="$1" --right="$DATA/R" >>"$RUNLOG" 2>&1 & APID=$!
    sleep 8
  fi
  eval $("$ROOT/dev/scripts/xwin.sh")
  # focus the left panel: click its first file row
  xdotool mousemove $((X+500)) $((Y+174)) click 1; sleep 0.5
  k Home
}
stop() { kill $APID 2>/dev/null; sleep 2; kill -9 $APID 2>/dev/null; wait $APID 2>/dev/null; }
right_half() { # the right panel's file list
  convert "$1" -crop 480x540+$((X+775))+$((Y+100)) +repage "$2"; }

# ================= phase 1: file types
start "$L"
shot 00-panels
k ctrl+q; sleep 1
check "Quick View opens" grep -q 'quickview: opened' "$RUNLOG"
shot 01-parent
k Down; check "folder: size and largest items" wait_page folder 0-Project; sleep 1.5; shot 02-folder
check "folder: calculation finished" grep -q 'quickview: folder done item=0-Project files=2 folders=1' "$RUNLOG"
k Down; check "photo (JPEG)" wait_page image 1-photo.jpg; sleep 0.5; shot 03-photo
k Down; check "animated GIF" wait_page image 2-anim.gif; sleep 0.5; shot 04-gif
k Down; check "code" wait_page text 3-code.cpp; sleep 0.5; shot 05-code
k Down; check "Latin-1 text" wait_page text 4-notes.txt; sleep 0.5; shot 06-latin1
k Down; check "zip contents" wait_page archive 5-archive.zip; sleep 0.5; shot 07-zip
if strings "$STAGE/usr/bin/panero" | grep -q -i 'poppler' || ldd "$STAGE/usr/bin/panero" | grep -q poppler-qt5; then
  k Down; check "PDF page" wait_page pdf 6-doc.pdf; sleep 0.5; shot 08-pdf
else
  k Down; check "PDF (no Poppler: file info)" wait_page info 6-doc.pdf; shot 08-pdf-info
fi
if ldd "$STAGE/usr/bin/panero" | grep -q Qt5Multimedia; then
  k Down; check "WAV player" wait_page media 7-sound.wav; sleep 0.5; shot 09-wav
else
  k Down; check "WAV (no Qt Multimedia: file info)" wait_page info 7-sound.wav; shot 09-wav-info
fi
k Down; check "binary: file info" wait_page info 8-data.bin; shot 10-binary

# ---- fast movement: only where the cursor stops is loaded
before=$(lines)
xdotool key --delay 40 Home Down Down Down Down Down; sleep 1.5
loads=$(tail -n +$((before + 1)) "$RUNLOG" | grep -c 'quickview: load item=')
echo "loads after fast movement: $loads" >> "$LOG"
check "fast movement loads once" test "$loads" -eq 1
check "stopped on the Latin-1 text" wait_page text 4-notes.txt

# ---- Tab into the viewer, Esc back, Tab/Tab
k Tab; sleep 0.5
check "Tab: focus in the viewer" bash -c "tail -3 '$RUNLOG' | grep -q 'focus=view'"
shot 11-viewer-focused
k Escape
check "Esc: focus back to the panel" bash -c "tail -3 '$RUNLOG' | grep -q 'focus=panel'"
k Down; check "cursor still moves the viewer" wait_page archive 5-archive.zip
k Tab; k Tab
check "Tab twice: back in the panel" bash -c "tail -4 '$RUNLOG' | grep -q 'focus=panel'"

# ---- close: the right panel is back unchanged
grep 'quickview:' "$RUNLOG" > "$SHOTS/trace-files.txt"
k ctrl+q; sleep 1.5
check "Quick View closes" bash -c "tail -2 '$RUNLOG' | grep -q 'quickview: closed'"
shot 12-closed
right_half "$SHOTS/00-panels.png" "$DEVHOME/before.png"; right_half "$SHOTS/12-closed.png" "$DEVHOME/after.png"
diff=$(compare -metric AE "$DEVHOME/before.png" "$DEVHOME/after.png" null: 2>&1 | cut -d' ' -f1)
echo "right panel pixels changed: $diff" >> "$LOG"
check "right panel restored" test "${diff%.*}" -lt 200
k ctrl+q; sleep 0.5; k ctrl+q; sleep 0.5
check "toggle twice works" bash -c "tail -2 '$RUNLOG' | grep -q 'quickview: closed'"
stop

# ================= phase 2: cloud rule (GUI thread never opens the file)
start "$CLOUD" strace
k ctrl+q; sleep 1
# a drive's top folder has no "..": the cursor starts on the first file
check "cloud text: waiting for Space" wait_page waiting a-cloud-notes.txt; sleep 0.5; shot 13-cloud-waiting
k Down; check "cloud photo: waiting for Space" wait_page waiting b-cloud-photo.jpg
sleep 1
MAIN=$(head -1 "$STRACE" | awk '{print $1}')
check "strace: main thread found" test -n "$MAIN"
check "no file opened before Space" bash -c "! grep -E 'cloud-(notes|photo)' '$STRACE' | grep -q 'openat'"
k space; check "Space loads the photo" wait_page image b-cloud-photo.jpg; sleep 0.5; shot 14-cloud-loaded
k Up; check "next cloud file waits again" wait_page waiting a-cloud-notes.txt
k space; check "Space loads the text" wait_page text a-cloud-notes.txt
sleep 0.5
opened=$(grep -E 'cloud-(notes|photo)' "$STRACE" | grep 'openat' | grep -v ENOENT)
echo "opens of the cloud files:" >> "$LOG"; echo "$opened" >> "$LOG"
check "cloud files opened after Space" test -n "$opened"
check "never by the GUI thread ($MAIN)" bash -c "! grep -E 'cloud-(notes|photo)' '$STRACE' | grep openat | grep -q '^$MAIN '"
grep 'quickview:' "$RUNLOG" > "$SHOTS/trace-cloud.txt"
stop

kill $XPID 2>/dev/null
for p in $(pgrep -f "^/usr/bin/dbus-daemon .*$XDG_RUNTIME_DIR/"); do kill "$p" 2>/dev/null; done
rm -rf "$XDG_RUNTIME_DIR"
echo "passed $PASS, failed $FAIL (screenshots: $SHOTS, log: $LOG)"
[[ $FAIL == 0 ]]
