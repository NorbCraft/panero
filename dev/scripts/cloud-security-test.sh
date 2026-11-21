#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs Panero's cloud security/integration tests (autotests/panerocloudsecuritytest)
# in complete isolation: a temporary HOME and XDG folders, a private D-Bus
# session (dbus-run-session) and a private gnome-keyring unlocked with a test
# password. The desktop keyring, configuration and files of the person running
# it are never touched.
#
# Usage: dev/scripts/cloud-security-test.sh [build-dir]   (default: build)
# Needs: dbus-run-session (dbus-daemon), gnome-keyring, fuse3 for the mount part,
#        a build configured with -DPANERO_RCLONE_BINARY.
set -euo pipefail
repo_root=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
build=${1:-$repo_root/build}
build=$(cd "$build" && pwd)
test_bin="$build/bin/panerocloudsecuritytest"
[ -x "$test_bin" ] || { echo "not built: $test_bin" >&2; exit 2; }
[ -x "$build/bin/panero-rclone" ] || { echo "no bundled rclone in $build/bin (configure with -DPANERO_RCLONE_BINARY)" >&2; exit 2; }
for tool in dbus-run-session gnome-keyring-daemon; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 2; }
done

tmp=$(mktemp -d)
cleanup() {
    local status=$?
    # the test unmounts on exit; make sure nothing stays mounted below $tmp
    { grep -o " $tmp[^ ]*" /proc/self/mountinfo 2>/dev/null || true; } | while read -r m; do fusermount3 -uz "$m" 2>/dev/null || true; done
    rm -rf "$tmp" || true
    exit "$status"
}
trap cleanup EXIT
mkdir -p "$tmp/home" "$tmp/run"
chmod 700 "$tmp/run"

env -i \
    PATH="/usr/local/bin:/usr/bin:/bin" \
    HOME="$tmp/home" \
    XDG_CONFIG_HOME="$tmp/home/.config" \
    XDG_DATA_HOME="$tmp/home/.local/share" \
    XDG_CACHE_HOME="$tmp/home/.cache" \
    XDG_RUNTIME_DIR="$tmp/run" \
    LANG=C.UTF-8 \
    QT_QPA_PLATFORM=offscreen \
    TEST_BIN="$test_bin" \
    PANERO_CLOUD_DIALOG_SHOTS="${PANERO_CLOUD_DIALOG_SHOTS:-}" \
    dbus-run-session -- bash -c '
        set -euo pipefail
        # a login keyring with a test password (files below the temporary HOME)
        printf "%s" "panero-test-keyring" | gnome-keyring-daemon --unlock --components=secrets >/dev/null
        gnome-keyring-daemon --start --components=secrets >/dev/null
        export PANERO_TEST_PRIVATE_KEYRING=1
        "$TEST_BIN" "$@"
    ' bash "${@:2}"
