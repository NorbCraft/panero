#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build and test Panero in a Windows VM over SSH (D-022). The VM has MSYS2
# UCRT64 with the same packages as .github/workflows/windows.yml and the
# pinned KF5 (dev/windows/install-kf5.sh). Connection details live outside the
# repository in ~/.config/panero-dev/vm.env:
#   PANERO_VM_HOST=...  PANERO_VM_USER=...  PANERO_VM_KEY=...  PANERO_VM_ROOT=/c/Users/<user>/panero-dev
#
# Usage (from the repository root on the Linux host):
#   dev/windows/vm.sh sync  <name> [ref]   clone/update $ROOT/<name> to origin/<ref> (default: the
#                                          current branch, which must be pushed) and apply the local
#                                          uncommitted changes, new files included, on top
#   dev/windows/vm.sh build <name>         cmake (first time) + ninja + ctest
#   dev/windows/vm.sh stage <name>         ninja install into <name>/stage + dev/windows/package.sh (portable folder)
#   dev/windows/vm.sh run   <name> <cmd>   any bash command in MSYS2 UCRT64, in $ROOT/<name>
#   dev/windows/vm.sh gui   <name> <ps1> [args]
#                                          run a PowerShell script ON THE VM DESKTOP (interactive
#                                          session, needed for windows, screenshots and input) and
#                                          wait for it; one GUI run at a time (lock). Output in
#                                          $ROOT/<name>/gui-run.log
#   dev/windows/vm.sh get   <remote-path> <local-path>   copy a file from the VM (scp)
#
# Rules: never delete anything in the VM outside $PANERO_VM_ROOT; OneDrive only
# inside a "test-panero" folder at its top, and only files you put there.
set -euo pipefail
conf="${PANERO_VM_CONF:-$HOME/.config/panero-dev/vm.env}"
[ -f "$conf" ] || { echo "missing $conf" >&2; exit 2; }
# shellcheck disable=SC1090
source "$conf"
: "${PANERO_VM_HOST:?}" "${PANERO_VM_USER:?}" "${PANERO_VM_KEY:?}" "${PANERO_VM_ROOT:?}"
ssh_opts=(-i "$PANERO_VM_KEY" -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=30 -o ServerAliveCountMax=20)
target="$PANERO_VM_USER@$PANERO_VM_HOST"
winroot=$(echo "$PANERO_VM_ROOT" | sed -E 's|^/([a-z])/|\U\1:/|')

vmbash() { # run a bash script (stdin) in MSYS2 UCRT64
    ssh "${ssh_opts[@]}" "$target" 'set MSYSTEM=UCRT64&& set CHERE_INVOKING=1&& C:\msys64\usr\bin\bash.exe -l -s'
}

cmd="${1:-}"; shift || true
case "$cmd" in
sync)
    name="$1"; ref="${2:-$(git rev-parse --abbrev-ref HEAD)}"
    # tracked and new (not ignored) files, through a throw-away index
    tmpidx=$(mktemp); cp "$(git rev-parse --git-dir)/index" "$tmpidx"
    diff=$(GIT_INDEX_FILE="$tmpidx" bash -c 'git add -A -- . ":!docs/windows preview build response" && git diff --cached HEAD --binary')
    rm -f "$tmpidx"
    { cat <<EOF
set -euo pipefail
mkdir -p "$PANERO_VM_ROOT"; cd "$PANERO_VM_ROOT"
[ -d "$name/.git" ] || git clone -q https://github.com/NorbCraft/panero.git "$name"
cd "$name"
git fetch -q origin
git checkout -q -f -B "panero-vm" "origin/$ref"
git clean -qfd -e build -e stage -e dist -e results
cat > /tmp/panero-vm.diff <<'PANERO_DIFF_END'
EOF
      printf '%s\n' "$diff"
      cat <<EOF
PANERO_DIFF_END
if [ -s /tmp/panero-vm.diff ] && [ "\$(wc -c < /tmp/panero-vm.diff)" -gt 1 ]; then git apply --whitespace=nowarn /tmp/panero-vm.diff && echo "applied local changes"; fi
git log --oneline -1
EOF
    } | vmbash
    ;;
build)
    name="$1"
    vmbash <<EOF
set -euo pipefail
cd "$PANERO_VM_ROOT/$name"
[ -f build/build.ninja ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX="\$PWD/stage" >/dev/null
ninja -C build -k 0 2>&1 | grep -E 'error|FAILED|warning: unused|^ninja: ' | tail -40
cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure 2>&1 | tail -15
EOF
    ;;
stage)
    name="$1"
    vmbash <<EOF
set -euo pipefail
cd "$PANERO_VM_ROOT/$name"
rm -rf stage dist
ninja -C build install >/dev/null
dev/windows/package.sh build stage dist 2>&1 | tail -3
ls dist
EOF
    ;;
run)
    name="$1"; shift
    { echo "set -eo pipefail; cd \"$PANERO_VM_ROOT/$name\""; echo "$*"; } | vmbash
    ;;
gui)
    name="$1"; script="$2"; shift 2
    dir="$winroot/$name"
    args="$*"
    # a scheduled task with /it runs in the logged-in desktop session; it is
    # started hidden through PowerShell and writes its output to gui-run.log
    vmbash <<EOF
set -uo pipefail
lock="$PANERO_VM_ROOT/gui.lock"
for i in \$(seq 1 360); do mkdir "\$lock" 2>/dev/null && break; sleep 5; done
[ -d "\$lock" ] || { echo "GUI lock busy" >&2; exit 3; }
trap 'rmdir "\$lock"' EXIT
task="PaneroGui-$name"
done_file="$PANERO_VM_ROOT/$name/gui-run.done"; rm -f "\$done_file" "$PANERO_VM_ROOT/$name/gui-run.log"
cat > "$PANERO_VM_ROOT/$name/gui-run.cmd" <<CMD
@echo off
cd /d "$dir"
powershell -NoProfile -ExecutionPolicy Bypass -File "$script" $args > "$dir/gui-run.log" 2>&1
echo %ERRORLEVEL% > "$dir/gui-run.done"
CMD
cat > "$PANERO_VM_ROOT/$name/gui-run.vbs" <<VBS
CreateObject("WScript.Shell").Run "cmd /c ""$dir/gui-run.cmd""", 0, True
VBS
schtasks //create //f //tn "\$task" //sc once //st 23:59 //it //tr "wscript.exe \"$dir/gui-run.vbs\"" >/dev/null
schtasks //run //tn "\$task" >/dev/null
for i in \$(seq 1 720); do [ -f "\$done_file" ] && break; sleep 5; done
schtasks //delete //f //tn "\$task" >/dev/null
cat "$PANERO_VM_ROOT/$name/gui-run.log" 2>/dev/null | tail -60
[ -f "\$done_file" ] || { echo "GUI run timed out" >&2; exit 4; }
exit \$(tr -d ' \r\n' < "\$done_file")
EOF
    ;;
get)
    scp -q "${ssh_opts[@]}" -r "$target:$1" "$2"
    ;;
*)
    sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//' | head -28
    exit 2
    ;;
esac
