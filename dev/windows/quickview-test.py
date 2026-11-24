# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Quick View (D-022) on Windows: Ctrl+Q turns the right panel into a viewer of
the left panel's cursor; every file type must show its page (Panero's
PANERO_QUICKVIEW_TRACE lines on stderr); fast movement loads once; Tab/Esc
move the focus; Ctrl+Q again shows the right panel unchanged.

Run through vm-gui-test.ps1 on the developer VM (only files in --work are
touched, the test profile is Qt's test location):
  dev/windows/vm.sh gui <name> dev/windows/vm-gui-test.ps1 -Script quickview-test.py -OutDir results\\quickview
or directly on a disposable runner with PANERO_EXE set."""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
import wave
import zipfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageGrab
from pywinauto import Desktop, keyboard, mouse

ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
ap.add_argument('--vm', action='store_true')
ap.add_argument('--work', default=r'C:\pt\quickview')
args = ap.parse_args()
OUT = Path(args.out)
OUT.mkdir(parents=True, exist_ok=True)
EXE = os.environ['PANERO_EXE']
WORK = Path(args.work).resolve()
L, R = WORK / 'L', WORK / 'R'
results = []


def record(check, ok, note='', info=False):
    status = 'PASS' if ok else ('NOTE' if info else 'FAIL')
    results.append((status, check, note))
    print(f'{status}  {check}' + (f' ({note})' if note else ''), flush=True)


def shot(name):
    ImageGrab.grab().save(OUT / f'{name}.png')


def keys(seq, pause=0.5):
    keyboard.send_keys(seq, pause=0.04, with_spaces=True)
    time.sleep(pause)


# ---- disposable files (sorted: folder, then 1..8)
shutil.rmtree(WORK, ignore_errors=True)
(L / '0-Project' / 'src').mkdir(parents=True)
R.mkdir(parents=True)
(L / '0-Project' / 'src' / 'blob.bin').write_bytes(os.urandom(300000))
(L / '0-Project' / 'readme.md').write_bytes(os.urandom(20000))
img = Image.new('RGB', (1600, 1000), (30, 58, 138))
ImageDraw.Draw(img).rectangle((200, 300, 1400, 700), fill=(245, 158, 11))
img.save(L / '1-photo.jpg', quality=85)
frames = [Image.new('RGB', (160, 160), c) for c in ((220, 38, 38), (22, 163, 74), (37, 99, 235))]
frames[0].save(L / '2-anim.gif', save_all=True, append_images=frames[1:], duration=250, loop=0)
(L / '3-code.cpp').write_text('// Quick View\n#include <iostream>\nint main() { std::cout << "hi\\n"; return 0; }\n')
(L / '4-notes.txt').write_bytes('Größe und Gewicht: schön, Straße, Ärger.\n'.encode('cp1252'))
with zipfile.ZipFile(L / '5-archive.zip', 'w') as z:
    z.writestr('docs/readme.txt', 'hello from the zip\n')
    z.writestr('image/data.bin', os.urandom(2048))
(L / '6-doc.pdf').write_bytes(b'%PDF-1.4\n% no pages\n')
w = wave.open(str(L / '7-sound.wav'), 'wb')
w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
w.writeframes(b''.join(struct.pack('<h', (i * 37) % 16000 - 8000) for i in range(22050)))
w.close()
(L / '8-data.bin').write_bytes(os.urandom(5000))
for i in range(3):
    (R / f'right file {i}.txt').write_text(f'right {i}\n')

app_dir = Path(EXE).parent
has_media = (app_dir / 'Qt5Multimedia.dll').exists() and (app_dir / 'plugins' / 'mediaservice').exists()
has_syntax = (app_dir / 'libKF5SyntaxHighlighting.dll').exists()
record('Qt Multimedia in the package', has_media, 'player' if has_media else 'file info + Open instead', info=True)
record('KSyntaxHighlighting in the package', has_syntax, info=True)

env = dict(os.environ)
if 'PANERO_CLEAN_PATH' in os.environ:
    env['PATH'] = os.environ['PANERO_CLEAN_PATH']
env['PANERO_QUICKVIEW_TRACE'] = '1'
LOG = OUT / 'stderr-quickview.txt'
log = open(LOG, 'w')
proc = subprocess.Popen([EXE, f'--left={L}', f'--right={R}'], env=env, stdout=log, stderr=log, cwd=str(WORK))


def main_window():
    for _ in range(60):
        for w in Desktop(backend='uia').windows():
            try:
                if w.process_id() == proc.pid and w.is_visible() and w.rectangle().width() > 500:
                    return w
            except Exception:  # noqa: BLE001
                pass
        time.sleep(0.5)
    return None


def trace():
    log.flush()
    try:
        return [line.strip()[len('quickview: '):] for line in LOG.read_text(errors='replace').splitlines()
                if line.startswith('quickview: ')]
    except OSError:
        return []


def wait_page(page, item, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        pages = [t for t in trace() if t.startswith('page=')]
        if pages and pages[-1] == f'page={page} item={item}':
            return True
        time.sleep(0.25)
    return False


code = 1
try:
    win = main_window()
    record('main window', win is not None)
    if win is None:
        raise SystemExit(1)
    time.sleep(4)
    win.set_focus()
    r = win.rectangle()
    # the left panel's file list: click its first row area
    mouse.click(coords=(r.left + int(r.width() * 0.40), r.top + int(r.height() * 0.22)))
    time.sleep(0.6)
    keys('{HOME}')
    shot('00-panels')
    before = ImageGrab.grab(bbox=(r.left + int(r.width() * 0.66), r.top + int(r.height() * 0.25),
                                  r.right - 20, r.top + int(r.height() * 0.75)))
    keys('^q', 1.0)
    record('Ctrl+Q opens Quick View', 'opened' in trace())
    steps = [('folder', '0-Project'), ('image', '1-photo.jpg'), ('image', '2-anim.gif'), ('text', '3-code.cpp'),
             ('text', '4-notes.txt'), ('archive', '5-archive.zip'), ('info', '6-doc.pdf'),
             ('media' if has_media else 'info', '7-sound.wav'), ('info', '8-data.bin')]
    for n, (page, item) in enumerate(steps, 1):
        keys('{DOWN}', 0.3)
        ok = wait_page(page, item)
        time.sleep(0.6)
        shot(f'{n:02d}-{item}')
        record(f'{item}: {page} page', ok, ([t for t in trace() if t.startswith('page=')] or ['-'])[-1])
    record('folder size calculated', any(t.startswith('folder done item=0-Project files=2 folders=1') for t in trace()))

    n0 = len(trace())
    keyboard.send_keys('{HOME}{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}', pause=0.03)
    time.sleep(1.5)
    loads = [t for t in trace()[n0:] if t.startswith('load item=')]
    record('fast movement loads once', len(loads) == 1, ', '.join(loads))
    record('stopped on 4-notes.txt', wait_page('text', '4-notes.txt'))

    keys('{TAB}')
    record('Tab: focus in the viewer', trace()[-1:] == ['focus=view'], str(trace()[-2:]))
    keys('{ESC}')
    record('Esc: focus back in the panel', trace()[-1:] == ['focus=panel'], str(trace()[-2:]))
    keys('^q', 1.5)
    record('Ctrl+Q closes Quick View', 'closed' in trace()[-2:])
    win.set_focus()
    after = ImageGrab.grab(bbox=(r.left + int(r.width() * 0.66), r.top + int(r.height() * 0.25),
                                 r.right - 20, r.top + int(r.height() * 0.75)))
    shot('20-closed')
    diff = ImageChops.difference(before.convert('RGB'), after.convert('RGB')).getbbox()
    record('right panel restored', diff is None, f'changed area {diff}')
    code = 0 if all(s != 'FAIL' for s, _, _ in results) else 1
finally:
    try:
        import psutil
        for child in psutil.Process(proc.pid).children(recursive=True):
            child.kill()
    except Exception:  # noqa: BLE001
        pass
    proc.kill()
    log.close()
    with open(OUT / 'quickview-results.md', 'w', encoding='utf-8') as f:
        f.write('| Result | Check | Note |\n|---|---|---|\n')
        for s, c, n in results:
            f.write(f'| {s} | {c} | {n} |\n')
    shutil.rmtree(WORK, ignore_errors=True)
sys.exit(code)
