#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build, stage-install and run Panero in an isolated environment.
# Never touches the real ~/.config or ~/.local/share: HOME and all XDG dirs
# point into build/dev-home (or $PANERO_DEV_HOME).
#
#   dev/scripts/run-dev.sh            # run on the current desktop (Wayland/X11)
#   dev/scripts/run-dev.sh --xvfb     # run headless under Xvfb (for screenshots)
#   dev/scripts/run-dev.sh --no-build -- --left=/tmp   # extra args after --
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build"
STAGE="$BUILD/stage"
DEVHOME="${PANERO_DEV_HOME:-$BUILD/dev-home}"
XVFB=0; DOBUILD=1; ARGS=(); SHOT=""; DELAY=6
while [[ $# -gt 0 ]]; do
  case "$1" in
    --xvfb) XVFB=1;; --no-build) DOBUILD=0;;
    --screenshot) SHOT="$2"; XVFB=1; shift;;   # write PNG of the Xvfb screen and quit
    --delay) DELAY="$2"; shift;;
    --) shift; ARGS=("$@"); break;;
    *) echo "unknown option $1" >&2; exit 2;;
  esac; shift
done
if [[ $DOBUILD == 1 ]]; then
  [[ -f "$BUILD/build.ninja" ]] || cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
  ninja -C "$BUILD" >/dev/null
  rm -rf "$STAGE"; DESTDIR="$STAGE" ninja -C "$BUILD" install >/dev/null
fi
mkdir -p "$DEVHOME"/{config,data,cache,runtime}; chmod 700 "$DEVHOME/runtime"
LIBDIR=$(cd "$STAGE/usr/lib" && ls -d */panero 2>/dev/null | head -1)
export HOME="$DEVHOME" XDG_CONFIG_HOME="$DEVHOME/config" XDG_DATA_HOME="$DEVHOME/data" \
       XDG_CACHE_HOME="$DEVHOME/cache" \
       XDG_DATA_DIRS="$STAGE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" \
       PANERO_PLUGIN_DIR="$STAGE/usr/lib/$LIBDIR/plugins" \
       QT_PLUGIN_PATH="$STAGE/usr/lib/$LIBDIR/plugins"
CMD=("$STAGE/usr/bin/panero" "${ARGS[@]}")
if [[ -n "$SHOT" ]]; then
  unset WAYLAND_DISPLAY; export QT_QPA_PLATFORM=xcb XDG_RUNTIME_DIR="$DEVHOME/runtime" GIO_USE_VFS=local
  DISP=":$((90 + RANDOM % 9))"
  Xvfb "$DISP" -screen 0 "${PANERO_XVFB_SIZE:-1400x900x24}" -nolisten tcp >/dev/null 2>&1 & XPID=$!
  sleep 1; export DISPLAY="$DISP"
  dbus-run-session -- "${CMD[@]}" >"$DEVHOME/last-run.log" 2>&1 & APID=$!
  sleep "$DELAY"
  import -window root "$SHOT"
  kill "$APID" 2>/dev/null || true; sleep 1; kill "$XPID" 2>/dev/null || true
  echo "screenshot: $SHOT (log: $DEVHOME/last-run.log)"
  exit 0
elif [[ $XVFB == 1 ]]; then
  unset WAYLAND_DISPLAY; export QT_QPA_PLATFORM=xcb XDG_RUNTIME_DIR="$DEVHOME/runtime" GIO_USE_VFS=local
  exec xvfb-run -a -s "-screen 0 ${PANERO_XVFB_SIZE:-1400x900x24}" dbus-run-session -- "${CMD[@]}"
else
  # keep the desktop's display/session bus, but a private D-Bus session avoids
  # clashing with a running Panero/Krusader instance
  exec dbus-run-session -- "${CMD[@]}"
fi
