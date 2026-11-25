# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Foreground guard for GUI tests on a real Windows desktop (the developer VM).

The VM is somebody's desktop: other applications may be open and in front.
A test started from a scheduled task cannot always take the foreground, and
pywinauto's key presses and clicks go to whatever window is in front - they
could type into the owner's applications. With PANERO_VM_GUARD=1 (set by
vm-gui-test.ps1) every key press and click first checks the foreground
window: it must belong to panero.exe (its dialogs included) or be a shell
property/confirmation dialog. Otherwise the guard brings Panero's window to
the front once; if that fails the test stops with a clear message instead of
sending input anywhere else. Without the variable (CI runners) nothing changes.
"""
import ctypes
import os
import sys
import time
from ctypes import wintypes

ENABLED = os.environ.get('PANERO_VM_GUARD') == '1'
ALLOWED = {'panero.exe'}
# shell dialogs the tests open on purpose (Properties, file operation questions)
ALLOWED_DIALOG_HOSTS = {'explorer.exe', 'rundll32.exe', 'dllhost.exe'}
DIALOG_CLASS = '#32770'

user32 = ctypes.WinDLL('user32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def _process_name(pid):
    h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return ''
    try:
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(len(buf))
        if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            return os.path.basename(buf.value).lower()
        return ''
    finally:
        kernel32.CloseHandle(h)


def _window_info(hwnd):
    pid = wintypes.DWORD()
    tid = user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    cls = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, cls, 256)
    return pid.value, tid, _process_name(pid.value), cls.value


def foreground_ok():
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return False
    _, _, name, cls = _window_info(hwnd)
    return name in ALLOWED or (name in ALLOWED_DIALOG_HOSTS and cls == DIALOG_CLASS)


def _panero_window():
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd) and _window_info(hwnd)[2] in ALLOWED:
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None  # EnumWindows goes top to bottom


def _bring_to_front(hwnd):
    fg = user32.GetForegroundWindow()
    fg_tid = user32.GetWindowThreadProcessId(fg, None) if fg else 0
    me = kernel32.GetCurrentThreadId()
    attached = bool(fg_tid) and fg_tid != me and user32.AttachThreadInput(me, fg_tid, True)
    try:
        user32.ShowWindow(hwnd, 9)  # SW_RESTORE
        user32.BringWindowToTop(hwnd)
        user32.SetForegroundWindow(hwnd)
    finally:
        if attached:
            user32.AttachThreadInput(me, fg_tid, False)


def ensure():
    """Raise SystemExit when input would reach another application."""
    if not ENABLED or foreground_ok():
        return
    hwnd = _panero_window()
    if hwnd:
        _bring_to_front(hwnd)
        time.sleep(0.4)
        if foreground_ok():
            return
    hwnd = user32.GetForegroundWindow()
    name = _window_info(hwnd)[2] if hwnd else 'none'
    print(f'STOPPED: another application ({name}) is in front of Panero on the VM desktop; '
          'no keys or clicks were sent to it. Close or minimise it and run the test again.',
          flush=True)
    sys.exit(3)


def install():
    """Wrap pywinauto's keyboard and mouse input with ensure()."""
    if not ENABLED:
        return
    from pywinauto import keyboard, mouse
    original_keys = keyboard.send_keys

    def guarded_keys(*args, **kwargs):
        ensure()
        return original_keys(*args, **kwargs)

    keyboard.send_keys = guarded_keys
    original_click = mouse._perform_click_input

    def guarded_click(*args, **kwargs):
        ensure()
        return original_click(*args, **kwargs)

    mouse._perform_click_input = guarded_click
