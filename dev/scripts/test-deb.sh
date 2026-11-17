#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test the Panero .deb in clean ubuntu:24.04 containers (no build
# dependencies, no source tree): installation with dependency resolution,
# --version, installed files and runtime libraries, the private KIO workers,
# coexistence with the distro krusader package, upgrade from an older
# Panero package, and remove/purge leaving no Panero files.
#
# Usage: dev/scripts/test-deb.sh [--deb <new.deb>] [--old-deb <old.deb>]
#                                [--log <file>] [--only <scenario>]
#   --deb      package under test (default: the newest dist/panero_*_amd64.deb)
#   --old-deb  older Panero package for the upgrade test (skipped if absent)
#   --log      write the full log here (default: dist/test-deb.log)
#   --only     run one scenario: install | coexist | upgrade | remove
#
# Each scenario runs in its own fresh container. The ubuntu Docker image is
# "minimized" (dpkg excludes docs, man pages and translations); the tests
# remove that exclusion first so the package is installed as on a desktop.
# Exit status is non-zero if any check fails.
set -euo pipefail

if ! docker info >/dev/null 2>&1 && [ -z "${PANERO_SG_REEXEC:-}" ] \
        && id -nG "$(id -un)" 2>/dev/null | grep -qw docker; then
    export PANERO_SG_REEXEC=1
    exec sg docker -c "$(printf '%q ' "$0" "$@")"
fi

repo_root=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
deb=
old_deb=
log=
only=
image=${PANERO_TEST_IMAGE:-ubuntu:24.04}

while [ $# -gt 0 ]; do
    case "$1" in
        --deb) deb=$2; shift 2 ;;
        --old-deb) old_deb=$2; shift 2 ;;
        --log) log=$2; shift 2 ;;
        --only) only=$2; shift 2 ;;
        -h|--help) sed -n '5,/^set -euo/p' "$0" | sed '/^set -euo/d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$deb" ]; then
    deb=$(ls -t "$repo_root"/dist/panero_*_amd64.deb 2>/dev/null | head -n 1 || true)
fi
[ -n "$deb" ] && [ -f "$deb" ] || { echo "no .deb found (use --deb)" >&2; exit 2; }
deb=$(cd "$(dirname "$deb")" && pwd)/$(basename "$deb")
log=${log:-$repo_root/dist/test-deb.log}
mkdir -p "$(dirname "$log")"

# Stage the packages in a private directory that is mounted read-only.
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp "$deb" "$stage/"
new_name=$(basename "$deb")
old_name=
if [ -n "$old_deb" ]; then
    cp "$old_deb" "$stage/"
    old_name=$(basename "$old_deb")
fi
chmod -R a+rX "$stage"

docker pull -q "$image" >/dev/null
image_digest=$(docker image inspect --format '{{join .RepoDigests " "}}' "$image" 2>/dev/null || true)

# Helpers available to every scenario (run inside the container).
read -r -d '' common <<'EOF' || true
set -uo pipefail
export DEBIAN_FRONTEND=noninteractive
fails=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; fails=$((fails + 1)); }
info() { echo "INFO: $*"; }
check() { local d=$1; shift; if "$@"; then pass "$d"; else fail "$d"; fi; }
# A check that is expected to fail because of a documented known issue: it
# reports KNOWN instead of FAIL (and PASS once the issue is fixed).
known() { local d=$1 why=$2; shift 2; if "$@"; then pass "$d"; else echo "KNOWN: $d -- $why"; fi; }
pkg_installed() { dpkg-query -W -f '${Status}' "$1" 2>/dev/null | grep -q 'ok installed'; }
finish() { echo "RESULT: $fails failure(s)"; exit $((fails > 0)); }
# Desktop-like dpkg configuration: install docs, man pages and translations.
rm -f /etc/dpkg/dpkg.cfg.d/excludes
apt-get update -qq
apt_install() { apt-get install -y -q "$@" > /tmp/apt.log 2>&1; local s=$?; tail -n 3 /tmp/apt.log; return $s; }
run_panero_version() {
    local home; home=$(mktemp -d)
    HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share \
        XDG_RUNTIME_DIR=$home/run QT_QPA_PLATFORM=offscreen panero --version 2>&1
}
installed_version() { dpkg-query -W -f '${Version}' panero 2>/dev/null; }
# Files (not directories) of the panero package that are still on disk.
leftover_files() { while read -r f; do [ -e "$f" ] && [ ! -d "$f" ] && echo "$f"; done < "$1"; }
EOF

read -r -d '' scenario_install <<'EOF' || true
cd /debs
info "apt install ./$NEW_DEB (with Recommends, as a desktop user would)"
before=$(dpkg-query -W -f '${Package}\n' | sort)
if apt-get install -y -q "./$NEW_DEB" > /tmp/apt.log 2>&1; then
    pass "apt install ./$NEW_DEB resolves all dependencies from the Ubuntu archive"
else
    tail -n 30 /tmp/apt.log; fail "apt install ./$NEW_DEB"; finish
fi
after=$(dpkg-query -W -f '${Package}\n' | sort)
info "packages newly installed: $(comm -13 <(echo "$before") <(echo "$after") | wc -l)"
check "dpkg status is 'install ok installed'" \
    test "$(dpkg-query -W -f '${Status}' panero)" = "install ok installed"
info "installed version: $(installed_version)"
for p in kio kinit ktexteditor-katepart breeze-icon-theme kde-cli-tools kio-extras packagekit; do
    check "dependency/recommendation $p installed" pkg_installed "$p"
done
for p in kdiff3 krusader; do
    check "$p NOT pulled in (optional)" bash -c "! dpkg-query -W -f '\${Status}' $p 2>/dev/null | grep -q 'ok installed'"
done
info "desktop launchers added by Panero and its dependencies (excluding NoDisplay):"
for f in /usr/share/applications/*.desktop; do
    grep -q '^NoDisplay=true' "$f" || echo "    $(basename "$f"): $(grep -m1 '^Name=' "$f") $(grep -m1 -E '^(OnlyShowIn|NotShowIn)=' "$f")"
done
out=$(run_panero_version); echo "$out" | sed 's/^/    /'
check "panero --version prints the Panero version" grep -q '^panero 0\.' <<< "$out"
home=$(mktemp -d)
check "panero --help describes Panero as a fork of Krusader" \
    bash -c "HOME=$home QT_QPA_PLATFORM=offscreen panero --help 2>&1 | grep -q 'fork of Krusader'"
for f in /usr/bin/panero \
         /usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/kio_krarc.so \
         /usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/kio_iso.so \
         /etc/xdg/panero_kio_isorc \
         /usr/share/applications/io.github.norbcraft.panero.desktop \
         /usr/share/metainfo/io.github.norbcraft.panero.metainfo.xml \
         /usr/share/icons/hicolor/scalable/apps/io.github.norbcraft.panero.svg \
         /usr/share/icons/hicolor/16x16/apps/io.github.norbcraft.panero.png \
         /usr/share/icons/hicolor/22x22/apps/io.github.norbcraft.panero.png \
         /usr/share/icons/hicolor/24x24/apps/io.github.norbcraft.panero.png \
         /usr/share/icons/hicolor/32x32/apps/io.github.norbcraft.panero.png \
         /usr/share/icons/hicolor/48x48/apps/io.github.norbcraft.panero.png \
         /usr/share/icons/hicolor/64x64/apps/io.github.norbcraft.panero.png \
         /usr/share/icons/hicolor/128x128/apps/io.github.norbcraft.panero.png \
         /usr/share/icons/hicolor/256x256/apps/io.github.norbcraft.panero.png \
         /usr/share/kxmlgui5/panero/paneroui.rc \
         /usr/share/panero/layout.xml \
         /usr/share/locale/de/LC_MESSAGES/panero.mo \
         /usr/share/doc/HTML/en/panero/index.docbook \
         /usr/share/man/man1/panero.1.gz \
         /usr/share/doc/panero/copyright \
         /usr/share/doc/panero/changelog.Debian.gz \
         /usr/share/doc/panero/LICENSES/GPL-2.0-or-later.txt.gz \
         /usr/share/doc/panero/LICENSES/GFDL-1.1-or-later.txt.gz \
         /usr/share/doc/panero/LICENSES/LGPL-3.0-or-later.txt.gz \
         /usr/share/doc/panero/LICENSES/FSFAP.txt; do
    check "installed: $f" test -e "$f"
done
for d in LICENSING.md USER-GUIDE.md README.md; do
    check "installed: /usr/share/doc/panero/$d[.gz]" bash -c "ls /usr/share/doc/panero/$d* >/dev/null 2>&1"
done
check "handbook docbook files are not compressed (help:/panero)" \
    bash -c "! ls /usr/share/doc/HTML/*/panero/*.docbook.gz >/dev/null 2>&1"
check "copyright file is DEP-5 and names GPL-2+" \
    bash -c "head -n 1 /usr/share/doc/panero/copyright | grep -q copyright-format/1.0 && grep -q '^License: GPL-2+' /usr/share/doc/panero/copyright"
check "copyright file carries the fork statement" \
    grep -q 'independent fork of Krusader' /usr/share/doc/panero/copyright
check "/etc/xdg/panero_kio_isorc is a conffile" \
    bash -c "dpkg-query -W -f '\${Conffiles}' panero | grep -q /etc/xdg/panero_kio_isorc"
for f in /usr/bin/panero /usr/lib/x86_64-linux-gnu/panero/plugins/kf5/kio/*.so; do
    check "all shared libraries resolve for $(basename "$f")" bash -c "! ldd '$f' | grep -q 'not found'"
done
check "no file outside the expected Panero paths" bash -c "! dpkg -L panero | grep -v -E \
'^/(\.|usr|usr/bin|usr/lib|usr/lib/x86_64-linux-gnu|usr/share|etc|etc/xdg)$|^/usr/share/lintian(/overrides(/panero)?)?$|^/usr/bin/panero$|/panero(/|$)|^/usr/share/(applications|metainfo|icons/hicolor/[^/]+/apps)/io\.github\.norbcraft\.panero\.|^/usr/share/locale/[^/]+/LC_MESSAGES/panero\.mo$|^/usr/share/man/man1/panero\.1\.gz$|^/etc/xdg/panero_kio_isorc$|^/usr/share/(applications|metainfo|icons|icons/hicolor|icons/hicolor/[^/]+|icons/hicolor/[^/]+/apps|locale|locale/[^/]+|locale/[^/]+/LC_MESSAGES|man|man/man1|doc|doc/HTML|doc/HTML/[^/]+|kxmlgui5)$'"

apt_install desktop-file-utils appstream >/dev/null
check "desktop-file-validate" desktop-file-validate /usr/share/applications/io.github.norbcraft.panero.desktop
appstreamcli validate --no-net /usr/share/metainfo/io.github.norbcraft.panero.metainfo.xml > /tmp/as.log 2>&1
as_status=$?; sed 's/^/    /' /tmp/as.log
check "appstreamcli validate (no errors)" test "$as_status" -eq 0

# KIO workers: load Panero's private workers through the system KIO with a
# real archive (kioclient5 comes from kde-cli-tools, a Recommends).
apt_install zip xorriso dbus >/dev/null
mkdir -p /tmp/kio/content && echo "hello panero" > /tmp/kio/content/hello.txt
(cd /tmp/kio/content && zip -q ../test.zip hello.txt)
xorriso -as mkisofs -quiet -R -o /tmp/kio/test.iso /tmp/kio/content 2>/dev/null
kio_cat() {
    local home; home=$(mktemp -d)
    HOME=$home XDG_RUNTIME_DIR=$home QT_QPA_PLATFORM=offscreen \
        QT_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/panero/plugins \
        timeout 60 dbus-run-session -- kioclient5 --noninteractive cat "$1" 2>/tmp/kio.err
}
out=$(kio_cat "krarc:/tmp/kio/test.zip/hello.txt"); echo "    krarc: ${out:-<empty>} $(head -c 300 /tmp/kio.err)"
check "kio_krarc.so reads a file from a zip archive" test "$out" = "hello panero"
out=$(kio_cat "iso:/tmp/kio/test.iso/hello.txt"); echo "    iso: ${out:-<empty>} $(head -c 300 /tmp/kio.err)"
known "kio_iso.so reads a file from an ISO image" \
    "Krusader 2.8.1's kio_iso does not recognise noble's application/vnd.efi.iso mime type (distro krusader behaves the same); fixed upstream in 4c85272cd and c6945e68e, backport pending" \
    test "$out" = "hello panero"

# Smoke start of the GUI (offscreen, temporary HOME): must still be running
# after 15 s (exit 124 from timeout) or exit cleanly.
home=$(mktemp -d)
HOME=$home XDG_RUNTIME_DIR=$home QT_QPA_PLATFORM=offscreen \
    timeout 15 dbus-run-session -- panero > /tmp/gui.log 2>&1
gui_status=$?
info "GUI smoke start exit status $gui_status (124 = still running after 15 s)"
grep -i -E 'katepart|ktexteditor|not found|cannot|error' /tmp/gui.log | head -n 10 | sed 's/^/    /'
check "GUI starts offscreen without crashing" bash -c "[ $gui_status -eq 124 ] || [ $gui_status -eq 0 ]"
check "no missing-editor (katepart) warning" bash -c "! grep -qi 'katepart' /tmp/gui.log"
finish
EOF

read -r -d '' scenario_coexist <<'EOF' || true
cd /debs
info "install the Ubuntu krusader package first, then Panero"
check "apt install krusader (Ubuntu archive)" apt_install krusader
info "krusader version: $(dpkg-query -W -f '${Version}' krusader)"
check "apt install ./$NEW_DEB next to krusader (no file conflicts)" apt_install "./$NEW_DEB"
dpkg -L krusader | sort > /tmp/k.list; dpkg -L panero | sort > /tmp/p.list
shared=$(comm -12 /tmp/k.list /tmp/p.list | while read -r f; do [ -d "$f" ] || echo "$f"; done)
check "no file is shared between krusader and panero" test -z "$shared"
[ -n "$shared" ] && echo "$shared" | sed 's/^/    shared: /'
check "dpkg --verify krusader (krusader files untouched)" dpkg --verify krusader
check "dpkg --verify panero" dpkg --verify panero
home=$(mktemp -d)
out=$(HOME=$home QT_QPA_PLATFORM=offscreen krusader --version 2>&1); echo "    $out"
check "krusader --version still works" grep -q '^krusader ' <<< "$out"
out=$(run_panero_version); echo "$out" | sed 's/^/    /'
check "panero --version works next to krusader" grep -q '^panero 0\.' <<< "$out"
check "apt remove panero" bash -c "apt-get remove -y -q panero > /tmp/apt.log 2>&1"
check "krusader still intact after removing panero" dpkg --verify krusader
info "reverse order: purge krusader, reinstall panero first, then krusader"
check "apt purge krusader" bash -c "apt-get purge -y -q krusader > /tmp/apt.log 2>&1"
check "apt install ./$NEW_DEB" apt_install "./$NEW_DEB"
check "apt install krusader after panero" apt_install krusader
check "dpkg --verify panero after krusader install" dpkg --verify panero
finish
EOF

read -r -d '' scenario_upgrade <<'EOF' || true
cd /debs
if [ -z "$OLD_DEB" ]; then echo "SKIP: no --old-deb given"; finish; fi
check "apt install ./$OLD_DEB (old version)" apt_install "./$OLD_DEB"
old_v=$(installed_version); info "installed: $old_v"
home=$(mktemp -d)
mkdir -p "$home/.config" && echo -e "[Look&Feel]\nFilelist Icon Size=22" > "$home/.config/panerorc"
check "apt install ./$NEW_DEB upgrades the package" apt_install "./$NEW_DEB"
new_v=$(installed_version); info "installed: $new_v"
check "version changed $old_v -> $new_v and dpkg considers it newer" dpkg --compare-versions "$new_v" gt "$old_v"
check "dpkg --verify panero after upgrade" dpkg --verify panero
check "no Panero file lost in the upgrade" bash -c "dpkg -L panero | while read -r f; do [ -e \"\$f\" ] || { echo missing \$f; exit 1; }; done"
out=$(run_panero_version); echo "$out" | sed 's/^/    /'
check "panero --version after upgrade" grep -q '^panero 0\.' <<< "$out"
check "user configuration untouched by the upgrade" grep -q 'Filelist Icon Size=22' "$home/.config/panerorc"
finish
EOF

read -r -d '' scenario_remove <<'EOF' || true
cd /debs
check "apt install ./$NEW_DEB" apt_install "./$NEW_DEB"
dpkg -L panero | sort > /tmp/p.list
check "apt remove panero" bash -c "apt-get remove -y -q panero > /tmp/apt.log 2>&1"
check "status after remove is config-files" \
    test "$(dpkg-query -W -f '${Status}' panero)" = "deinstall ok config-files"
left=$(leftover_files /tmp/p.list)
echo "${left:-    (none)}" | sed 's/^\//    left after remove: \//'
check "after remove only the conffile /etc/xdg/panero_kio_isorc remains" \
    test "$left" = "/etc/xdg/panero_kio_isorc"
check "apt purge panero" bash -c "apt-get purge -y -q panero > /tmp/apt.log 2>&1"
check "package unknown to dpkg after purge" bash -c "! dpkg-query -W panero >/dev/null 2>&1"
left=$(leftover_files /tmp/p.list)
check "no file of the package remains after purge" test -z "$left"
found=$(find / -xdev \( -path /proc -o -path /debs -o -path /tmp \) -prune -o -iname '*panero*' -print 2>/dev/null)
echo "${found:-    (none)}" | sed 's/^\//    found: \//'
check "no path named *panero* left on the system after purge" test -z "$found"
finish
EOF

run_scenario() {
    local name=$1 script=$2
    echo "=== scenario: $name ($image) ==="
    docker run --rm -i \
        -e NEW_DEB="$new_name" -e OLD_DEB="$old_name" \
        -v "$stage:/debs:ro" \
        "$image" bash -c "$common"$'\n'"$script"
}

{
    echo "Panero package test: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "package: $new_name (sha256 $(sha256sum "$deb" | cut -d' ' -f1))"
    [ -n "$old_name" ] && echo "old package: $old_name"
    echo "image: ${image_digest:-$image}"
} | tee "$log"

overall=0
for s in install coexist upgrade remove; do
    if [ -n "$only" ] && [ "$only" != "$s" ]; then continue; fi
    var=scenario_$s
    set +e
    run_scenario "$s" "${!var}" 2>&1 | tee -a "$log"
    st=${PIPESTATUS[0]}
    set -e
    [ "$st" = 0 ] || overall=1
done

echo "=== summary ===" | tee -a "$log"
echo "PASS: $(grep -c '^PASS' "$log")  FAIL: $(grep -c '^FAIL' "$log")  KNOWN: $(grep -c '^KNOWN' "$log")  SKIP: $(grep -c '^SKIP' "$log")" | tee -a "$log"
grep -E '^(FAIL|KNOWN)' "$log" || true
exit "$overall"
