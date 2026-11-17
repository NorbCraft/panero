#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Isolated end-to-end test of Panero's "Install KDiff3?" flow (spec §9) in a clean
# ubuntu:24.04 container: own system D-Bus, polkitd and packagekitd, Panero
# installed from the .deb, driven under Xvfb with xdotool. The host system and
# its packages are not touched.
#
#   dev/scripts/test-kdiff3-install.sh dist/panero_*.deb [allow|deny|broken] [outdir]
#
#   allow  - a polkit rule authorises the test user: KDiff3 must get installed from
#            the Ubuntu archive and the comparison must start (kdiff3 process)
#   deny   - polkit refuses: nothing is installed, Panero treats it as cancelled
#   broken - software sources unusable: Panero shows the guidance/retry dialog
#
# Requires Docker (run via `sg docker -c ...` if the group is not active yet).
set -euo pipefail
DEB="$(readlink -f "${1:?path to panero .deb}")"
MODE="${2:-allow}"
OUT="$(readlink -m "${3:-dist/test-kdiff3-$MODE}")"
mkdir -p "$OUT"
SCRIPT="$OUT/inner.sh"
cat > "$SCRIPT" <<'INNER'
#!/usr/bin/env bash
set -uo pipefail
MODE="$1"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq /work/panero.deb packagekit polkitd dbus xvfb xauth xdotool x11-utils imagemagick procps python3 >/dev/null 2>&1 \
  || { echo "RESULT: setup failed"; exit 2; }
dpkg -s kdiff3 >/dev/null 2>&1 && { echo "RESULT: kdiff3 unexpectedly preinstalled"; exit 2; }

userdel -r ubuntu 2>/dev/null || true
useradd -m -u 1000 tester   # uid 1000: may write the mounted /out
case "$MODE" in
  allow) RESULT_JS='return polkit.Result.YES;';;
  *)     RESULT_JS='return polkit.Result.NO;';;
esac
cat > /etc/polkit-1/rules.d/49-panero-test.rules <<RULES
polkit.addRule(function(action, subject) {
  if (action.id.indexOf("org.freedesktop.packagekit.") == 0 && subject.user == "tester") { $RESULT_JS }
});
RULES
if [ "$MODE" = broken ]; then
  rm -rf /var/lib/apt/lists/*            # no package lists: kdiff3 cannot be resolved
  echo 'Acquire::http::Proxy "http://127.0.0.1:9/";' > /etc/apt/apt.conf.d/99broken
fi

mkdir -p /run/dbus && dbus-daemon --system --fork
/usr/lib/polkit-1/polkitd --no-debug >/tmp/polkitd.log 2>&1 &
sleep 1
PKD=$(ls /usr/libexec/packagekitd /usr/lib/packagekit/packagekitd 2>/dev/null | head -1)
"$PKD" >/tmp/packagekitd.log 2>&1 &
sleep 2

# test data and a returning-user profile (no first-run dialogs)
su tester -c 'mkdir -p ~/A ~/B ~/.config && printf "one\ntwo\n" > ~/A/"same name ü.txt" && printf "one\n2\n" > ~/B/"same name ü.txt" && printf "First Time=false\nConfig Version=1\n\n[ViewerModule]\nFirstRun=false\n\n[Panero]\nKrusader Import Offered=true\n" > ~/.config/panerorc'

cat > /tmp/drive.sh <<'DRIVE'
export HOME=/home/tester QT_QPA_PLATFORM=xcb
Xvfb :5 -screen 0 1400x900x24 -nolisten tcp >/dev/null 2>&1 & sleep 1
export DISPLAY=:5
panero --left="$HOME/A" --right="$HOME/B" >/tmp/panero.log 2>&1 &
sleep 8
eval $(xwininfo -root -tree | python3 -I -c '
import re,sys
b=None
for l in sys.stdin:
    if "panero" not in l.lower(): continue
    m=re.search(r"(\d+)x(\d+)\+-?\d+\+-?\d+\s+\+(-?\d+)\+(-?\d+)",l)
    if m:
        w,h,x,y=map(int,m.groups())
        if b is None or w*h>b[0]: b=(w*h,x,y)
print("X=%d Y=%d"%(b[1:] if b else (0,0)))')
# right panel: first file row, then the toolbar's Compare button
xdotool mousemove $((X+830)) $((Y+174)) click 1; sleep 0.5
xdotool mousemove $((X+275)) $((Y+40)) click 1; sleep 2
import -window root /out/1-install-offer.png
xdotool key alt+i                      # "Install"
for i in $(seq 1 90); do sleep 2; pgrep -x kdiff3 >/dev/null && break; [ $i -gt 15 ] && [ "$(pgrep -f packagekitd -c)" = 0 ] && break; done
sleep 3
import -window root /out/2-after-install.png
pgrep -a kdiff3 > /out/kdiff3-process.txt || true
DRIVE
chmod +x /tmp/drive.sh
su tester -c 'dbus-run-session -- bash /tmp/drive.sh'

echo "--- packagekitd log (tail)"; tail -5 /tmp/packagekitd.log
if dpkg -s kdiff3 >/dev/null 2>&1; then INSTALLED=yes; else INSTALLED=no; fi
RUNNING=$([ -s /out/kdiff3-process.txt ] && echo yes || echo no)
echo "RESULT: mode=$MODE kdiff3_installed=$INSTALLED kdiff3_started=$RUNNING"
INNER
chmod +x "$SCRIPT"
cp "$DEB" "$OUT/panero.deb"
docker run --rm -v "$OUT:/work" -v "$OUT:/out" ubuntu:24.04 bash /work/inner.sh "$MODE" | tee "$OUT/result.txt"
rm -f "$OUT/panero.deb"
