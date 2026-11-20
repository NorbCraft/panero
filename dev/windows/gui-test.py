# SPDX-License-Identifier: GPL-2.0-or-later
"""First-run GUI checks of the Windows build from a Total Commander user's view.

Runs on a disposable GitHub Windows runner against disposable files in
C:\\pt\\work. Drives Panero with the keyboard (and the mouse where UI Automation
exposes the file rows), checks every result on disk, and takes a screenshot
after each step. Results go to <out>/gui-results.md and gui-results.json.

Core file operations (copy, move, rename, new folder, delete to Recycle Bin)
are "must" checks: the script exits 1 if one fails. The other checks record how
familiar the behaviour is for a Total Commander user and never fail the job.
"""
import argparse
import atexit
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import psutil
import win32con
import win32gui
from PIL import ImageGrab
from pywinauto import Application, Desktop, keyboard, mouse

sys.stdout.reconfigure(encoding='utf-8', errors='replace')  # the CI console is cp1252
ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
args = ap.parse_args()
OUT = Path(args.out)
OUT.mkdir(parents=True, exist_ok=True)

EXE = os.environ['PANERO_EXE']
WORK = Path(r'C:\pt\work')
LEFT, RIGHT = WORK / 'left', WORK / 'right'
results = []
shot_no = [20]


def shot(name):
    shot_no[0] += 1
    ImageGrab.grab().save(OUT / f'{shot_no[0]:02d}-{name}.png')


def record(check, ok, note='', must=False):
    status = 'PASS' if ok else ('FAIL' if must else 'NOTE')
    results.append({'check': check, 'status': status, 'must': must, 'note': note})
    print(f'{status}: {check} {("- " + note) if note else ""}', flush=True)


def write_results():
    with open(OUT / 'gui-results.json', 'w') as f:
        json.dump(results, f, indent=1)
    with open(OUT / 'gui-results.md', 'w', encoding='utf-8') as f:
        f.write('| Check | Result | Note |\n|---|---|---|\n')
        for r in results:
            f.write(f"| {r['check']} | {r['status']} | {r['note']} |\n")
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as f:
            f.write('## Panero Windows GUI checks\n\n')
            f.write(open(OUT / 'gui-results.md', encoding='utf-8').read())


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


def recycle_bin_names():
    ps = ('$s = New-Object -ComObject Shell.Application; '
          '$s.Namespace(10).Items() | ForEach-Object { $_.Name }')
    out = subprocess.run(['powershell', '-NoProfile', '-Command', ps],
                         capture_output=True, text=True).stdout
    return [l.strip() for l in out.splitlines() if l.strip()]


# --- disposable files -------------------------------------------------------
shutil.rmtree(WORK, ignore_errors=True)
(LEFT / 'docs').mkdir(parents=True)
RIGHT.mkdir(parents=True)
for n in ('alpha.txt', 'beta.txt', 'gamma.txt', 'delta.txt', 'omega.txt'):
    (LEFT / n).write_text(f'Panero test file {n}\n')
(LEFT / 'docs' / 'inside.txt').write_text('inside\n')
# Sorted the way Panero shows them: "..", folders, then files by name
ORDER = ['..', 'docs', 'alpha.txt', 'beta.txt', 'delta.txt', 'gamma.txt', 'omega.txt']

env = dict(os.environ)
env['PATH'] = os.environ['PANERO_CLEAN_PATH']
env['QT_FORCE_STDERR_LOGGING'] = '1'
APP_DIR = str(Path(EXE).parent).lower()
WINDIR = os.environ.get('SystemRoot', r'C:\Windows').lower()


def start(arguments, logname):
    log = open(OUT / f'stderr-{logname}.txt', 'w')
    return subprocess.Popen([EXE] + arguments, env=env, stdout=log, stderr=log, cwd=str(WORK))


def windows_of(pid):
    out = []
    for w in Desktop(backend='uia').windows():
        try:
            if w.process_id() == pid and w.is_visible() and w.window_text():
                out.append(w)
        except Exception:  # window vanished
            pass
    return out


DIALOG_TITLES = ('Information', 'Search results', 'Konfigurator', 'Missing Plugin', 'Warning', 'Error')


def is_main(w):
    """The main window is the large top-level window without an owner (dialogs have one).
    UI Automation reports the bare title ("Users"); the title bar shows "Users - Panero"."""
    t = w.window_text()
    r = w.rectangle()
    return win32gui.GetWindow(w.handle, win32con.GW_OWNER) == 0 and not t.startswith(DIALOG_TITLES) \
        and r.width() > 500 and r.height() > 350


def module_audit(proc, label):
    """Every DLL/EXE mapped by Panero and its child processes (kioslave5, kiod5)."""
    bad, seen = [], []
    try:
        procs = [psutil.Process(proc.pid)]
        procs += procs[0].children(recursive=True)
    except psutil.Error as e:
        record(f'Module audit ({label})', False, str(e), must=True)
        return
    for p in procs:
        try:
            for m in p.memory_maps():
                path = m.path.lower()
                if not path.endswith(('.dll', '.exe')):
                    continue
                seen.append(f'{p.name()}\t{m.path}')
                if not (path.startswith(APP_DIR) or path.startswith(WINDIR)):
                    bad.append(f'{p.name()}: {m.path}')
        except psutil.Error:
            pass
    with open(OUT / f'loaded-modules-{label}.txt', 'w') as f:
        f.write('\n'.join(sorted(set(seen))))
    names = sorted({p.name() for p in procs if p.is_running()})
    record(f'Every loaded module comes from the ZIP folder or Windows ({label})', not bad,
           f'processes: {", ".join(names)}; ' + ('; '.join(bad[:8]) if bad else f'{len(set(seen))} modules'),
           must=True)


# --- phase A: first run, as a new user would start it (double-click, no arguments) ----
proc = start([], 'first-run')
seen_titles, main = [], None
deadline = time.time() + 120
while time.time() < deadline and proc.poll() is None:
    time.sleep(1)
    wins = windows_of(proc.pid)
    for w in wins:
        if w.window_text() not in seen_titles:
            seen_titles.append(w.window_text())
            time.sleep(1)
            shot('first-run-' + ''.join(c for c in w.window_text() if c.isalnum())[:30])
    mains = [w for w in wins if is_main(w)]
    dialogs = [w for w in wins if not is_main(w)]
    if dialogs:
        d = dialogs[0]
        d.set_focus()
        # Welcome box: OK. Konfigurator (opened by the first run): close it, keep defaults.
        if d.window_text().startswith('Konfigurator'):
            d.close()
        else:
            keys('{ENTER}', 1.0)
        continue
    if mains:
        main = mains[0]
        break
if main is None:
    record('First run reaches the main window', False,
           f'exit code {proc.poll()}; windows seen: {seen_titles}', must=True)
    proc.kill()
    sys.exit(1)
time.sleep(3)
main.set_focus()
shot('first-run-main-window')
record('First run reaches the main window', True, 'windows on the way: ' + ' → '.join(seen_titles))
r = main.rectangle()
record('Main window size on first run', True, f'{r.width()}×{r.height()} on a {ImageGrab.grab().size[0]}×'
       f'{ImageGrab.grab().size[1]} screen; title "{main.window_text()}"')
module_audit(proc, 'first-run')
try:
    main.close()
    proc.wait(15)
    record('Panero closes cleanly (window close button)', True)
except Exception as e:  # noqa: BLE001
    proc.kill()
    record('Panero closes cleanly (window close button)', False, str(e))
for p in psutil.process_iter(['name']):
    if p.info['name'] in ('kioslave5.exe', 'kiod5.exe'):
        p.kill()

# --- phase B: Total Commander habits on disposable files --------------------------------
proc = start(['--left', str(LEFT), '--right', str(RIGHT)], 'gui-test')
app = Application(backend='uia')
win = None
for _ in range(60):
    time.sleep(1)
    mains = [w for w in windows_of(proc.pid) if is_main(w)]
    if mains:
        app.connect(process=proc.pid)
        win = app.window(handle=mains[0].handle)
        break
if win is None:
    record('Panero starts with --left/--right', False,
           f'windows: {[w.window_text() for w in windows_of(proc.pid)]}', must=True)
    shot('no-main-window')
    proc.kill()
    sys.exit(1)
time.sleep(3)
win.set_focus()
shot('panels')
title = lambda: app.top_window().window_text()
record('Window title shows the active folder', 'left' in title(), f'title "{title()}"')

# UI Automation dump for later calibration (what Qt exposes to screen readers)
try:
    with open(OUT / 'uia-tree.txt', 'w', encoding='utf-8') as f:
        sys.stdout, old = f, sys.stdout
        win.print_control_identifiers(depth=6)
        sys.stdout = old
except Exception as e:  # noqa: BLE001
    sys.stdout = sys.__stdout__
    print('UIA dump failed:', e)


def go_to(name):
    """Put the cursor on <name> in the active (left) panel with Home + Down."""
    keys('{HOME}', 0.3)
    for _ in range(ORDER.index(name)):
        keys('{DOWN}', 0.15)


# --- navigation ---------------------------------------------------------------
go_to('docs')
keys('{ENTER}', 1.5)
shot('entered-docs')
record('Enter opens a folder', title().startswith('docs'), f'title "{title()}"')
keys('{BACKSPACE}', 1.5)
record('Backspace goes to the parent folder', title().startswith('left'), f'title "{title()}"')
keys('{TAB}', 0.8)
record('Tab switches to the other panel', title().startswith('right'), f'title "{title()}"')
keys('{TAB}', 0.8)

# --- copy with F5 (keyboard selection with Insert) --------------------------------
go_to('alpha.txt')
keys('{INSERT}', 0.4)              # mark alpha, cursor moves to beta (as in TC)
keys('{INSERT}', 0.4)              # mark beta
shot('marked-with-insert')
keys('{F5}', 1.5)
shot('f5-dialog')
keys('{ENTER}', 0.5)
ok = wait_for(lambda: (RIGHT / 'alpha.txt').exists() and (RIGHT / 'beta.txt').exists())
record('F5 copies the files marked with Insert', ok, must=True)
shot('after-f5')
module_audit(proc, 'after-copy')   # KIO workers (kioslave5.exe) are running now

# --- move with F6 (single file under the cursor) ------------------------------------
keys('{ESC}', 0.3)
keys('%-', 0.4)                    # Alt+- clears the marks (Panero; TC uses Num -)
go_to('delta.txt')
keys('{F6}', 1.5)
shot('f6-dialog')
keys('{ENTER}', 0.5)
ok = wait_for(lambda: (RIGHT / 'delta.txt').exists() and not (LEFT / 'delta.txt').exists())
record('F6 moves the file under the cursor', ok, must=True)
ORDER.remove('delta.txt')

# --- rename with F2 -------------------------------------------------------------------
go_to('gamma.txt')
keys('{F2}', 1.0)
shot('f2-inline-editor')
keys('renamed', 0.3)
keys('{ENTER}', 0.5)
ok = wait_for(lambda: not (LEFT / 'gamma.txt').exists()
              and ((LEFT / 'renamed.txt').exists() or (LEFT / 'renamed').exists()))
which = 'renamed.txt (extension kept)' if (LEFT / 'renamed.txt').exists() else 'renamed (extension replaced)'
record('F2 renames in place', ok, which if ok else '', must=True)
shot('after-f2')

# --- new folder with F7 ---------------------------------------------------------------
keys('{F7}', 1.2)
shot('f7-dialog')
keys('^a', 0.2)
keys('New folder', 0.3)
keys('{ENTER}', 0.5)
ok = wait_for(lambda: (LEFT / 'New folder').is_dir())
record('F7 creates a folder', ok, must=True)
shot('after-f7')
if ok:
    t = title()
    record('After F7 the cursor stays in the current folder', t.startswith('left'), f'title "{t}"')

# Escape out of anything left open, back to the left panel listing
keys('{ESC}', 0.3)

# --- delete to the Recycle Bin with F8 ------------------------------------------------
ORDER = ['..', 'docs', 'New folder', 'alpha.txt', 'beta.txt', 'omega.txt', 'renamed.txt']
go_to('omega.txt')
before = recycle_bin_names()
keys('{F8}', 1.5)
shot('f8-confirm')
keys('{ENTER}', 0.5)
gone = wait_for(lambda: not (LEFT / 'omega.txt').exists())
in_bin = wait_for(lambda: 'omega.txt' in recycle_bin_names() or
                  any(n.startswith('omega') for n in set(recycle_bin_names()) - set(before)), 8)
record('F8 removes the file', gone, must=True)
record('F8 sends the file to the Recycle Bin', gone and in_bin, must=True)
shot('after-f8')

# --- Delete key, as in Total Commander --------------------------------------------------
ORDER = ['..', 'docs', 'New folder', 'alpha.txt', 'beta.txt', 'renamed.txt']
go_to('beta.txt')
keys('{DELETE}', 1.5)
shot('del-confirm')
keys('{ENTER}', 0.5)
ok = wait_for(lambda: not (LEFT / 'beta.txt').exists())
record('Del key deletes to the Recycle Bin (TC habit)', ok and 'beta.txt' in recycle_bin_names())

# --- mouse selection: Ctrl+click two rows, then F5 ----------------------------------------
try:
    rows = [w for w in win.descendants() if w.window_text() in ('alpha.txt', 'renamed.txt')]
    if len(rows) >= 2:
        r = rows[0].rectangle()
        mouse.click(button='left', coords=(r.left + 10, (r.top + r.bottom) // 2))
        keyboard.send_keys('{VK_CONTROL down}')
        r = rows[1].rectangle()
        mouse.click(button='left', coords=(r.left + 10, (r.top + r.bottom) // 2))
        keyboard.send_keys('{VK_CONTROL up}')
        time.sleep(0.5)
        shot('mouse-ctrl-click')
        shutil.rmtree(RIGHT, ignore_errors=True)
        RIGHT.mkdir()
        keys('{F5}', 1.5)
        keys('{ENTER}', 0.5)
        ok = wait_for(lambda: (RIGHT / 'alpha.txt').exists() and (RIGHT / 'renamed.txt').exists())
        record('Mouse: Ctrl+click selects two files, F5 copies both', ok)
    else:
        record('Mouse: Ctrl+click selection', False,
               'file rows are not exposed to UI Automation; needs an interactive session')
except Exception as e:  # noqa: BLE001
    record('Mouse: Ctrl+click selection', False, f'automation error: {e}')

# --- drives -------------------------------------------------------------------------------
keys('{ESC}', 0.3)
keys('%{F1}', 1.2)                     # Total Commander: Alt+F1 = left drive list
shot('alt-f1')
menus = [w.window_text() for w in app.windows() if w.element_info.control_type == 'Menu']
record('Alt+F1 opens a drive list (TC habit)', bool(menus), ', '.join(menus))
keys('{ESC}', 0.5)

# --- Shift+F6, TC's rename in place ----------------------------------------------------------
ORDER = ['..', 'docs', 'New folder', 'alpha.txt', 'renamed.txt']
go_to('alpha.txt')
keys('+{F6}', 1.2)
shot('shift-f6')
dlg = app.top_window().window_text()
record('Shift+F6 renames in place (TC habit)', False,
       f'Panero opens "{dlg}" (Move delayed) instead')
keys('{ESC}', 0.5)

# --- wrap up -------------------------------------------------------------------------------
try:
    win.close()
except Exception:  # noqa: BLE001
    pass
try:
    proc.wait(15)
except subprocess.TimeoutExpired:
    proc.kill()
    record('Panero closes cleanly', False, 'killed after 15 s')

sys.exit(1 if any(r['status'] == 'FAIL' for r in results) else 0)
