#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Xvfb has no window manager: keyboard focus follows the pointer and new dialogs do
# not get focus. Give the topmost viewable top-level window the input focus and put
# the pointer on it, so following xdotool key presses reach the newest dialog.
# Optional argument: only consider windows whose title contains this text.
WANT="${1:-}" python3 -I - <<'PY'
import os, re, subprocess
want = os.environ.get("WANT", "").lower()
out = subprocess.run(["xwininfo", "-root", "-children", "-int"], capture_output=True, text=True).stdout
ids = [int(m.group(1)) for m in re.finditer(r"^\s+(\d+) ", out, re.M)]
top = None
for wid in ids:  # xwininfo lists the topmost window first
    info = subprocess.run(["xwininfo", "-id", str(wid)], capture_output=True, text=True).stdout
    if "IsViewable" not in info:
        continue
    title = re.search(r'"([^"]*)"', info)
    if want and (not title or want not in title.group(1).lower()):
        continue
    g = {k: int(v) for k, v in re.findall(r"(Absolute upper-left [XY]|Width|Height):\s+(-?\d+)", info)}
    if g.get("Width", 0) < 60 or g.get("Height", 0) < 40:
        continue
    top = (wid, g)
    break
if top:
    wid, g = top
    cx = g["Absolute upper-left X"] + g["Width"] // 2
    cy = g["Absolute upper-left Y"] + min(g["Height"] // 2, 60)
    subprocess.run(["xdotool", "mousemove", str(cx), str(cy)])
    subprocess.run(["xdotool", "windowfocus", str(wid)], stderr=subprocess.DEVNULL)
PY
