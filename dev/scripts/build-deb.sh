#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the Panero .deb in a clean ubuntu:24.04 container (the base of Zorin
# OS 18.1) with `dpkg-buildpackage -us -uc -b`, which also runs the autotests
# (ctest via dh_auto_test), then run lintian. Results go to dist/.
#
# Usage: dev/scripts/build-deb.sh [options]
#   --ref <git-ref>      build this commit (default: HEAD) via `git archive`
#   --worktree           build the working tree instead (tracked + untracked,
#                        non-ignored files), for local iterations
#   --version <debver>   override the Debian version of the top changelog
#                        entry (test builds only, e.g. 0.1.0~preview.0-1)
#   --out <dir>          output directory (default: dist)
#   --no-lintian         skip lintian
#   --nocheck            skip the autotests (DEB_BUILD_OPTIONS=nocheck)
#
# Environment:
#   PANERO_BUILD_IMAGE   container image (default: ubuntu:24.04; may be pinned
#                        by digest, e.g. ubuntu:24.04@sha256:...)
#   PANERO_APT_CACHE     optional host directory used as the container's apt
#                        package cache, to speed up repeated builds
#
# Docker access: uses `docker` directly when the socket is accessible; if the
# user is in the docker group but this session does not have it yet, the
# script re-runs itself under `sg docker`.
set -euo pipefail

if ! docker info >/dev/null 2>&1 && [ -z "${PANERO_SG_REEXEC:-}" ] \
        && id -nG "$(id -un)" 2>/dev/null | grep -qw docker; then
    export PANERO_SG_REEXEC=1
    exec sg docker -c "$(printf '%q ' "$0" "$@")"
fi

repo_root=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
ref=HEAD
use_worktree=0
deb_version=
out_dir=dist
run_lintian=1
nocheck=0
image=${PANERO_BUILD_IMAGE:-ubuntu:24.04}

while [ $# -gt 0 ]; do
    case "$1" in
        --ref) ref=$2; shift 2 ;;
        --worktree) use_worktree=1; shift ;;
        --version) deb_version=$2; shift 2 ;;
        --out) out_dir=$2; shift 2 ;;
        --no-lintian) run_lintian=0; shift ;;
        --nocheck) nocheck=1; shift ;;
        -h|--help) sed -n '5,/^set -euo/p' "$0" | sed '/^set -euo/d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

cd "$repo_root"
mkdir -p "$out_dir"
out_abs=$(cd "$out_dir" && pwd)

if [ "$use_worktree" = 1 ]; then
    src_desc="working tree of $(git rev-parse HEAD)"
    git diff --quiet HEAD -- || src_desc="$src_desc (with uncommitted changes)"
    make_tar() {
        git ls-files -z --cached --others --exclude-standard \
            | tar --null --no-recursion --ignore-failed-read -T - -cf -
    }
else
    commit=$(git rev-parse --verify "$ref^{commit}")
    src_desc="commit $commit"
    make_tar() { git archive --format=tar "$commit"; }
fi

echo "==> Building Panero .deb from $src_desc in $image"
docker pull -q "$image" >/dev/null
image_digest=$(docker image inspect --format '{{join .RepoDigests " "}}' "$image" 2>/dev/null || true)
echo "==> Image: ${image_digest:-$image}"

cache_args=()
if [ -n "${PANERO_APT_CACHE:-}" ]; then
    mkdir -p "$PANERO_APT_CACHE"
    cache_args=(-v "$(cd "$PANERO_APT_CACHE" && pwd):/var/cache/apt/archives")
fi

# Runs inside the container: install the build dependencies declared in
# debian/control, build (incl. ctest), lint, copy the results to /out.
read -r -d '' container_script <<'EOF' || true
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
rm -f /etc/apt/apt.conf.d/docker-clean
apt-get update -qq
apt-get install -y -qq --no-install-recommends dpkg-dev build-essential >/dev/null
mkdir -p /build/panero
tar -x -C /build/panero
cd /build/panero
if [ -n "$DEB_VERSION_OVERRIDE" ]; then
    sed -i "1s/^panero ([^)]*)/panero ($DEB_VERSION_OVERRIDE)/" debian/changelog
fi
echo "==> Package version: $(dpkg-parsechangelog -SVersion)"
apt-get build-dep -y -qq --no-install-recommends ./ >/dev/null
echo "==> Build environment"
head -n 4 /etc/os-release
gcc --version | head -n 1
cmake --version | head -n 1
dpkg-query -W debhelper qtbase5-dev libkf5kio-dev extra-cmake-modules
if [ "$NOCHECK" = 1 ]; then export DEB_BUILD_OPTIONS=nocheck; fi
dpkg-buildpackage -us -uc -b
cd /build
cp -v ./*.deb ./*.buildinfo ./*.changes /out/
cp -v ./*.ddeb /out/ 2>/dev/null || true
if [ "$RUN_LINTIAN" = 1 ]; then
    apt-get install -y -qq --no-install-recommends lintian >/dev/null
    echo "==> $(lintian --version)"
    chmod -R a+rX /build
    runuser -u nobody -- env HOME=/tmp lintian --display-info --display-experimental \
        --pedantic --show-overrides /build/*.changes 2>&1 | tee /out/lintian.txt || true
fi
chown -R "$HOST_UID:$HOST_GID" /out
EOF

set +e
make_tar | docker run --rm -i \
    -e DEB_VERSION_OVERRIDE="$deb_version" \
    -e NOCHECK="$nocheck" \
    -e RUN_LINTIAN="$run_lintian" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -v "$out_abs:/out" \
    "${cache_args[@]}" \
    "$image" bash -c "$container_script" 2>&1 | tee "$out_abs/build.log"
status=${PIPESTATUS[1]}
set -e

{
    echo "source: $src_desc"
    echo "image: ${image_digest:-$image}"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$out_abs/BUILD-INFO.txt"

if [ "$status" != 0 ]; then
    echo "==> Build FAILED (exit $status), see $out_abs/build.log" >&2
    exit "$status"
fi
echo "==> Done. Results in $out_abs:"
ls -l "$out_abs"
