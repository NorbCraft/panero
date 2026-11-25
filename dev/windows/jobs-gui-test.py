#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Windows GUI checks of the preview 13 file operations (D-022), on disposable files:

- marks are cleared after F5 (F8 afterwards takes only the file under the cursor);
- F8 moves to the Recycle Bin, Ctrl+Z restores exactly that item ($I record gone);
- a long copy opens the progress window, Esc keeps it in the background, the jobs
  button with the progress ring is on the toolbar, the copy completes.

On the developer VM through vm-gui-test.ps1 (D-022):
  dev/windows/vm.sh gui <name> dev/windows/vm-gui-test.ps1 -Script jobs-gui-test.py
With --vm only the Recycle Bin items this test made (original path inside --work)
are removed again at the end.
"""
import argparse
import atexit
import json
import os
import sys
import shutil
import subprocess
import time
from pathlib import Path

import win32con
import win32gui
from PIL import ImageGrab
from pywinauto import Application, Desktop, keyboard
# VM desktop: input only while Panero is in front (fgguard.py)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fgguard  # noqa: E402
fgguard.install()

ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
ap.add_argument('--vm', action='store_true')
ap.add_argument('--work', default=r'C:\pt\work')
ap.add_argument('--files', type=int, default=6000)
args = ap.parse_args()
OUT = Path(args.out)
OUT.mkdir(parents=True, exist_ok=True)
EXE = os.environ['PANERO_EXE']
WORK = Path(args.work).resolve()
LEFT, RIGHT = WORK / 'left', WORK / 'right'
results = []
shot_no = [0]


def shot(name):
    shot_no[0] += 1
    ImageGrab.grab().save(OUT / f'jobs-{shot_no[0]:02d}-{name}.png')


def record(check, ok, note='', must=True):
    status = 'PASS' if ok else ('FAIL' if must else 'NOTE')
    results.append({'check': check, 'status': status, 'note': note})
    print(f'{status}: {check} {("- " + note) if note else ""}', flush=True)


def write_results():
    with open(OUT / 'jobs-results.json', 'w') as f:
        json.dump(results, f, indent=1)


atexit.register(write_results)


def wait_for(cond, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if cond():
                return True
        except OSError:
            pass
        time.sleep(0.3)
    return False


def keys(seq, pause=0.6):
    keyboard.send_keys(seq, pause=0.05, with_spaces=True)
    time.sleep(pause)


def recycled_from_work():
    """($I record, original path) of the Recycle Bin items whose original path lies in WORK."""
    import win32api
    import win32security
    sid = win32security.ConvertSidToStringSid(win32security.GetTokenInformation(
        win32security.OpenProcessToken(win32api.GetCurrentProcess(), win32con.TOKEN_QUERY),
        win32security.TokenUser)[0])
    folder = Path(WORK.drive + '\\') / '$Recycle.Bin' / sid
    work = str(WORK).lower().rstrip('\\') + '\\'
    out = []
    try:
        infos = [p for p in folder.iterdir() if p.name.startswith('$I')]
    except OSError:
        return out
    for info in infos:
        try:
            data = info.read_bytes()
            n = int.from_bytes(data[24:28], 'little')
            orig = data[28:28 + 2 * n].decode('utf-16-le').rstrip('\0')
        except (OSError, UnicodeDecodeError):
            continue
        if orig.lower().startswith(work):
            out.append((info, orig))
    return out


def purge_own_recycled():
    for info, orig in recycled_from_work():
        data = info.with_name('$R' + info.name[2:])
        try:
            if data.is_dir():
                shutil.rmtree(data)
            else:
                data.unlink(missing_ok=True)
            info.unlink()
            print('removed from the Recycle Bin (made by this test):', orig, flush=True)
        except OSError as e:
            print('could not purge', orig, e)


if args.vm:
    atexit.register(purge_own_recycled)
    purge_own_recycled()

# --- disposable files ----------------------------------------------------------------
shutil.rmtree(WORK, ignore_errors=True)
(LEFT / 'many').mkdir(parents=True)
RIGHT.mkdir(parents=True)
for n in ('a.txt', 'b.txt', 'c.txt', 'd.txt'):
    (LEFT / n).write_text(f'Panero jobs test {n}\n')
blob = os.urandom(4096)
for i in range(args.files):
    (LEFT / 'many' / f'f{i:05d}.dat').write_bytes(blob)
ORDER = ['..', 'many', 'a.txt', 'b.txt', 'c.txt', 'd.txt']

env = dict(os.environ)
env['PATH'] = os.environ.get('PANERO_CLEAN_PATH', env['PATH'])
log = open(OUT / 'stderr-jobs.txt', 'w')
proc = subprocess.Popen([EXE, '--left', str(LEFT), '--right', str(RIGHT)], env=env, stdout=log, stderr=log,
                        cwd=str(WORK))
atexit.register(lambda: proc.poll() is None and proc.kill())


def windows_of(pid):
    out = []
    for w in Desktop(backend='uia').windows():
        try:
            if w.process_id() == pid and w.is_visible() and w.window_text():
                out.append(w)
                for c in w.children(control_type='Window'):
                    if c.handle and c.is_visible() and c.window_text():
                        out.append(c)
        except Exception:  # noqa: BLE001
            pass
    return out


def titles():
    return [w.window_text() for w in windows_of(proc.pid)]


win = None
for _ in range(60):
    time.sleep(1)
    mains = [w for w in windows_of(proc.pid)
             if win32gui.GetWindow(w.handle, win32con.GW_OWNER) == 0 and w.rectangle().width() > 500]
    if mains:
        app = Application(backend='uia').connect(process=proc.pid)
        win = app.window(handle=mains[0].handle)
        break
record('Panero starts with the work folders', win is not None, str(titles()))
if win is None:
    raise SystemExit(1)
time.sleep(2)
win.set_focus()
r = win.rectangle()


def go_to(name):
    keys('{HOME}', 0.3)
    for _ in range(ORDER.index(name)):
        keys('{DOWN}', 0.15)


def press_button(label):
    for w in windows_of(proc.pid):
        try:
            for b in w.descendants(control_type='Button'):
                if b.window_text().replace('&', '') == label:
                    b.click_input()
                    time.sleep(0.6)
                    return 'clicked'
        except Exception:  # noqa: BLE001
            pass
    keys('{ENTER}', 0.6)
    return 'Enter'


# 1. marks cleared after F5: mark a + b, copy, then F8 takes only the cursor file (c.txt)
go_to('a.txt')
keys('{INSERT}', 0.4)
keys('{INSERT}', 0.4)
keys('{F5}', 1.5)
keys('{ENTER}', 0.5)
ok = wait_for(lambda: (RIGHT / 'a.txt').exists() and (RIGHT / 'b.txt').exists())
record('F5 copies the two marked files', ok)
shot('after-f5')
keys('{F8}', 1.5)
how = press_button('Move to Recycle Bin')
gone = wait_for(lambda: not (LEFT / 'c.txt').exists())
record('Marks cleared after F5: F8 moved only the cursor file to the Recycle Bin',
       gone and (LEFT / 'a.txt').exists() and (LEFT / 'b.txt').exists(), f'button {how}')
binned = [o for _, o in recycled_from_work() if o.lower().endswith('\\c.txt')]
record('c.txt is in the Recycle Bin', bool(binned), str(binned))

# 2. Ctrl+Z restores exactly that item
win.set_focus()
keys('^z', 1.0)
back = wait_for(lambda: (LEFT / 'c.txt').exists(), 8)
still = [o for _, o in recycled_from_work() if o.lower().endswith('\\c.txt')]
record('Ctrl+Z restores c.txt from the Recycle Bin', back, '')
record('The restored item left the Recycle Bin ($I record removed)', back and not still, str(still))
shot('after-undo')

# 3. long copy: progress window, Esc keeps it in the background, jobs button, completion
win.set_focus()
go_to('many')
keys('{F5}', 1.5)
keys('{ENTER}', 0.2)
shown = wait_for(lambda: any('Copying many' in t for t in titles()), 8)
record('A long copy opens the progress window', shown, str(titles()))
shot('progress-window')
for w in windows_of(proc.pid):
    if 'Copying many' in w.window_text():
        try:
            w.set_focus()
            keys('{ESC}', 0.8)
        except Exception as e:  # noqa: BLE001
            print('focus failed', e)
hidden = not any('Copying many' in t for t in titles())
copied = len(list((RIGHT / 'many').glob('*.dat'))) if (RIGHT / 'many').exists() else 0
record('Esc keeps the copy in the background (window hidden, copy goes on)', hidden and copied < args.files,
       f'{copied} of {args.files} copied')
shot('jobs-button')
ImageGrab.grab(bbox=(r.right - 360, r.top, r.right, r.top + 90)).resize((1080, 270)).save(OUT / 'jobs-button-zoom.png')
done = wait_for(lambda: len(list((RIGHT / 'many').glob('*.dat'))) >= args.files, 600)
record('The background copy completes', done)
time.sleep(1.5)
shot('notice-after-background-copy')
record('Notice when the background copy ended (tray message, see screenshot)', True, must=False)

proc.kill()
time.sleep(1)
shutil.rmtree(WORK, ignore_errors=True)
failed = [x for x in results if x['status'] == 'FAIL']
print(f'RESULT: {len(results) - len(failed)} passed, {len(failed)} failed', flush=True)
raise SystemExit(1 if failed else 0)
