# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""First-run GUI checks of the Windows build from a Total Commander user's view.

Runs on a disposable GitHub Windows runner against disposable files in
C:\\pt\\work. With --vm (dev/windows/vm-gui-test.ps1) it runs on a real machine:
files in --work, no Cloud Files test provider, Panero with PANERO_TEST_PROFILE=1
(separate settings), and only the items it deleted are removed from the Recycle Bin.

Drives Panero with the keyboard (and the mouse where UI Automation
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
# VM desktop: input only while Panero is in front (fgguard.py)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fgguard  # noqa: E402
fgguard.install()

sys.stdout.reconfigure(encoding='utf-8', errors='replace')  # the CI console is cp1252
ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
# --vm: a real Windows machine (the developer VM, D-022) instead of a disposable
# runner: own work folder, no Cloud Files test provider, the Recycle Bin left
# as it was (only the items this test deleted are removed from it again)
ap.add_argument('--vm', action='store_true')
ap.add_argument('--work', default=r'C:\pt\work')
args = ap.parse_args()
OUT = Path(args.out)
OUT.mkdir(parents=True, exist_ok=True)
VM = args.vm

EXE = os.environ['PANERO_EXE']
WORK = Path(args.work).resolve()
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


def recycled_from_work():
    """Recycle Bin entries ($I info + $R data in C:\\$Recycle.Bin\\<SID>) whose original
    path lies inside WORK: only what this test deleted."""
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
            if int.from_bytes(data[0:8], 'little') == 2:       # Windows 10/11: length + path
                n = int.from_bytes(data[24:28], 'little')
                orig = data[28:28 + 2 * n].decode('utf-16-le').rstrip('\0')
            else:                                              # Vista..8.1: fixed 520 bytes
                orig = data[24:24 + 520].decode('utf-16-le').split('\0')[0]
        except (OSError, UnicodeDecodeError):
            continue
        if orig.lower().startswith(work):
            out.append((orig, info, folder / ('$R' + info.name[2:])))
    return out


def purge_own_recycled():
    """VM: permanently removes the Recycle Bin items that came from WORK (nothing else)."""
    removed = []
    for orig, info, data in recycled_from_work():
        try:
            if data.is_dir():
                shutil.rmtree(data)
            else:
                data.unlink(missing_ok=True)
            info.unlink()
            removed.append(orig)
        except OSError as e:
            print('could not purge', orig, e)
    if removed:
        print('removed from the Recycle Bin (deleted by this test):', removed, flush=True)
    return removed


if VM:
    atexit.register(purge_own_recycled)


def recycle_bin_names():
    ps = ('$s = New-Object -ComObject Shell.Application; '
          '$s.Namespace(10).Items() | ForEach-Object { $_.Name }')
    out = subprocess.run(['powershell', '-NoProfile', '-Command', ps],
                         capture_output=True, text=True).stdout
    return [l.strip() for l in out.splitlines() if l.strip()]


# --- disposable files -------------------------------------------------------
if VM:
    purge_own_recycled()   # left over from an interrupted run
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
env['PANERO_DEBUG_EXPLORER_MENU'] = '1'   # Explorer menu items, choices and results in the log
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
                # dialogs owned by the main window (Panero gives parentless dialogs the
                # main window as parent) sit below it in the UI Automation tree
                for c in w.children(control_type='Window'):
                    h = c.handle
                    if h and h != w.handle and win32gui.GetWindow(h, win32con.GW_OWNER) and c.is_visible() \
                            and c.window_text():
                        out.append(c)
        except Exception:  # window vanished
            pass
    return out


DIALOG_TITLES = ('Information', 'Search results', 'Konfigurator', 'Panero Settings', 'Missing Plugin', 'Warning',
                 'Error', 'Import Krusader Settings', 'Quick Settings', 'Welcome to Panero')
ASSISTANT = 'Welcome to Panero'   # first-start assistant (D-022): new users only


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
                if VM:   # a real machine: shell extensions (OneDrive, 7-Zip, ...) load too; only MSYS2 is wrong
                    if '\\msys64' in path:
                        bad.append(f'{p.name()}: {m.path}')
                elif not (path.startswith(APP_DIR) or path.startswith(WINDIR)):
                    bad.append(f'{p.name()}: {m.path}')
        except psutil.Error:
            pass
    with open(OUT / f'loaded-modules-{label}.txt', 'w') as f:
        f.write('\n'.join(sorted(set(seen))))
    names = sorted({p.name() for p in procs if p.is_running()})
    record(f'No loaded module comes from MSYS2 ({label})' if VM else
           f'Every loaded module comes from the ZIP folder or Windows ({label})', not bad,
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


def norm(text):
    """A menu item's text without accelerator, shortcut and dots, lower case."""
    t = (text or '').split('\t')[0].replace('&', '').strip().lower()
    return t.rstrip('.\u2026').strip()


def menu_items(pid):
    """Menu items of the popup menus of Panero (Explorer's native menu, Qt menus)."""
    items = []
    for w in Desktop(backend='uia').windows(process=pid):
        try:
            if is_main(w):
                continue
            items += [i for i in w.descendants(control_type='MenuItem')]
        except Exception:  # noqa: BLE001
            pass
    return items


def menu_texts(pid):
    out = []
    for i in menu_items(pid):
        try:
            out.append(i.window_text())
        except Exception:  # noqa: BLE001
            pass
    return out


def wait_menu(pid, want=('panero menu',), timeout=12):
    """Waits until an open menu has one of @p want (the shell loads its extensions first)."""
    texts, end = [], time.time() + timeout
    while time.time() < end:
        texts = menu_texts(pid)
        if any(norm(t) in want for t in texts):
            return texts
        time.sleep(0.4)
    return texts


def click_native_submenu_item(text, parent_text):
    """Clicks an item of an open native submenu (UI Automation does not list the
    submenus of shell extensions): finds the #32768 menu windows, asks each for its
    HMENU (MN_GETHMENU) and clicks the item by text, else the submenu's first item."""
    import ctypes
    from ctypes import wintypes
    u32 = ctypes.windll.user32
    menus = []

    def found(h, _):
        if win32gui.GetClassName(h) == '#32768' and win32gui.IsWindowVisible(h):
            menus.append(h)
        return True
    win32gui.EnumWindows(found, None)
    want, parent, fallback = norm(text), norm(parent_text), None
    for h in menus:
        hmenu = win32gui.SendMessage(h, 0x01E1, 0, 0)   # MN_GETHMENU
        if not hmenu:
            continue
        texts = []
        for i in range(u32.GetMenuItemCount(wintypes.HMENU(hmenu))):
            buf = ctypes.create_unicode_buffer(256)
            u32.GetMenuStringW(wintypes.HMENU(hmenu), i, buf, 256, 0x400)   # MF_BYPOSITION
            texts.append(norm(buf.value))
        if parent in texts:                 # the parent menu
            continue
        pos = texts.index(want) if want in texts else 0
        r = wintypes.RECT()
        if u32.GetMenuItemRect(None, wintypes.HMENU(hmenu), pos, ctypes.byref(r)):
            fallback = (pos, texts, ((r.left + r.right) // 2, (r.top + r.bottom) // 2))
            if want in texts:
                break
    if fallback:
        mouse.click(button='left', coords=fallback[2])
        time.sleep(1.5)
        return f'item {fallback[0]} of {fallback[1]}'
    return None


def close_menus():
    for _ in range(3):
        keys('{ESC}', 0.3)


def click_menu(pid, text, close_on_fail=True, pause=1.5):
    want = norm(text)
    for i in menu_items(pid):
        try:
            if norm(i.window_text()) == want:
                i.click_input()
                time.sleep(pause)
                return True
        except Exception:  # noqa: BLE001
            pass
    if close_on_fail:
        close_menus()
    return False


def panel_view_rect(main_w, win):
    """The left file panel's view (UI Automation: a Tree between the sidebar and the middle)."""
    mid = win.rectangle().mid_point().x
    pins = [l.rectangle() for l in main_w.descendants(control_type='List') if l.window_text() == 'Pinned folders']
    side_right = pins[0].right if pins else win.rectangle().left + 200
    views = [t.rectangle() for t in main_w.descendants(control_type='Tree')
             if side_right - 5 <= t.rectangle().left < mid and t.rectangle().height() > 100]
    return views[0] if views else None


def hold_right(x, y):
    """Panero's right button marks (Total Commander); held, it opens the menu."""
    mouse.move(coords=(x, y))
    time.sleep(0.3)
    mouse.press(button='right', coords=(x, y))
    time.sleep(1.4)
    mouse.release(button='right', coords=(x, y))
    time.sleep(0.3)


def clipboard_text():
    import win32clipboard
    for _ in range(5):
        try:
            win32clipboard.OpenClipboard()
            try:
                return win32clipboard.GetClipboardData(win32con.CF_UNICODETEXT)
            finally:
                win32clipboard.CloseClipboard()
        except Exception:  # noqa: BLE001
            time.sleep(0.3)
    return None


def panel_order(folder):
    """The rows as Panero sorts them: "..", folders, then files, by name."""
    entries = list(Path(folder).iterdir())
    dirs = sorted((e.name for e in entries if e.is_dir()), key=str.lower)
    files = sorted((e.name for e in entries if not e.is_dir()), key=str.lower)
    return ['..'] + dirs + files


SHELL_FILE = ['open with', 'send to', 'properties']      # Explorer's items on a file
SHELL_DIR = ['send to', 'properties']
SHELL_BACKGROUND = ['new', 'properties']
EXTRAS = ['copy to other panel', 'move to other panel', 'copy path', 'open terminal here', 'panero menu']


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
        # First-start assistant: Enter goes Next ... Start Panero with the defaults.
        # Konfigurator (opened by older first runs): close it, keep defaults.
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
record('First run: only the first-start assistant, then the panels (no welcome box, tool search or Konfigurator)',
       all(t == ASSISTANT for t in first_run_dialogs), f'dialogs: {first_run_dialogs}', must=True)
record('First run shows the first-start assistant', ASSISTANT in first_run_dialogs, f'dialogs: {first_run_dialogs}')
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
for p in psutil.process_iter(['name', 'exe']):
    if p.info['name'] in ('kioslave5.exe', 'kiod5.exe') and (p.info['exe'] or '').lower().startswith(APP_DIR):
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

# --- Explorer's right-click menu with Panero's extras on top (D-019) ---------------------------
def explorer_menu(label, opener, shell_need, extras_need, keep_open=False):
    opener()
    texts = wait_menu(proc.pid)
    time.sleep(0.8)
    shot(f'p12-explorer-menu-{label}')
    have = {norm(t) for t in texts}
    missing = [x for x in shell_need + extras_need if x not in have]
    first = norm(texts[0]) if texts else ''
    record(f'Explorer menu on {label}: the shell\'s {shell_need} and Panero\'s {extras_need}', not missing,
           f'missing: {missing}; menu: {texts}', must=True)
    if not keep_open:
        close_menus()
    return texts


def menu_key_on(name):
    def open_it():
        go_to(name)
        keys('{VK_APPS}', 0.5)
    return open_it


try:
    win.set_focus()
    keys('{ESC}', 0.3)
    keys('%-', 0.4)                     # clear the marks of the mouse test
    ORDER = panel_order(LEFT)
    texts = explorer_menu('file', menu_key_on('alpha.txt'), SHELL_FILE, EXTRAS)
    top = [norm(t) for t in texts[:6]]
    record('Panero\'s extras are at the top of the Explorer menu (Copy to other panel first)',
           bool(top) and top[0] == 'copy to other panel', f'first items: {texts[:7]}')
    explorer_menu('folder', menu_key_on('docs'), SHELL_DIR, EXTRAS)

    def several():
        go_to('alpha.txt')
        keys('{INSERT}{INSERT}', 0.4)    # alpha.txt and renamed.txt
        keys('{VK_APPS}', 0.5)
    explorer_menu('several', several, SHELL_FILE[1:], ['copy to other panel', 'copy paths', 'panero menu'])
    keys('%-', 0.4)

    view = panel_view_rect(main_w, win)
    bg_point = (view.left + 80, view.bottom - 30) if view else None

    def background():
        if bg_point:
            hold_right(*bg_point)
    explorer_menu('background', background, SHELL_BACKGROUND, ['copy path', 'open terminal here', 'panero menu'])

    # Copy to other panel (F5) from the menu
    (RIGHT / 'alpha.txt').unlink(missing_ok=True)
    time.sleep(1.0)
    go_to('alpha.txt')
    keys('{VK_APPS}', 0.5)
    wait_menu(proc.pid)
    ok = click_menu(proc.pid, 'Copy to other panel')
    shot('p12-explorer-menu-copy-dialog')
    if ok:
        keys('{ENTER}', 0.5)
    copied = wait_for(lambda: (RIGHT / 'alpha.txt').exists())
    record('Explorer menu: "Copy to other panel" copies the file (F5 dialog)', ok and copied,
           f'clicked: {ok}; in the right folder: {sorted(p.name for p in RIGHT.iterdir())}', must=True)

    # Copy Path: the Windows path with backslashes
    go_to('alpha.txt')
    keys('{VK_APPS}', 0.5)
    wait_menu(proc.pid)
    ok = click_menu(proc.pid, 'Copy Path')
    clip = clipboard_text()
    record('Explorer menu: "Copy Path" puts the Windows path on the clipboard', ok and clip == str(LEFT / 'alpha.txt'),
           f'clipboard: {clip!r}', must=True)

    # a shell verb: "Copy as path" (Windows 11 / Server 2025 shows it without Shift)
    go_to('alpha.txt')
    keys('{VK_APPS}', 0.5)
    wait_menu(proc.pid)
    ok = click_menu(proc.pid, 'Copy as path')
    clip = clipboard_text()
    record('Explorer menu: the shell\'s "Copy as path" works', ok and clip == f'"{LEFT / "alpha.txt"}"',
           f'clicked: {ok}; clipboard: {clip!r}')

    # New > Folder on the background (a submenu filled by a shell extension: IContextMenu3)
    before = {p.name for p in LEFT.iterdir()}
    if bg_point:
        hold_right(*bg_point)
    wait_menu(proc.pid)
    ok_new = click_menu(proc.pid, 'New', close_on_fail=False, pause=1.5)
    sub = menu_texts(proc.pid)
    shot('p12-explorer-menu-new-submenu')
    ok_folder = click_menu(proc.pid, 'Folder', close_on_fail=False) \
        or (ok_new and click_native_submenu_item('Folder', 'New'))
    if not ok_new or not ok_folder:
        close_menus()
    created = []
    wait_for(lambda: created.extend(sorted({p.name for p in LEFT.iterdir() if p.is_dir()} - before)) or created, 8)
    time.sleep(2.5)                      # Panero refreshes after the command
    shot('p12-explorer-menu-new-folder')
    record('Explorer menu: New > Folder (the shell) creates a folder, and the panel shows it', bool(created),
           f'New: {ok_new}, Folder: {ok_folder}; created {created}; submenu: {sub}', must=True)

    if created:
        new_name = created[0]
        # the shell's "Rename" starts Panero's rename in place
        ORDER = panel_order(LEFT)
        go_to(new_name)
        keys('{VK_APPS}', 0.5)
        wait_menu(proc.pid)
        ok = click_menu(proc.pid, 'Rename', pause=1.0)
        shot('p12-explorer-menu-rename')
        keys('^a', 0.2)
        keys('shellmenu', 0.3)
        keys('{ENTER}', 0.5)
        renamed = wait_for(lambda: (LEFT / 'shellmenu').is_dir())
        record('Explorer menu: "Rename" renames in place (Panero\'s inline rename)', ok and renamed,
               f'clicked: {ok}; folders now: {sorted(p.name for p in LEFT.iterdir() if p.is_dir())}', must=True)
        time.sleep(1.5)
        target = 'shellmenu' if renamed else new_name
        # the shell's "Delete": to the Recycle Bin, as in Explorer
        ORDER = panel_order(LEFT)
        go_to(target)
        before_wins = {w.handle for w in windows_of(proc.pid)}
        keys('{VK_APPS}', 0.5)
        wait_menu(proc.pid)
        ok = click_menu(proc.pid, 'Delete', pause=2.0)
        dialogs = [w for w in windows_of(proc.pid) if w.handle not in before_wins and not is_main(w)]
        names = [w.window_text() for w in dialogs]
        if dialogs:                       # "Are you sure you want to move this folder to the Recycle Bin?"
            shot('p12-explorer-menu-delete-confirm')
            press_button(proc.pid, 'Yes')
        gone = wait_for(lambda: not (LEFT / target).exists(), 10)
        binned = wait_for(lambda: target in recycle_bin_names(), 8)
        time.sleep(2.0)
        shot('p12-explorer-menu-after-delete')
        record('Explorer menu: the shell\'s "Delete" moves the folder to the Recycle Bin', ok and gone and binned,
               f'clicked: {ok}; dialogs: {names}; gone: {gone}; in the Recycle Bin: {binned}', must=True)
        ORDER = panel_order(LEFT)
        visible = [w.window_text() for w in win.descendants() if w.window_text() == target]
        record('After a shell command the panel is refreshed', not visible, f'rows named {target}: {len(visible)}')

    # Properties from the shell
    ORDER = panel_order(LEFT)
    go_to('alpha.txt')
    before_wins = {w.handle for w in windows_of(proc.pid)}
    keys('{VK_APPS}', 0.5)
    wait_menu(proc.pid)
    ok = click_menu(proc.pid, 'Properties', pause=1.0)
    props = []
    wait_for(lambda: props.extend(w for w in windows_of(proc.pid) if w.handle not in before_wins) or props, 10)
    time.sleep(1.0)
    shot('p12-explorer-menu-properties')
    names = [w.window_text() for w in props]
    record('Explorer menu: "Properties" opens the Windows properties of the file', ok and any('alpha' in n for n in names),
           f'windows: {names}', must=True)
    for w in props:
        try:
            w.close()
        except Exception:  # noqa: BLE001
            pass
    time.sleep(1.0)

    # the shell's "Open" on a folder enters it in the panel (no Explorer window)
    win.set_focus()
    go_to('docs')
    keys('{VK_APPS}', 0.5)
    wait_menu(proc.pid)
    ok = click_menu(proc.pid, 'Open', pause=1.5)
    t = title()
    shot('p12-explorer-menu-open-folder')
    explorers = [w.window_text() for w in Desktop(backend='uia').windows() if w.window_text() == 'docs'
                 and w.process_id() != proc.pid]
    record('Explorer menu: "Open" on a folder opens it in the panel, not in Explorer', ok and t.startswith('docs')
           and not explorers, f'title "{t}"; Explorer windows: {explorers}', must=True)
    for w in Desktop(backend='uia').windows():
        if w.window_text() == 'docs' and w.process_id() != proc.pid:
            try:
                w.close()
            except Exception:  # noqa: BLE001
                pass
    if t.startswith('docs'):
        keys('{BACKSPACE}', 1.5)
except Exception as e:  # noqa: BLE001
    record('Explorer menu checks', False, f'automation error: {e!r}', must=True)
    close_menus()
win.set_focus()
keys('%-', 0.4)
ORDER = panel_order(LEFT)

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
for _ in range(2):                      # a slow second drive (a VM's CD drive) may swallow the Tab
    if title().startswith('left'):
        break
    time.sleep(1.5)
    keys('{TAB}', 0.8)

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
        keys('{VK_APPS}', 0.5)
        shell_items = wait_menu(proc.pid)
        # Explorer's menu first (D-019); Panero's own menu through "Panero Menu"
        opened = click_menu(proc.pid, 'Panero Menu', pause=1.5)
        shot('context-menu-recycle-bin')
        items = menu_texts(proc.pid)
        close_menus()
        record('Right-click menu says "Move to Recycle Bin" (Panero Menu), Explorer\'s menu has no "Trash"',
               opened and 'Move to Recycle Bin' in items and not any('Trash' in i for i in items + shell_items),
               f'Explorer menu: {shell_items}; Panero menu: {items}', must=True)
    else:
        record('Right-click menu says "Move to Recycle Bin"', False, 'file row not exposed to UI Automation')
except Exception as e:  # noqa: BLE001
    record('Right-click menu says "Move to Recycle Bin"', False, f'automation error: {e}')
keys('{ESC}', 0.3)
win.set_focus()
before_wins = {w.handle for w in windows_of(proc.pid)}
keys('^+,', 1.0)                       # Settings (KDE's Preferences shortcut; Ctrl+, too)
new_wins = []
for _ in range(20):                    # 15 pages: give the window time on a slow runner
    new_wins = [w for w in windows_of(proc.pid) if w.handle not in before_wins]
    if new_wins:
        break
    time.sleep(0.5)
time.sleep(1)
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


# --- phase C (D-020): folder icons on C:\, a real cloud sync folder, no downloads while browsing ----
import ctypes  # noqa: E402

k32 = ctypes.WinDLL('kernel32')
k32.GetFileAttributesW.restype = ctypes.c_uint32
k32.GetFileAttributesW.argtypes = [ctypes.c_wchar_p]
RECALL_ON_DATA_ACCESS, PINNED, UNPINNED = 0x400000, 0x80000, 0x100000
PROFILE = Path(os.environ['USERPROFILE'])
CLOUD = PROFILE / 'PaneroTestCloud'
CLOUD_NAME = 'Panero Test Cloud'
PLACEHOLDERS = ['notes.txt', 'report.pdf', 'photo.jpg', 'tool.exe', 'noext', 'Shared/plan.docx']
FETCH_LOG = OUT / 'cloud-provider-log.jsonl'
READY, STOP = OUT / 'cloud-provider-state.json', OUT / 'cloud-provider.stop'
PANERO_PROCS = ('panero.exe', 'kioslave5.exe', 'kiod5.exe')


def wattrs(rel):
    return k32.GetFileAttributesW(str(CLOUD / rel))


def fetches(by_panero=True):
    out = []
    if FETCH_LOG.exists():
        for line in FETCH_LOG.read_text(encoding='utf-8').splitlines():
            e = json.loads(line)
            if e.get('type') == 'FETCH_DATA' and (not by_panero or e.get('image', '').lower().endswith(PANERO_PROCS)):
                out.append(e)
    return out


def still_placeholders():
    return {rel: f'0x{wattrs(rel):08x}' for rel in PLACEHOLDERS if not wattrs(rel) & RECALL_ON_DATA_ACCESS}


def main_window_of(p):
    for _ in range(60):
        time.sleep(1)
        mains = [w for w in windows_of(p.pid) if is_main(w)]
        if mains:
            a = Application(backend='uia')
            a.connect(process=p.pid)
            return a, a.window(handle=mains[0].handle), Desktop(backend='uia').window(handle=mains[0].handle).wrapper_object()
    return None, None, None


provider = None
if VM:   # no test sync root on a real machine (it would sit in the user's profile and registry)
    pstate = {'ok': False, 'vm': True}
else:
    try:
        for f in (FETCH_LOG, READY, STOP):
            f.unlink(missing_ok=True)
        provider = subprocess.Popen([sys.executable, str(Path(__file__).with_name('cloud-provider.py')), '--root',
                                     str(CLOUD), '--log', str(FETCH_LOG), '--ready', str(READY), '--stop', str(STOP),
                                     '--name', CLOUD_NAME],
                                    stdout=open(OUT / 'cloud-provider-stdout.txt', 'w'), stderr=subprocess.STDOUT)
        wait_for(lambda: READY.exists() or provider.poll() is not None, 60)
        time.sleep(0.5)
        pstate = json.loads(READY.read_text()) if READY.exists() else {'ok': False, 'exit': provider.poll()}
    except Exception as e:  # noqa: BLE001
        pstate = {'ok': False, 'error': repr(e)}
cloud_ok = bool(pstate.get('ok'))
if not VM:
    record('Test sync root registered with the Cloud Files API (cldapi, as OneDrive)', cloud_ok,
           f"register {pstate.get('register_hr')}, connect {pstate.get('connect_hr')}, placeholders "
           f"{pstate.get('placeholders_hr')}, registry: {pstate.get('registry')}; attributes {pstate.get('attributes')}"
           + (f"; {pstate}" if not cloud_ok else ''), must=True)
if cloud_ok:
    record('Placeholders are online-only before Panero starts (RECALL_ON_DATA_ACCESS)', not still_placeholders(),
           f'not placeholders: {still_placeholders()}', must=True)

proc = start(['--left', 'C:\\', '--right', str(PROFILE)], 'cloud')
app, win, main_w = main_window_of(proc)
if win is None:
    record('Panero starts with C:\\ and the profile folder', False, 'no main window', must=True)
    proc.kill()
else:
    time.sleep(3)
    win.set_focus()
    title = lambda: app.top_window().window_text()
    if title() == PROFILE.name:
        keys('{TAB}', 1.0)
    shot('c-drive-folders')
    mid = win.rectangle().mid_point().x
    pins_list = [l.rectangle() for l in main_w.descendants(control_type='List') if l.window_text() == 'Pinned folders']
    side_right = pins_list[0].right if pins_list else win.rectangle().left + 200

    def icon_checks(names, label, tree, found=None):
        """Icon colours of visible folder rows by UI Automation name: the sidebar's folder
        tree (left of the panels) or the left panel (both are tree views: TreeItem)."""
        found = {} if found is None else found
        wr = win.rectangle()
        # the visible part of the view: the tree view control under the rows
        views = []
        for t in main_w.descendants(control_type='Tree'):
            tr = t.rectangle()
            if (tree and tr.right <= side_right + 5) or (not tree and side_right - 5 <= tr.left < mid):
                views.append(tr)
        vtop = views[0].top + (0 if tree else 22) if views else wr.top   # the panel's column header
        vbottom = views[0].bottom if views else wr.bottom
        with open(OUT / f'uia-{label}.txt', 'a', encoding='utf-8') as dump:
            for el in main_w.descendants(control_type='TreeItem'):
                try:
                    t = el.window_text()
                    r = el.rectangle()
                    dump.write(f'{t}\t{r}\n')
                    if t not in names or t in found or r.height() < 8 or r.top < vtop or r.bottom > vbottom:
                        continue
                    if (tree and r.left >= side_right) or (not tree and not side_right <= r.left < mid):
                        continue
                    cy = (r.top + r.bottom) // 2
                    found[t] = icon_colour((r.left + 1, cy - 9, r.left + 27, cy + 9))
                except Exception:  # noqa: BLE001
                    pass
        box = (wr.left, wr.top + 100, side_right, wr.bottom - 40) if tree else (side_right, wr.top + 100, mid, wr.bottom - 40)
        crop_shot(f'{label}-icons-zoom', box, 2)
        return found

    c_names = ['inetpub', 'PerfLogs', 'Program Files', 'Program Files (x86)', 'Users', 'Windows']
    existing = [n for n in c_names if os.path.isdir('C:\\' + n)]
    panel = icon_checks(existing, 'c-drive-panel', tree=False)
    for step in range(6):                 # page through the list (Users, Windows are near the end)
        if all(n in panel for n in existing):
            break
        keys('{PGDN}', 1.0)
        shot(f'c-drive-folders-page{step + 2}')
        icon_checks(existing, f'c-drive-panel-page{step + 2}', tree=False, found=panel)
    keys('{HOME}', 0.8)
    missing = [n for n in existing if n not in panel or panel[n]['yellow'] < 0.05]
    record('Every folder on C:\\ has its folder icon in the panel (also Windows, PerfLogs, inetpub)',
           bool(panel) and not missing, f'icon colours: {panel}; without icon: {missing}', must=bool(panel))
    tree_icons = icon_checks(existing, 'c-drive-tree', tree=True)
    try:                                   # scroll the tree to its end
        tr = [t.rectangle() for t in main_w.descendants(control_type='Tree') if t.window_text() == 'Folder tree']
        for step in range(10):
            if not tr or all(n in tree_icons for n in existing):
                break
            mouse.scroll(coords=((tr[0].left + tr[0].right) // 2, (tr[0].top + tr[0].bottom) // 2), wheel_dist=-3)
            time.sleep(0.8)
            shot(f'c-drive-tree-scroll{step + 1}')
            icon_checks(existing, f'c-drive-tree-scroll{step + 1}', tree=True, found=tree_icons)
    except Exception as e:  # noqa: BLE001
        print('tree scroll failed:', e)
    missing = [n for n in existing if n not in tree_icons or tree_icons[n]['yellow'] < 0.05]
    record('Every folder on C:\\ has its folder icon in the folder tree', bool(tree_icons) and not missing,
           f'icon colours: {tree_icons}; without icon: {missing}', must=bool(tree_icons))

    if VM and os.environ.get('OneDrive') and os.path.isdir(os.environ['OneDrive']):
        # read only: the user's real OneDrive is found and listed (nothing is opened or changed)
        lists = [l for l in main_w.descendants(control_type='List') if l.window_text() == 'Cloud folders']
        names = [li.window_text() for li in main_w.descendants(control_type='ListItem')
                 if lists and lists[0].rectangle().top <= li.rectangle().top <= lists[0].rectangle().bottom]
        if lists:
            r = lists[0].rectangle()
            crop_shot('vm-cloud-section', (r.left - 4, r.top - 30, r.left + 240, r.bottom + 6), 3)
        record('The sidebar\'s Cloud section lists the real OneDrive', any('OneDrive' in n for n in names),
               f'Cloud section: {names}')

    if cloud_ok:
        # the Cloud section lists the sync folder (found in the registry), with its name
        entries = [li for li in main_w.descendants(control_type='ListItem') if li.window_text() == CLOUD_NAME]
        lists = [l.window_text() for l in main_w.descendants(control_type='List')]
        record('The sidebar\'s Cloud section lists the sync folder by its name', bool(entries),
               f'lists: {lists}', must=True)
        if entries:
            r = entries[0].rectangle()
            crop_shot('cloud-section', (r.left - 4, r.top - 30, r.left + 240, r.bottom + 6), 3)
            entries[0].click_input()
            time.sleep(2.0)
        shot('cloud-folder-badges')
        record('Clicking the cloud entry opens the sync folder in the active panel', title() == CLOUD.name,
               f'title "{title()}"', must=True)
        # browse: every row painted, the cursor over every file, the folder entered and left
        ORDER = ['..', 'Shared', 'kept.txt', 'local-copy.txt', 'noext', 'notes.txt', 'photo.jpg', 'report.pdf', 'tool.exe']
        keys('{HOME}', 0.3)
        for _ in ORDER[1:]:
            keys('{DOWN}', 0.25)
        go_to('Shared')
        keys('{ENTER}', 2.0)
        shot('cloud-subfolder')
        keys('{BACKSPACE}', 2.0)
        rows = [el for el in main_w.descendants() if el.window_text() in ('notes', 'report', 'kept', 'local-copy', 'Shared')
                and side_right < el.rectangle().left < mid]
        if rows:
            top = min(el.rectangle().top for el in rows)
            crop_shot('cloud-badges-zoom', (side_right, top - 30, mid, top + 240), 2)
        time.sleep(2)
        got = fetches()
        record('Browsing the sync folder downloads nothing (no FETCH_DATA from Panero)', not got,
               f'fetch requests by Panero: {got}', must=True)
        record('The placeholders stay online-only after browsing', not still_placeholders(),
               f'downloaded: {still_placeholders()}', must=True)
        # preview 13: a folder gets the "available" tick only when all its content is local;
        # "Shared" holds an online-only placeholder, so its badge is the (blue) cloud
        time.sleep(2.0)                       # the folder scan runs in the background
        shared = [el.rectangle() for el in main_w.descendants() if el.window_text() == 'Shared'
                  and side_right < el.rectangle().left < mid]
        if shared:
            r = shared[0]
            cy = (r.top + r.bottom) // 2
            badge = icon_colour((r.left + 1, cy - 9, r.left + 27, cy + 9))
            crop_shot('p13-cloud-folder-badge', (r.left - 4, cy - 14, r.left + 160, cy + 14), 4)
            record('A sync folder with online-only content shows the cloud badge, not the tick', badge['blue'] > 0.02,
                   f'icon colours of "Shared": {badge}')

        # Explorer's offline actions on a file and on a folder
        for name, rel in (('report.pdf', 'report.pdf'), ('Shared', 'Shared')):
            go_to(name)
            keys('{VK_APPS}', 0.5)
            texts = wait_menu(proc.pid)
            time.sleep(0.5)
            shot(f'cloud-menu-{name}')
            ok = click_menu(proc.pid, 'Always Keep on This Device')
            a = wattrs(rel)
            child = wattrs('Shared/plan.docx') if name == 'Shared' else a
            record(f'"Always Keep on This Device" on {name} sets PINNED, clears UNPINNED'
                   + (' (also inside the folder)' if name == 'Shared' else ''),
                   ok and a & PINNED and not a & UNPINNED and child & PINNED,
                   f'menu: {texts}; attributes 0x{a:08x}, inside 0x{child:08x}', must=True)
            shot(f'cloud-always-keep-{name}')
            keys('{VK_APPS}', 0.5)
            wait_menu(proc.pid)
            ok = click_menu(proc.pid, 'Free Up Space')
            a = wattrs(rel)
            child = wattrs('Shared/plan.docx') if name == 'Shared' else a
            record(f'"Free Up Space" on {name} sets UNPINNED, clears PINNED', ok and a & UNPINNED and not a & PINNED
                   and child & UNPINNED, f'attributes 0x{a:08x}, inside 0x{child:08x}', must=True)
            shot(f'cloud-free-up-{name}')
        # Explorer's menu on several placeholders and on the folder's background
        go_to('notes.txt')
        keys('{INSERT}{INSERT}{INSERT}', 0.4)   # notes.txt, photo.jpg, report.pdf
        keys('{VK_APPS}', 0.5)
        texts = wait_menu(proc.pid)
        time.sleep(0.8)
        shot('p12-explorer-menu-cloud-several')
        have = {norm(t) for t in texts}
        record('Explorer menu on several placeholders: shell items, Panero\'s extras and the offline actions',
               all(x in have for x in ('send to', 'properties', 'copy to other panel', 'always keep on this device',
                                       'free up space')), f'menu: {texts}', must=True)
        close_menus()
        keys('%-', 0.4)
        view = panel_view_rect(main_w, win)
        if view:
            hold_right(view.left + 80, view.bottom - 30)
        texts = wait_menu(proc.pid)
        time.sleep(0.8)
        shot('p12-explorer-menu-cloud-background')
        have = {norm(t) for t in texts}
        record('Explorer menu on the sync folder\'s background', all(x in have for x in ('properties', 'panero menu')),
               f'menu: {texts}', must=True)
        close_menus()
        time.sleep(2)
        got = fetches()
        record('Context menus (Explorer\'s, on files, a folder, several, the background) and offline actions download nothing',
               not got, f'fetch requests by Panero: {got}; by any process: {fetches(by_panero=False)}', must=True)
        record('The placeholders stay online-only after the menus', not still_placeholders(),
               f'downloaded: {still_placeholders()}', must=True)

        # explicit open: F3 may download (as in Explorer) - and proves the provider sees reads
        go_to('notes.txt')
        before_wins = {w.handle for w in windows_of(proc.pid)}
        keys('{F3}', 3.0)
        wait_for(lambda: fetches(), 10)
        shot('cloud-f3-download')
        got = fetches()
        record('F3 on an online-only file downloads it (the provider is asked, as in Explorer)',
               any(e.get('file') == 'notes.txt' for e in got), f'fetch requests: {got}', must=True)
        for w in windows_of(proc.pid):
            if w.handle not in before_wins:
                try:
                    w.close()
                except Exception:  # noqa: BLE001
                    pass
        time.sleep(1)
        win.set_focus()

        # the folder holding the sync folder: a normal folder in the tree and the panel
        keys('{BACKSPACE}', 2.0)
        shot('profile-with-cloud-folder')
        prof = icon_checks([CLOUD.name], 'profile-tree', tree=True)
        record('The sync folder has its icon in the folder tree', bool(prof) and all(c['dark'] < 0.9 for c in prof.values()),
               f'{prof}')
        # right-click menu of the cloud entry: Pin, Open in Other Panel
        cloud_list = [l.rectangle() for l in main_w.descendants(control_type='List') if l.window_text() == 'Cloud folders']
        entries = [li for li in main_w.descendants(control_type='ListItem') if li.window_text() == CLOUD_NAME
                   and cloud_list and cloud_list[0].top <= li.rectangle().top <= cloud_list[0].bottom]
        if entries:
            r = entries[0].rectangle()
            mouse.right_click(coords=((r.left + r.right) // 2, (r.top + r.bottom) // 2))
            time.sleep(1.2)
            texts = [i.window_text() for i in menu_items(proc.pid)]
            shot('cloud-entry-menu')
            pin_clicked = click_menu(proc.pid, 'Pin')
            time.sleep(1)
            try:                               # the new pin is the last one: scroll the pins list down
                pl = [l.rectangle() for l in main_w.descendants(control_type='List') if l.window_text() == 'Pinned folders'][0]
                mouse.scroll(coords=((pl.left + pl.right) // 2, (pl.top + pl.bottom) // 2), wheel_dist=-10)
                time.sleep(0.8)
            except Exception:  # noqa: BLE001
                pass
            shot('cloud-pinned')
            mouse.right_click(coords=((r.left + r.right) // 2, (r.top + r.bottom) // 2))
            time.sleep(1.2)
            ok = click_menu(proc.pid, 'Open in Other Panel')
            time.sleep(1.5)
            record('Cloud entry menu: Open in Other Panel', ok and title() == CLOUD.name, f'title "{title()}"')
            shot('cloud-open-other-panel')
        time.sleep(1)
        record('Nothing else was downloaded (only the file opened with F3)',
               all(e.get('file') == 'notes.txt' for e in fetches()), f'fetch requests by Panero: {fetches()}', must=True)
    try:
        win.close()
        proc.wait(15)
    except Exception:  # noqa: BLE001
        proc.kill()
    if cloud_ok:
        # pins are saved in the settings (%LOCALAPPDATA%\panerorc, group "Panero Pins")
        rc = Path(os.environ['LOCALAPPDATA']) / 'panerorc'
        text = rc.read_text(encoding='utf-8', errors='replace') if rc.exists() else ''
        group = text.split('[Panero Pins]', 1)[1].split('\n[', 1)[0] if '[Panero Pins]' in text else ''
        record('Cloud entry menu: Pin adds it to Pinned (with its name)', 'pin_clicked' in globals() and pin_clicked
               and 'PaneroTestCloud' in group and CLOUD_NAME in group, f'[Panero Pins]: {group.strip()[:400]}', must=True)
if provider is not None:
    STOP.write_text('stop')
    try:
        provider.wait(30)
    except subprocess.TimeoutExpired:
        provider.kill()

# --- phase D (preview 13): Pack / Unpack with the bundled 7-Zip and Windows' tar, archives as
# folders, Explorer's drive menus, Quick Settings' right-click menu choice --------------------------
ARC = WORK / 'arc'
ARC_SRC, ARC_OUT = ARC / 'src', ARC / 'out'
shutil.rmtree(ARC, ignore_errors=True)
(ARC_SRC / 'sub').mkdir(parents=True)
ARC_OUT.mkdir()
for n in ('one.txt', 'two.txt'):
    (ARC_SRC / n).write_text(f'Panero archive test {n}\n')
(ARC_SRC / 'sub' / 'three.txt').write_text('three\n')
SEVENZIP = Path(EXE).parent / '7-Zip' / '7z.exe'
ARC_WANT = {'one.txt', 'two.txt', 'sub/three.txt'}


def archive_names(path):
    """Entries of an archive, as the bundled 7-Zip lists them (files and folders)."""
    try:
        out = subprocess.run([str(SEVENZIP), 'l', '-ba', '-slt', str(path)], capture_output=True, text=True,
                             timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return set()
    return {l[7:].strip().replace('\\', '/') for l in out.splitlines() if l.startswith('Path = ')}


def new_dialog(before, timeout=10):
    found = []
    wait_for(lambda: found.extend(w for w in windows_of(proc.pid) if w.handle not in before and not is_main(w))
             or found, timeout)
    return found[0] if found else None


PACK_TYPES = ('tar', 'tar.gz', 'tar.bz2', 'tar.lzma', 'tar.xz', 'zip', 'cbz', 'rar', 'cbr', 'lha', 'arj', '7z')


def combo_text(c):
    for get in (lambda: c.selected_text(), lambda: c.window_text(), lambda: c.get_value()):
        try:
            t = get()
            if t:
                return t
        except Exception:  # noqa: BLE001
            pass
    return ''


def choose_in_combo(dlg, text, candidates=PACK_TYPES):
    """Selects @p text in the dialog's combo box that offers it (the packer type).
    UI Automation's select() can mark an item without QComboBox switching to it,
    so type the name into the focused combo box (Qt jumps to the matching item)
    and check what it shows."""
    for c in dlg.descendants(control_type='ComboBox'):
        if combo_text(c) not in candidates:
            continue
        try:
            # open its list (focusing the closed box over UI Automation did not
            # move the keyboard focus: the typing went into the name field)
            c.click_input()
            time.sleep(0.6)
            keys(text.replace('+', '{+}'), 0.5)
            keys('{ENTER}', 0.6)
            if combo_text(c) == text:
                return True
            keys('{ESC}', 0.3)
        except Exception:  # noqa: BLE001
            pass
    for c in dlg.descendants(control_type='ComboBox'):
        try:
            c.select(text)
            if c.selected_text() == text or c.window_text() == text:
                return True
        except Exception:  # noqa: BLE001
            pass
    for c in dlg.descendants(control_type='ComboBox'):   # fallback: open it and click the item
        try:
            c.click_input()
            time.sleep(0.6)
            for w in Desktop(backend='uia').windows(process=proc.pid):
                for li in w.descendants(control_type='ListItem'):
                    if li.window_text() == text:
                        li.click_input()
                        time.sleep(0.4)
                        return True
            keys('{ESC}', 0.3)
        except Exception:  # noqa: BLE001
            pass
    return False


def pack_as(kind):
    before = {w.handle for w in windows_of(proc.pid)}
    keys('%{F5}', 1.5)
    dlg = new_dialog(before)
    if dlg is None:
        return f'no Pack dialog; windows {[w.window_text() for w in windows_of(proc.pid)]}'
    chosen = choose_in_combo(dlg, kind)
    shot(f'p13-pack-dialog-{kind}')
    how = press_button(proc.pid, 'OK')
    return f'dialog "{dlg.window_text()}", type chosen: {chosen}, OK {how}'


def unpack_to(name, dest):
    global ORDER
    ORDER = panel_order(ARC_OUT)
    go_to(name)
    before = {w.handle for w in windows_of(proc.pid)}
    keys('%{F9}', 1.5)
    dlg = new_dialog(before)
    if dlg is None:
        return 'no Unpack dialog'
    shot(f'p13-unpack-dialog-{name}')
    keys('^a', 0.2)
    keys(str(dest).replace('\\', '/'), 0.3)
    keys('{ENTER}', 0.5)
    return f'dialog "{dlg.window_text()}"'


def unpacked_ok(dest):
    try:
        return (dest / 'one.txt').read_text() == 'Panero archive test one.txt\n' \
            and (dest / 'sub' / 'three.txt').read_text() == 'three\n'
    except OSError:
        return False


proc = start(['--left', str(ARC_SRC), '--right', str(ARC_OUT)], 'archives')
app, win, main_w = None, None, None
for _ in range(60):
    time.sleep(1)
    mains = [w for w in windows_of(proc.pid) if is_main(w)]
    if mains:
        app = Application(backend='uia')
        app.connect(process=proc.pid)
        win = app.window(handle=mains[0].handle)
        main_w = Desktop(backend='uia').window(handle=mains[0].handle).wrapper_object()
        break
if win is None:
    record('Panero starts for the archive checks', False, 'no main window', must=True)
    proc.kill()
else:
    time.sleep(3)
    win.set_focus()
    title = lambda: app.top_window().window_text()
    if title() != 'src':
        keys('{TAB}', 0.8)
    record('Bundled 7-Zip is in the Panero folder', SEVENZIP.exists(), str(SEVENZIP), must=True)
    try:
        # mark everything, pack as zip, 7z and tar.gz into the other panel (the dialog's default)
        ORDER = panel_order(ARC_SRC)
        go_to('sub')
        keys('{INSERT}{INSERT}{INSERT}', 0.5)
        for kind, must in (('zip', True), ('7z', True), ('tar.gz', False)):
            note = pack_as(kind)
            target = ARC_OUT / f'src.{kind}'
            ok = wait_for(lambda: target.exists() and ARC_WANT <= archive_names(target), 40)
            time.sleep(1.0)
            shot(f'p13-packed-{kind}')
            record(f'Pack (Alt+F5) as {kind} on Windows makes the archive with the marked files and folders', ok,
                   f'{note}; {target.name}: {sorted(archive_names(target)) if target.exists() else "missing"}',
                   must=must)
            for w in windows_of(proc.pid):           # an error box would block the next step
                if not is_main(w):
                    record(f'No dialog left open after packing {kind}', False, w.window_text())
                    try:
                        w.set_focus()
                        keys('{ESC}', 0.5)
                    except Exception:  # noqa: BLE001
                        pass
        win.set_focus()
        keys('%-', 0.4)
        # unpack each into its own folder (Alt+F9, the path typed into the dialog)
        keys('{TAB}', 1.0)
        if title() != 'out':
            keys('{TAB}', 1.0)
        keys('^r', 1.5)                               # the right panel shows the new archives
        for kind, must in (('zip', True), ('7z', True), ('tar.gz', False)):
            if not (ARC_OUT / f'src.{kind}').exists():
                continue
            dest = ARC / f'un-{kind}'
            dest.mkdir(exist_ok=True)
            note = unpack_to(f'src.{kind}', dest)
            ok = wait_for(lambda: unpacked_ok(dest), 40)
            shot(f'p13-unpacked-{kind}')
            record(f'Unpack (Alt+F9) of a {kind} on Windows restores the files and folders', ok,
                   f'{note}; {dest.name}: {sorted(str(p.relative_to(dest)) for p in dest.rglob("*"))}', must=must)
            for w in windows_of(proc.pid):
                if not is_main(w):
                    record(f'No dialog left open after unpacking {kind}', False, w.window_text())
                    try:
                        w.set_focus()
                        keys('{ESC}', 0.5)
                    except Exception:  # noqa: BLE001
                        pass
            win.set_focus()
        # Enter on an archive opens it as a folder (krarc + 7-Zip)
        for kind in ('zip', '7z'):
            ORDER = panel_order(ARC_OUT)
            go_to(f'src.{kind}')
            keys('{ENTER}', 3.0)
            t = title()
            rows = [w.window_text() for w in win.descendants() if w.window_text() in ('one', 'two', 'sub')]
            shot(f'p13-archive-browse-{kind}')
            record(f'Enter on a {kind} opens it as a folder (krarc with the bundled 7-Zip)',
                   t.startswith(f'src.{kind}') and {'one', 'two', 'sub'} <= set(rows),
                   f'title "{t}"; rows {sorted(set(rows))}', must=True)
            for w in windows_of(proc.pid):
                if not is_main(w):
                    try:
                        w.set_focus()
                        keys('{ESC}', 0.5)
                    except Exception:  # noqa: BLE001
                        pass
            win.set_focus()
            if t.startswith(f'src.{kind}'):
                keys('{BACKSPACE}', 2.0)
    except Exception as e:  # noqa: BLE001
        record('Archive checks', False, f'automation error: {e!r}', must=True)
        close_menus()

    # Explorer's drive menu (Properties, Format, Eject, BitLocker, ...) with Panero's items on top
    def drive_menu_check(label, opener, panero_need):
        try:
            win.set_focus()
            opener()
            texts = wait_menu(proc.pid, want=tuple(panero_need[:1]))
            time.sleep(0.8)
            shot(f'p13-drive-menu-{label}')
            have = {norm(t) for t in texts}
            missing = [x for x in panero_need + ['properties'] if x not in have]
            record(f'Drive menu ({label}): Explorer\'s drive menu with Panero\'s {panero_need} on top', not missing,
                   f'missing: {missing}; menu: {texts}', must=True)
        except Exception as e:  # noqa: BLE001
            record(f'Drive menu ({label})', False, f'automation error: {e!r}', must=True)
        close_menus()

    def c_drive_items(control_type, where=None):
        out = []
        for el in main_w.descendants(control_type=control_type):
            try:
                if '(C:)' in el.window_text() and (where is None or where(el)):
                    out.append(el)
            except Exception:  # noqa: BLE001
                pass
        return sorted(out, key=lambda el: el.rectangle().left)

    def right_click(el):
        r = el.rectangle()
        mouse.right_click(coords=((r.left + r.right) // 2, (r.top + r.bottom) // 2))
        time.sleep(1.0)

    def drive_list():
        keys('%{F1}', 1.2)
        items = []
        for w in Desktop(backend='uia').windows(process=proc.pid):
            try:
                items += [i for i in w.descendants(control_type='MenuItem') if '(C:)' in i.window_text()]
            except Exception:  # noqa: BLE001
                pass
        if items:
            right_click(items[0])
    drive_menu_check('drive-list', drive_list, ['open in other panel', 'open in a new tab'])

    def path_bar_button():
        buttons = c_drive_items('Button', lambda el: el.rectangle().height() < 40)
        if buttons:
            right_click(buttons[0])
    drive_menu_check('path-bar-button', path_bar_button, ['open in other panel', 'open in a new tab'])

    def pinned_drive():
        pins = c_drive_items('ListItem')
        if pins:
            try:
                pins[0].iface_scroll_item.ScrollIntoView()
                time.sleep(0.4)
            except Exception:  # noqa: BLE001  (no ScrollItem pattern: visible already)
                pass
            right_click(pins[0])
    drive_menu_check('pinned-drive', pinned_drive, ['open in other panel', 'unpin'])

    # Quick Settings: drive buttons on; the right-click menu row (Windows only)
    def quick_settings():
        before = {w.handle for w in windows_of(proc.pid)}
        qs = [b for b in main_w.descendants() if b.window_text() == 'Quick Settings'
              and b.element_info.control_type in ('Button', 'MenuItem', 'SplitButton')]
        if not qs:
            return None
        qs[0].click_input()
        return new_dialog(before)

    # Quick Settings (preview 13) is a popup that applies each change at once;
    # Esc closes it. Its "Right-click:" box offers the Explorer menu or Panero's.
    RIGHT_CLICK = ('Explorer menu (default)', 'Panero menu')
    try:
        dlg = quick_settings()
        if dlg is None:
            record('Quick Settings opens', False, 'no Quick Settings button or popup', must=True)
        else:
            combos = [combo_text(c) for c in dlg.descendants(control_type='ComboBox')]
            labels = [t.window_text() for t in dlg.descendants(control_type='Text')]
            shot('p13-quick-settings')
            record('Quick Settings has the "Right-click" row (Explorer menu default / Panero menu)',
                   'Right-click:' in labels and 'Explorer menu (default)' in combos,
                   f'labels: {labels}; combos: {combos}', must=True)
            for cb in dlg.descendants(control_type='CheckBox'):
                if cb.window_text() == 'Drive buttons' and not cb.get_toggle_state():
                    cb.click_input()
                    time.sleep(0.6)
            keys('{ESC}', 1.5)

            def drive_button():
                # the drive bar's button, not the path bar's drive list button (MediaButton)
                buttons = [b for b in c_drive_items('Button') if b.rectangle().height() < 40
                           and b.element_info.class_name != 'MediaButton']
                if buttons:
                    right_click(buttons[0])
            drive_menu_check('drive-button', drive_button, ['open in other panel', 'hide this button'])

            # "Panero menu": files get Panero's own menu again (no Explorer items)
            dlg = quick_settings()
            if dlg is not None:
                chosen = choose_in_combo(dlg, 'Panero menu', RIGHT_CLICK)
                shot('p13-quick-settings-panero-menu')
                keys('{ESC}', 1.0)
                win.set_focus()
                keys('{TAB}', 0.8)
                if title() != 'src':
                    keys('{TAB}', 0.8)
                ORDER = panel_order(ARC_SRC)
                go_to('one.txt')
                keys('{VK_APPS}', 0.5)
                texts = wait_menu(proc.pid, want=('move to recycle bin',))
                shot('p13-panero-menu-chosen')
                have = {norm(t) for t in texts}
                record('Quick Settings "Panero menu": the right-click menu is Panero\'s own (no Explorer items)',
                       'move to recycle bin' in have and 'send to' not in have and 'panero menu' not in have,
                       f'chosen: {chosen}; menu: {texts}', must=True)
                close_menus()
    except Exception as e:  # noqa: BLE001
        record('Quick Settings checks', False, f'automation error: {e!r}', must=True)
        close_menus()
    try:
        win.close()
        proc.wait(15)
    except Exception:  # noqa: BLE001
        proc.kill()


if VM:
    removed = purge_own_recycled()
    record('VM: the items this test deleted are removed from the Recycle Bin again (nothing else)',
           not recycled_from_work(), f'removed: {removed}')

sys.exit(1 if any(r['status'] == 'FAIL' for r in results) else 0)
