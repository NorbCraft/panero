#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# One-time host setup for Panero development on Zorin OS 18.1 / Ubuntu 24.04.
# Run once, by the owner, in a normal terminal:
#
#     sudo bash dev/scripts/bootstrap-host.sh
#
# What it does (all from the configured distro archives, no new repositories):
#   1. apt-get install: compiler/CMake toolchain, Qt5/KF5 development packages
#      required by Krusader v2.8.1, Debian packaging + license tooling,
#      icon export tools, headless GUI test tools, KDiff3, GitHub CLI,
#      VM helper tools.
#   2. Adds the invoking user to the `docker` group (root-equivalent; owner
#      approved 2026-11-17). Takes effect after logging out and back in.
#
# It does not change desktop settings, Krusader settings, or other repositories.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "Please run with sudo: sudo bash $0" >&2
  exit 1
fi
TARGET_USER="${SUDO_USER:-}"
if [[ -z "$TARGET_USER" || "$TARGET_USER" == root ]]; then
  echo "Run via sudo from your normal user account (SUDO_USER is not set)." >&2
  exit 1
fi

BUILD=(build-essential cmake ninja-build extra-cmake-modules gettext pkg-config
       ccache clang-format gdb)
QT_KF5=(qtbase5-dev qtbase5-dev-tools
        libkf5archive-dev libkf5bookmarks-dev libkf5codecs-dev libkf5completion-dev
        libkf5coreaddons-dev libkf5config-dev libkf5crash-dev libkf5doctools-dev libkf5iconthemes-dev
        libkf5itemviews-dev libkf5kio-dev libkf5notifications-dev libkf5parts-dev
        libkf5solid-dev libkf5textwidgets-dev libkf5wallet-dev libkf5widgetsaddons-dev
        libkf5windowsystem-dev libkf5xmlgui-dev libkf5guiaddons-dev libkf5i18n-dev
        libacl1-dev libattr1-dev zlib1g-dev)
PACKAGING=(debhelper devscripts lintian fakeroot equivs licensecheck reuse)
ICONS=(inkscape librsvg2-bin imagemagick scour)
GUITEST=(xvfb xauth xdotool x11-utils kdiff3 packagekit packagekit-tools
         kio-extras kdegraphics-thumbnailers ffmpegthumbs)
TOOLS=(gh virtinst cloud-image-utils libvirt-clients qemu-utils)

echo "==> apt-get update"
apt-get update

echo "==> Installing packages"
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  "${BUILD[@]}" "${QT_KF5[@]}" "${PACKAGING[@]}" "${ICONS[@]}" "${GUITEST[@]}" "${TOOLS[@]}"

echo "==> Adding $TARGET_USER to the docker group"
if getent group docker >/dev/null; then
  usermod -aG docker "$TARGET_USER"
else
  echo "   (no docker group found - is Docker installed from the distro? skipping)"
fi

echo
echo "Done. Next steps for $TARGET_USER:"
echo "  1. Log out and back in (or reboot) so the docker group applies,"
echo "     then restart VS Code so the agent inherits it."
echo "  2. Authenticate GitHub CLI (browser flow, no token pasting):"
echo "       gh auth login --hostname github.com --git-protocol https --web --scopes workflow"
echo "       gh auth setup-git"
