#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Reset the isolated dev home (build/dev-home) to a "returning user" state so
# first-run dialogs do not block automated screenshots/tests.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEVHOME="${PANERO_DEV_HOME:-$ROOT/build/dev-home}"
mountpoint -q "$DEVHOME/runtime/gvfs" 2>/dev/null && fusermount -u "$DEVHOME/runtime/gvfs" || true
rm -rf "$DEVHOME"; mkdir -p "$DEVHOME/config"
cat > "$DEVHOME/config/panerorc" <<CFG
First Time=false
Config Version=1

[ViewerModule]
FirstRun=false
CFG
echo "seeded $DEVHOME"
