# SPDX-FileCopyrightText: 2026 Panero contributors
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


def press_button(pid, label):
    """Click a dialog button by its text (falls back to Enter). Returns how it was pressed."""
    for w in windows_of(pid):
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
env['QT_LOGGING_RULES'] = 'kf.kio.*=true'
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


DIALOG_TITLES = ('Information', 'Search results', 'Konfigurator', 'Panero Settings', 'Missing Plugin', 'Warning',
                 'Error', 'Import Krusader Settings', 'Quick Settings')


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


def crop_shot(name, box, scale=3):
    """Screenshot of a screen area, enlarged (proof images for small details)."""
    shot_no[0] += 1
    img = ImageGrab.grab(bbox=box)
    img.resize((img.width * scale, img.height * scale)).save(OUT / f'{shot_no[0]:02d}-{name}.png')
    return img


def title_bar_buttons(w):
    """Names of the buttons UI Automation finds in the window's title bar."""
    names = []
    try:
        for tb in w.descendants(control_type='TitleBar'):
            names += [b.window_text() for b in tb.descendants(control_type='Button')]
    except Exception:  # noqa: BLE001
        pass
    return names


def click_title_button(w, names):
    try:
        for tb in w.descendants(control_type='TitleBar'):
            for b in tb.descendants(control_type='Button'):
                name = b.window_text()
                if name in names:
                    b.click_input()
                    time.sleep(1.2)
                    return name
    except Exception as e:  # noqa: BLE001
        print('title bar click failed:', e)
    return None


def show_cmd(hwnd):
    return win32gui.GetWindowPlacement(hwnd)[1]


def check_window_frame(w, label):
    """(a) a normal main window: minimize, maximize and an enabled close button, no '?'."""
    hwnd = w.handle
    style = win32gui.GetWindowLong(hwnd, win32con.GWL_STYLE)
    exstyle = win32gui.GetWindowLong(hwnd, win32con.GWL_EXSTYLE)
    close_state = -1
    try:
        close_state = win32gui.GetMenuState(win32gui.GetSystemMenu(hwnd, False), win32con.SC_CLOSE,
                                            win32con.MF_BYCOMMAND)
    except Exception as e:  # noqa: BLE001
        print('GetMenuState failed:', e)
    close_ok = close_state != -1 and not close_state & (win32con.MF_GRAYED | win32con.MF_DISABLED)
    buttons = title_bar_buttons(w)
    ok = bool(style & win32con.WS_MINIMIZEBOX) and bool(style & win32con.WS_MAXIMIZEBOX) \
        and not exstyle & win32con.WS_EX_CONTEXTHELP and close_ok
    record(f'Main window has minimize, maximize and an enabled close button, no "?" ({label})', ok,
           f'title bar buttons: {buttons}; WS_MINIMIZEBOX={bool(style & win32con.WS_MINIMIZEBOX)}, '
           f'WS_MAXIMIZEBOX={bool(style & win32con.WS_MAXIMIZEBOX)}, '
           f'WS_EX_CONTEXTHELP={bool(exstyle & win32con.WS_EX_CONTEXTHELP)}, close enabled={close_ok}; '
           f'UI Automation type {w.element_info.control_type}', must=True)
    r = w.rectangle()
    crop_shot(f'title-bar-{label}', (max(r.right - 260, r.left), r.top, r.right, r.top + 40))


def is_accent(px):
    r, g, b = px[:3]
    return b > 150 and b - r > 60   # Panero's highlight (accent) colour, not the grey inactive outline


def panel_outlines(w, label):
    """(c) the active panel's card outline: accent pixels at both sides of each panel."""
    shot(f'outline-{label}')
    img = ImageGrab.grab()
    handles = []
    try:
        handles = [t.rectangle() for t in w.descendants(control_type='Thumb')
                   if t.rectangle().height() > 150 and t.rectangle().width() < 12]
    except Exception:  # noqa: BLE001
        pass
    if not handles:
        return None
    h = max(handles, key=lambda r: r.height())
    wr = w.rectangle()
    cl, ct, cr, cb = win32gui.GetClientRect(w.handle)
    client_right = win32gui.ClientToScreen(w.handle, (cr, cb))[0]   # first pixel outside the client area
    y = h.top + int(h.height() * 0.62)
    left_x = h.left - 330
    try:   # the left panel's own box ends at the splitter handle
        boxes = [g.rectangle() for g in w.descendants(control_type='Group')
                 if abs(g.rectangle().right - h.left) <= 2 and abs(g.rectangle().top - h.top) <= 2]
        if boxes:
            left_x = max(r.left for r in boxes)
    except Exception:  # noqa: BLE001
        pass
    res = {}
    for side, (x0, x1) in (('left', (max(left_x, wr.left), h.left)), ('right', (h.right, client_right))):
        xs = [x for x in range(x0, x1) if is_accent(img.getpixel((x, y)))]
        res[side] = {'accent_left': xs[0] if xs else None, 'accent_right': xs[-1] if xs else None,
                     'area': (x0, x1)}
    res['client_right'] = client_right
    res['y'] = y
    crop_shot(f'outline-{label}-right-edge', (client_right - 60, h.top, min(client_right + 20, img.width), h.top + 120))
    return res


def icon_colour(box):
    """Share of yellow (Windows folder) and of blue pixels in a 16 px icon area."""
    img = ImageGrab.grab(bbox=box).convert('RGB')
    px = list(img.getdata())
    yellow = sum(1 for r, g, b in px if r > 190 and g > 140 and b < 120) / len(px)
    blue = sum(1 for r, g, b in px if b > 150 and b - r > 50) / len(px)
    dark = sum(1 for r, g, b in px if r < 90 and g < 90 and b < 90) / len(px)
    return {'yellow': round(yellow, 2), 'blue': round(blue, 2), 'dark': round(dark, 2)}


# --- phase A: first run, as a new user would start it (double-click, no arguments) ----
proc = start([], 'first-run')
seen_titles, main, first_run_dialogs = [], None, []
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
        if d.window_text() not in first_run_dialogs:
            first_run_dialogs.append(d.window_text())
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
record('First run goes straight to the panels (no welcome box, tool search or Konfigurator)',
       not first_run_dialogs, f'dialogs: {first_run_dialogs}', must=True)
r = main.rectangle()
record('Main window size on first run', True, f'{r.width()}×{r.height()} on a {ImageGrab.grab().size[0]}×'
       f'{ImageGrab.grab().size[1]} screen; title "{main.window_text()}"')
module_audit(proc, 'first-run')

# (a) title bar: minimize, maximize, close - and they work
check_window_frame(main, 'first-run')
hwnd = main.handle
pressed = click_title_button(main, ('Maximize',))
shot('window-maximized')
record('Title bar Maximize button maximizes the window', pressed is not None and show_cmd(hwnd) == win32con.SW_SHOWMAXIMIZED,
       f'pressed {pressed}; showCmd {show_cmd(hwnd)}', must=True)
if pressed:
    panel_outlines(main, 'maximized-first-run')
pressed = click_title_button(main, ('Restore',))
record('Title bar Restore button restores the window', pressed is not None and show_cmd(hwnd) == win32con.SW_SHOWNORMAL,
       f'pressed {pressed}; showCmd {show_cmd(hwnd)}')
pressed = click_title_button(main, ('Minimize',))
time.sleep(0.5)
shot('window-minimized')
record('Title bar Minimize button minimizes the window', pressed is not None and show_cmd(hwnd) == win32con.SW_SHOWMINIMIZED,
       f'pressed {pressed}; showCmd {show_cmd(hwnd)}', must=True)
win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
time.sleep(1.0)
try:
    main.set_focus()
except Exception:  # noqa: BLE001
    pass
pressed = click_title_button(main, ('Close',))
try:
    proc.wait(15)
    record('Title bar Close button closes Panero', pressed is not None, f'pressed {pressed}', must=True)
except Exception as e:  # noqa: BLE001
    try:
        main.close()
        proc.wait(15)
    except Exception:  # noqa: BLE001
        proc.kill()
    record('Title bar Close button closes Panero', False, f'pressed {pressed}; {e}', must=True)
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
record('Window title shows the active folder', title() in ('left', 'right'), f'title "{title()}"')
if title() != 'left':                    # Panero restores the panel that was active last time
    keys('{TAB}', 0.8)
    record('Tab switches to the other panel', title() == 'left', f'title "{title()}"')
main_w = Desktop(backend='uia').window(handle=win.handle).wrapper_object()
check_window_frame(main_w, 'gui-test')


def outline_check(label, active):
    res = panel_outlines(main_w, label)
    if res is None:
        record(f'Active panel outline ({label})', False, 'panel splitter not found in UI Automation')
        return
    a, o = res[active], res['left' if active == 'right' else 'right']
    both_sides = a['accent_left'] is not None and a['accent_right'] is not None \
        and a['accent_left'] - a['area'][0] < 30 and a['area'][1] - a['accent_right'] < 30
    gap = res['client_right'] - 1 - res['right']['accent_right'] if res['right']['accent_right'] is not None else None
    other_plain = o['accent_left'] is None
    note = (f"{active} panel accent outline x={a['accent_left']}..{a['accent_right']} in {a['area']}; "
            f"other panel accent: {o['accent_left']}..{o['accent_right']}; right panel's right outline "
            f"{gap if gap is not None else '-'} px inside the window's right edge (x={res['client_right'] - 1})")
    record(f'Active panel has its outline on both sides, the other none ({label})', both_sides and other_plain,
           note, must=True)
    if active == 'right':
        record(f'Right panel outline is not on the window edge ({label})', gap is not None and gap >= 3, note,
               must=True)


# (c) the active panel's outline, left and right, keyboard and mouse, normal and maximized
outline_check('left-active-tab', 'left')
keys('{TAB}', 1.0)
outline_check('right-active-tab', 'right')
keys('{TAB}', 1.0)
outline_check('left-active-again', 'left')
win32gui.ShowWindow(win.handle, win32con.SW_MAXIMIZE)
time.sleep(1.5)
outline_check('left-active-maximized', 'left')
keys('{TAB}', 1.0)
outline_check('right-active-maximized', 'right')
try:                                     # mouse: click an empty spot of each panel
    res = panel_outlines(main_w, 'click-probe')
    if res:
        lx = (res['left']['area'][0] + res['left']['area'][1]) // 2
        mouse.click(button='left', coords=(lx, res['y']))
        time.sleep(1.0)
        outline_check('left-active-click-maximized', 'left')
        rx = (res['right']['area'][0] + res['right']['area'][1]) // 2
        mouse.click(button='left', coords=(rx, res['y']))
        time.sleep(1.0)
        outline_check('right-active-click-maximized', 'right')
except Exception as e:  # noqa: BLE001
    record('Active panel outline after a mouse click', False, f'automation error: {e}')
win32gui.ShowWindow(win.handle, win32con.SW_RESTORE)
time.sleep(1.5)
win.set_focus()
if title() != 'left':
    keys('{TAB}', 0.8)

# (b) pins use the Windows shell icons, like the panels and the folder tree
PIN_NAMES = ('left', 'Desktop', 'Documents', 'Downloads', 'Home')


def visible_pins(scroll_to=None):
    items = {}
    for li in main_w.descendants(control_type='ListItem'):
        if li.window_text() in PIN_NAMES or ':)' in li.window_text():
            if li.window_text() == scroll_to:
                try:
                    li.iface_scroll_item.ScrollIntoView()
                    time.sleep(0.5)
                except Exception:  # noqa: BLE001
                    pass
            items.setdefault(li.window_text(), li)
    rects = {n: li.rectangle() for n, li in items.items()}   # positions after scrolling
    lst = [l.rectangle() for l in main_w.descendants(control_type='List') if l.window_text() == 'Pinned folders']
    if lst:   # only rows that are visible in the list
        rects = {n: r for n, r in rects.items() if r.top >= lst[0].top - 1 and r.bottom <= lst[0].bottom + 1}
    return rects


def pin_colours(rects, name):
    if rects:
        first = min(rects.values(), key=lambda r: r.top)
        crop_shot(name, (first.left, first.top - 4, first.left + 230, max(r.bottom for r in rects.values()) + 4), 3)
    return {n: icon_colour((r.left + 2, (r.top + r.bottom) // 2 - 9, r.left + 26, (r.top + r.bottom) // 2 + 9))
            for n, r in rects.items()}


try:
    colours = pin_colours(visible_pins(), 'pins-default-shell-icons')
    home = colours.get('Home')
    record('Default pins use the Windows shell icons (Home: yellow user folder; none monochrome)',
           bool(home) and home['yellow'] > 0.08 and all(c['dark'] < 0.25 for n, c in colours.items() if n in PIN_NAMES),
           f'icon colours: {colours}', must=True)
    for b in main_w.descendants(control_type='Button'):
        if b.window_text() == 'Pin current folder':
            b.click_input()
            time.sleep(1.0)
            break
    keys('{BACKSPACE}', 1.5)              # leave the folder: its pin is not highlighted then
    rects = visible_pins(scroll_to='left')
    shot('pins-shell-icons')
    colours = pin_colours(rects, 'pins-shell-icons-zoom')
    left_pin = colours.get('left')
    record('A new pin of a plain folder uses the Windows shell folder icon (yellow, as in the panel and tree)',
           bool(left_pin) and left_pin['yellow'] > 0.08, f'icon colours: {colours}', must=True)
    keys('{ENTER}', 1.5)                  # back into "left" (the cursor is on it)
except Exception as e:  # noqa: BLE001
    record('Pins use the Windows shell icons', False, f'automation error: {e}', must=True)
if not title().startswith('left'):
    record('Back in the left folder after the pin check', False, f'title "{title()}"')

# (d) Windows words: the toolbar's Recycle Bin menu
try:
    rb = [b for b in main_w.descendants() if b.window_text() == 'Recycle Bin'
          and b.element_info.control_type in ('Button', 'MenuItem', 'SplitButton')]
    trash_words = [b.window_text() for b in main_w.descendants() if 'Trash' in b.window_text()]
    if rb:
        rb[0].click_input()
        time.sleep(1.0)
        shot('toolbar-recycle-bin-menu')
        items = []
        for w in Desktop(backend='uia').windows(process=proc.pid):
            try:
                items += [i.window_text() for i in w.descendants(control_type='MenuItem')]
            except Exception:  # noqa: BLE001
                pass
        keys('{ESC}', 0.5)
        record('Toolbar says Recycle Bin (Open / Empty Recycle Bin), not Trash',
               'Open Recycle Bin' in items and 'Empty Recycle Bin' in items and not trash_words,
               f'menu: {items}; "Trash" texts in the window: {trash_words}', must=True)
    else:
        record('Toolbar says Recycle Bin, not Trash', False, f'no Recycle Bin button; "Trash" texts: {trash_words}',
               must=True)
except Exception as e:  # noqa: BLE001
    record('Toolbar says Recycle Bin, not Trash', False, f'automation error: {e}', must=True)
win.set_focus()

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
record('Tab switches back', title().startswith('right'), f'title "{title()}"')
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
record('F2 renames in place', ok, must=True)
record('F2 keeps the extension (selects the name only, as in TC)', (LEFT / 'renamed.txt').exists(),
       'renamed.txt' if (LEFT / 'renamed.txt').exists() else 'typed text replaced "gamma.txt" completely')
if (LEFT / 'renamed').exists():
    (LEFT / 'renamed').rename(LEFT / 'renamed.txt')   # keep the rest of the script on track
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
dlg_texts = []
for w in windows_of(proc.pid):
    if not is_main(w):
        try:
            dlg_texts += [t.window_text() for t in w.descendants(control_type='Text')]
        except Exception:  # noqa: BLE001
            pass
how = press_button(proc.pid, 'Move to Recycle Bin')
record('F8 confirmation button says "Move to Recycle Bin"', how == 'clicked', f'button {how}', must=True)
record('F8 question speaks of the Recycle Bin', any('Recycle Bin' in t for t in dlg_texts), f'text: {dlg_texts}')
gone = wait_for(lambda: not (LEFT / 'omega.txt').exists())
time.sleep(1)
shot('f8-result')
errors = [w.window_text() for w in windows_of(proc.pid) if not is_main(w)]
in_bin = wait_for(lambda: 'omega.txt' in recycle_bin_names() or
                  any(n.startswith('omega') for n in set(recycle_bin_names()) - set(before)), 8)
record('F8 removes the file', gone, f'Recycle Bin button {how}; dialogs after: {errors}', must=True)
record('F8 sends the file to the Recycle Bin', gone and in_bin, must=True)
shot('after-f8')

# --- Delete key, as in Total Commander --------------------------------------------------
ORDER = ['..', 'docs', 'New folder', 'alpha.txt', 'beta.txt', 'renamed.txt']
go_to('beta.txt')
keys('{DELETE}', 1.5)
shot('del-confirm')
press_button(proc.pid, 'Move to Recycle Bin')
ok = wait_for(lambda: not (LEFT / 'beta.txt').exists())
binned = any(n.startswith('beta') for n in recycle_bin_names())
record('Del key deletes to the Recycle Bin (TC habit)', ok and binned,
       f'removed: {ok}, in Recycle Bin: {binned}')

# --- mouse selection: Ctrl+click two rows, then F5 ----------------------------------------
try:
    # the Name column shows "alpha" (the extension has its own column, as in TC)
    mid = win.rectangle().mid_point().x
    rows = [w for w in win.descendants() if w.window_text() in ('alpha', 'renamed')
            and w.rectangle().right < mid + 120]           # left panel only
    rows.sort(key=lambda w: w.rectangle().top)
    with open(OUT / 'uia-rows.txt', 'w', encoding='utf-8') as f:
        for w in rows:
            f.write(f'{w.element_info.control_type}\t{w.window_text()}\t{w.rectangle()}\n')
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
        time.sleep(1)
        got = sorted(p.name for p in RIGHT.iterdir())
        record('Mouse: click + Ctrl+click selects two files, F5 copies exactly those', ok and got == [
            'alpha.txt', 'renamed.txt'], f'right panel now has {got}')
    else:
        record('Mouse: Ctrl+click selection', False,
               'file rows are not exposed to UI Automation; needs an interactive session')
except Exception as e:  # noqa: BLE001
    record('Mouse: Ctrl+click selection', False, f'automation error: {e}')

# --- drives -------------------------------------------------------------------------------
keys('{ESC}', 0.3)
def drive_menu_items():
    items = []
    for w in Desktop(backend='uia').windows(process=proc.pid):
        try:
            items += [i.window_text() for i in w.descendants(control_type='MenuItem')
                      if '(' in i.window_text() and ':)' in i.window_text()]
        except Exception:  # noqa: BLE001
            pass
    return items


keys('%{F1}', 1.2)                     # Total Commander: Alt+F1 = left drive list
shot('alt-f1')
items = drive_menu_items()
record('Alt+F1 opens the drive list with Explorer names (TC habit)', bool(items), ', '.join(items))
keys('{ESC}', 0.5)
# Alt+F2: right drive list; pick the second drive and check the right panel follows
keys('%{F2}', 1.2)
items = drive_menu_items()
if len(items) > 1:
    keys('{DOWN}{DOWN}{ENTER}', 1.5)
    shot('alt-f2-second-drive')
    t = title()
    letter = items[1][items[1].rfind('(') + 1:items[1].rfind(')')]
    record('Alt+F2 + Enter switches the right panel to another drive', letter.rstrip(':') in t or t.endswith(':/')
           or t.endswith(':\\') or len(t) <= 3, f'chose "{items[1]}", title "{t}"')
else:
    keys('{ESC}', 0.5)
    record('Alt+F2 + Enter switches the right panel to another drive', False, f'menu items: {items}')
keys('{TAB}', 0.8)                      # back to the left panel

# --- Shift+F6, TC's rename in place ----------------------------------------------------------
ORDER = ['..', 'docs', 'New folder', 'alpha.txt', 'renamed.txt']
go_to('alpha.txt')
keys('+{F6}', 1.2)
shot('shift-f6')
dlg = [w.window_text() for w in windows_of(proc.pid) if not is_main(w)]
record('Shift+F6 renames in place (TC habit)', False,
       f'Panero opens the "Move ... to" dialog with a Queue option (Move delayed) instead: {dlg}')
keys('{ESC}', 0.5)

# --- (d) Windows words in the right-click menu and the settings window ------------------------
try:
    rows = sorted((w for w in win.descendants() if w.window_text() == 'alpha'), key=lambda w: w.rectangle().left)
    if rows:
        # Panero's right button selects (Total Commander style); the menu key opens the menu
        win.set_focus()
        go_to('alpha.txt')
        keys('{VK_APPS}', 1.5)
        shot('context-menu-recycle-bin')
        items = []
        for w in Desktop(backend='uia').windows(process=proc.pid):
            try:
                items += [i.window_text() for i in w.descendants(control_type='MenuItem')]
            except Exception:  # noqa: BLE001
                pass
        keys('{ESC}', 0.5)
        record('Right-click menu says "Move to Recycle Bin"', 'Move to Recycle Bin' in items
               and not any('Trash' in i for i in items), f'menu: {items}', must=True)
    else:
        record('Right-click menu says "Move to Recycle Bin"', False, 'file row not exposed to UI Automation')
except Exception as e:  # noqa: BLE001
    record('Right-click menu says "Move to Recycle Bin"', False, f'automation error: {e}')
keys('{ESC}', 0.3)
win.set_focus()
before_wins = {w.handle for w in windows_of(proc.pid)}
keys('^+,', 2.5)                       # Configure Panero (KDE's Preferences shortcut)
new_wins = [w for w in windows_of(proc.pid) if w.handle not in before_wins]
shot('settings-window')
names = [w.window_text() for w in new_wins]
record('The settings window is called "Panero Settings" (not Konfigurator)', any(n.startswith('Panero Settings') for n in names)
       and not any('Konfigurator' in n for n in names), f'windows: {names}', must=True)
for w in new_wins:
    try:
        w.close()
    except Exception:  # noqa: BLE001
        pass
time.sleep(1)
for w in windows_of(proc.pid):           # "apply changes?" or similar
    if not is_main(w):
        try:
            w.set_focus()
            keys('{ESC}', 0.5)
        except Exception:  # noqa: BLE001
            pass
win.set_focus()

# --- F3 view, F4 edit, F9 terminal (TC habits) ------------------------------------------------
def new_processes(before, names):
    return [p for p in psutil.process_iter(['name', 'pid']) if p.info['name'] in names and p.info['pid'] not in before]


keys('{ESC}', 0.5)
keys('%-', 0.4)                         # clear the marks from the mouse test
ORDER = ['..', 'docs', 'New folder', 'alpha.txt', 'renamed.txt']
go_to('alpha.txt')
before_wins = {w.handle for w in windows_of(proc.pid)}
keys('{F3}', 2.5)
shot('f3-viewer')
viewer = [w for w in windows_of(proc.pid) if w.handle not in before_wins]
names = [w.window_text() for w in viewer]
record('F3 opens the file in a viewer', bool(viewer) and not any(n.startswith('Error') for n in names),
       ', '.join(names))
for w in viewer:                         # close the viewer (or an error box) so nothing blocks
    try:
        w.close()
    except Exception:  # noqa: BLE001
        pass
time.sleep(1)
for w in windows_of(proc.pid):
    if not is_main(w):
        try:
            w.set_focus()
            keys('{ESC}', 0.5)
        except Exception:  # noqa: BLE001
            pass
time.sleep(1)
win.set_focus()
before = {p.pid for p in psutil.process_iter()}
keys('{F4}', 3.0)
shot('f4-editor')
eds = new_processes(before, ('notepad.exe', 'Notepad.exe'))
record('F4 opens the file in Notepad (Windows default)', bool(eds), ', '.join(p.info['name'] for p in eds))
for p in eds:
    p.kill()
time.sleep(1)
win.set_focus()
before = {p.pid for p in psutil.process_iter()}
keys('{F9}', 3.0)
shot('f9-terminal')
terms = new_processes(before, ('WindowsTerminal.exe', 'wt.exe', 'powershell.exe', 'cmd.exe', 'OpenConsole.exe'))
record('F9 opens a terminal in the current folder', bool(terms), ', '.join(p.info['name'] for p in terms))
for p in terms:
    try:
        p.kill()
    except psutil.Error:
        pass
time.sleep(1)
for w in windows_of(proc.pid):          # an error box would block closing
    if not is_main(w):
        record('No dialog left open after F9', False, w.window_text())
        try:
            w.set_focus()
            keys('{ESC}', 0.5)
        except Exception:  # noqa: BLE001
            pass

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
