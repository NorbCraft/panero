#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Reset the isolated dev home (build/dev-home) to a "returning user" state so
# first-run dialogs do not block automated screenshots/tests.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEVHOME="${PANERO_DEV_HOME:-$ROOT/build/dev-home}"
# headless runs may start gvfs / xdg-document-portal on the private bus: unmount their
# FUSE mounts below the dev home and stop leftover helpers before deleting it
for m in $(awk -v d="$DEVHOME" '$2 ~ "^"d {print $2}' /proc/mounts | sort -r); do
  fusermount -u "$m" 2>/dev/null || fusermount -uz "$m" 2>/dev/null || true
done
pkill -f "XDG_RUNTIME_DIR=$DEVHOME" 2>/dev/null || true
rm -rf "$DEVHOME"; mkdir -p "$DEVHOME/config"
cat > "$DEVHOME/config/panerorc" <<CFG
First Time=false
Config Version=1

[ViewerModule]
FirstRun=false
CFG
echo "seeded $DEVHOME"
