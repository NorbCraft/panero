#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Print "X=.. Y=.." (absolute top-left) of the largest window whose name contains
# "Panero" (the main window), for xdotool scripts:  eval $(dev/scripts/xwin.sh)
# Never interactive: prints X=0 Y=0 if nothing is found.
xwininfo -root -tree 2>/dev/null | python3 -I -c '
import re, sys
best = None
for line in sys.stdin:
    if "panero" not in line.lower():
        continue
    m = re.search(r"(\d+)x(\d+)\+-?\d+\+-?\d+\s+\+(-?\d+)\+(-?\d+)", line)
    if m:
        w, h, x, y = map(int, m.groups())
        if best is None or w * h > best[0]:
            best = (w * h, x, y)
print("X=%d Y=%d" % ((best[1], best[2]) if best else (0, 0)))
'
