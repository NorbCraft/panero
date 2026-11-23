# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""A tiny Cloud Files sync provider for the Windows GUI test (D-020).

Registers a real sync root with the Cloud Files API (cldapi.dll, the API
OneDrive uses), fills it with placeholder files whose content is "in the
cloud", and stays connected: whenever any program reads a placeholder, Windows
calls our FETCH_DATA callback (that is what downloads a OneDrive file). Every
callback is logged with the process that caused it, so the GUI test can prove
that browsing with Panero never downloads anything, and that opening a file
explicitly (F3) does.

    python cloud-provider.py --root C:/Users/me/PaneroTestCloud --log fetch.jsonl
                             --ready ready.json --stop stop.flag

Writes <ready> (sync root info, attributes, how it was registered) once the
placeholders exist; runs until <stop> exists; then disconnects and unregisters.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import winreg

ap = argparse.ArgumentParser()
ap.add_argument('--root', required=True)
ap.add_argument('--log', required=True)
ap.add_argument('--ready', required=True)
ap.add_argument('--stop', required=True)
ap.add_argument('--name', default='Panero Test Cloud')
args = ap.parse_args()

ROOT = os.path.abspath(args.root)
cld = ctypes.WinDLL('cldapi.dll')
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
HRESULT = ctypes.c_long
LI = ctypes.c_longlong


class GUID(ctypes.Structure):
    _fields_ = [('Data1', wt.DWORD), ('Data2', wt.WORD), ('Data3', wt.WORD), ('Data4', ctypes.c_ubyte * 8)]


class CF_SYNC_REGISTRATION(ctypes.Structure):
    _fields_ = [('StructSize', wt.DWORD), ('ProviderName', wt.LPCWSTR), ('ProviderVersion', wt.LPCWSTR),
                ('SyncRootIdentity', ctypes.c_void_p), ('SyncRootIdentityLength', wt.DWORD),
                ('FileIdentity', ctypes.c_void_p), ('FileIdentityLength', wt.DWORD), ('ProviderId', GUID)]


class CF_POLICY(ctypes.Structure):
    _fields_ = [('Primary', ctypes.c_ushort), ('Modifier', ctypes.c_ushort)]


class CF_SYNC_POLICIES(ctypes.Structure):
    _fields_ = [('StructSize', wt.DWORD), ('Hydration', CF_POLICY), ('Population', CF_POLICY),
                ('InSync', wt.DWORD), ('HardLink', wt.DWORD), ('PlaceholderManagement', wt.DWORD)]


class CF_PROCESS_INFO(ctypes.Structure):
    _fields_ = [('StructSize', wt.DWORD), ('ProcessId', wt.DWORD), ('ImagePath', wt.LPCWSTR),
                ('PackageName', wt.LPCWSTR), ('ApplicationId', wt.LPCWSTR), ('CommandLine', wt.LPCWSTR),
                ('SessionId', wt.DWORD)]


class CF_CALLBACK_INFO(ctypes.Structure):
    _fields_ = [('StructSize', wt.DWORD), ('ConnectionKey', LI), ('CallbackContext', ctypes.c_void_p),
                ('VolumeGuidName', wt.LPCWSTR), ('VolumeDosName', wt.LPCWSTR), ('VolumeSerialNumber', wt.DWORD),
                ('SyncRootFileId', LI), ('SyncRootIdentity', ctypes.c_void_p), ('SyncRootIdentityLength', wt.DWORD),
                ('FileId', LI), ('FileSize', LI), ('FileIdentity', ctypes.c_void_p), ('FileIdentityLength', wt.DWORD),
                ('NormalizedPath', wt.LPCWSTR), ('TransferKey', LI), ('PriorityHint', ctypes.c_ubyte),
                ('CorrelationVector', ctypes.c_void_p), ('ProcessInfo', ctypes.POINTER(CF_PROCESS_INFO)),
                ('RequestKey', LI)]


class CF_FETCH_DATA(ctypes.Structure):
    _fields_ = [('Flags', wt.DWORD), ('RequiredFileOffset', LI), ('RequiredLength', LI),
                ('OptionalFileOffset', LI), ('OptionalLength', LI), ('LastDehydrationTime', LI),
                ('LastDehydrationReason', ctypes.c_int)]


class CF_CALLBACK_PARAMETERS(ctypes.Structure):
    _fields_ = [('ParamSize', ctypes.c_ulong), ('FetchData', CF_FETCH_DATA)]


class CF_OPERATION_INFO(ctypes.Structure):
    _fields_ = [('StructSize', wt.DWORD), ('Type', ctypes.c_int), ('ConnectionKey', LI), ('TransferKey', LI),
                ('CorrelationVector', ctypes.c_void_p), ('SyncStatus', ctypes.c_void_p), ('RequestKey', LI)]


class CF_TRANSFER_DATA(ctypes.Structure):
    _fields_ = [('Flags', wt.DWORD), ('CompletionStatus', ctypes.c_long), ('Buffer', ctypes.c_void_p),
                ('Offset', LI), ('Length', LI)]


class CF_OPERATION_PARAMETERS(ctypes.Structure):
    _fields_ = [('ParamSize', ctypes.c_ulong), ('TransferData', CF_TRANSFER_DATA)]


class FILE_BASIC_INFO(ctypes.Structure):
    _fields_ = [('CreationTime', LI), ('LastAccessTime', LI), ('LastWriteTime', LI), ('ChangeTime', LI),
                ('FileAttributes', wt.DWORD)]


class CF_FS_METADATA(ctypes.Structure):
    _fields_ = [('BasicInfo', FILE_BASIC_INFO), ('FileSize', LI)]


class CF_PLACEHOLDER_CREATE_INFO(ctypes.Structure):
    _fields_ = [('RelativeFileName', wt.LPCWSTR), ('FsMetadata', CF_FS_METADATA), ('FileIdentity', ctypes.c_void_p),
                ('FileIdentityLength', wt.DWORD), ('Flags', wt.DWORD), ('Result', HRESULT), ('CreateUsn', LI)]


class CF_CALLBACK_REGISTRATION(ctypes.Structure):
    _fields_ = [('Type', ctypes.c_int), ('Callback', ctypes.c_void_p)]


CALLBACK = ctypes.WINFUNCTYPE(None, ctypes.POINTER(CF_CALLBACK_INFO), ctypes.POINTER(CF_CALLBACK_PARAMETERS))
cld.CfRegisterSyncRoot.restype = HRESULT
cld.CfRegisterSyncRoot.argtypes = [wt.LPCWSTR, ctypes.POINTER(CF_SYNC_REGISTRATION), ctypes.POINTER(CF_SYNC_POLICIES), wt.DWORD]
cld.CfUnregisterSyncRoot.restype = HRESULT
cld.CfUnregisterSyncRoot.argtypes = [wt.LPCWSTR]
cld.CfConnectSyncRoot.restype = HRESULT
cld.CfConnectSyncRoot.argtypes = [wt.LPCWSTR, ctypes.POINTER(CF_CALLBACK_REGISTRATION), ctypes.c_void_p, wt.DWORD,
                                  ctypes.POINTER(LI)]
cld.CfDisconnectSyncRoot.restype = HRESULT
cld.CfDisconnectSyncRoot.argtypes = [LI]
cld.CfCreatePlaceholders.restype = HRESULT
cld.CfCreatePlaceholders.argtypes = [wt.LPCWSTR, ctypes.POINTER(CF_PLACEHOLDER_CREATE_INFO), wt.DWORD, wt.DWORD,
                                     ctypes.POINTER(wt.DWORD)]
cld.CfExecute.restype = HRESULT
cld.CfExecute.argtypes = [ctypes.POINTER(CF_OPERATION_INFO), ctypes.POINTER(CF_OPERATION_PARAMETERS)]
k32.GetFileAttributesW.restype = wt.DWORD
k32.GetFileAttributesW.argtypes = [wt.LPCWSTR]
k32.SetFileAttributesW.restype = wt.BOOL
k32.SetFileAttributesW.argtypes = [wt.LPCWSTR, wt.DWORD]

CF_CALLBACK_TYPE_FETCH_DATA, CF_CALLBACK_TYPE_FETCH_PLACEHOLDERS = 0, 3
CF_CALLBACK_TYPE_NOTIFY_FILE_OPEN_COMPLETION, CF_CALLBACK_TYPE_NONE = 5, -1
PINNED, RECALL_ON_DATA_ACCESS = 0x80000, 0x400000

# what is "in the cloud": name -> content (folders end with "/")
CONTENT = {
    'Shared/': None,
    'Shared/plan.docx': b'PK\x03\x04 a placeholder inside a placeholder folder\n' * 40,
    'notes.txt': b'Notes kept in the cloud. Panero must not download them while browsing.\n' * 20,
    'report.pdf': b'%PDF-1.4\n% placeholder pdf\n' + b'0' * 9000,
    'photo.jpg': b'\xff\xd8\xff\xe0' + b'\x00' * 20000,
    'tool.exe': b'MZ' + b'\x00' * 5000,
    'noext': b'#!/bin/sh\necho no extension: Panero would sniff the content here\n',
    'local-copy.txt': b'This one is downloaded (available on this device).\n' * 10,
    'kept.txt': b'This one is always kept on this device.\n' * 10,
}

log_lock = threading.Lock()
os.makedirs(os.path.dirname(os.path.abspath(args.log)), exist_ok=True)
logf = open(args.log, 'a', encoding='utf-8')


def log(event):
    event['time'] = round(time.time(), 3)
    with log_lock:
        logf.write(json.dumps(event) + '\n')
        logf.flush()


def proc_of(info):
    try:
        p = info.contents.ProcessInfo
        if p:
            return {'pid': p.contents.ProcessId, 'image': p.contents.ImagePath or ''}
    except Exception:  # noqa: BLE001
        pass
    return {'pid': 0, 'image': ''}


def rel_of(info):
    path = info.contents.NormalizedPath or ''
    root_tail = os.path.splitdrive(ROOT)[1].replace('/', '\\')
    i = path.lower().find(root_tail.lower())
    return path[i + len(root_tail):].lstrip('\\').replace('\\', '/') if i >= 0 else path


keepalive = []


def on_fetch_data(info, params):
    try:
        rel = rel_of(info)
        fd = params.contents.FetchData
        log({'type': 'FETCH_DATA', 'file': rel, 'offset': fd.RequiredFileOffset, 'length': fd.RequiredLength,
             **proc_of(info)})
        data = CONTENT.get(rel) or b''
        buf = ctypes.create_string_buffer(data, len(data))
        op = CF_OPERATION_INFO(StructSize=ctypes.sizeof(CF_OPERATION_INFO), Type=0,  # TRANSFER_DATA
                               ConnectionKey=info.contents.ConnectionKey, TransferKey=info.contents.TransferKey,
                               RequestKey=info.contents.RequestKey)
        prm = CF_OPERATION_PARAMETERS(ParamSize=ctypes.sizeof(CF_OPERATION_PARAMETERS))
        prm.TransferData.Flags = 0
        prm.TransferData.CompletionStatus = 0 if data else -1073741823  # STATUS_UNSUCCESSFUL
        prm.TransferData.Buffer = ctypes.cast(buf, ctypes.c_void_p)
        prm.TransferData.Offset = 0
        prm.TransferData.Length = len(data)
        hr = cld.CfExecute(ctypes.byref(op), ctypes.byref(prm))
        log({'type': 'TRANSFER_DATA', 'file': rel, 'hr': hr & 0xffffffff})
    except Exception as e:  # noqa: BLE001
        log({'type': 'ERROR', 'where': 'fetch', 'error': repr(e)})


def on_fetch_placeholders(info, params):
    log({'type': 'FETCH_PLACEHOLDERS', 'file': rel_of(info), **proc_of(info)})


def on_open(info, params):
    log({'type': 'OPEN', 'file': rel_of(info), **proc_of(info)})


cb_fetch = CALLBACK(on_fetch_data)
cb_placeholders = CALLBACK(on_fetch_placeholders)
cb_open = CALLBACK(on_open)
table = (CF_CALLBACK_REGISTRATION * 4)(
    CF_CALLBACK_REGISTRATION(CF_CALLBACK_TYPE_FETCH_DATA, ctypes.cast(cb_fetch, ctypes.c_void_p)),
    CF_CALLBACK_REGISTRATION(CF_CALLBACK_TYPE_FETCH_PLACEHOLDERS, ctypes.cast(cb_placeholders, ctypes.c_void_p)),
    CF_CALLBACK_REGISTRATION(CF_CALLBACK_TYPE_NOTIFY_FILE_OPEN_COMPLETION, ctypes.cast(cb_open, ctypes.c_void_p)),
    CF_CALLBACK_REGISTRATION(CF_CALLBACK_TYPE_NONE, None))


def filetime_now():
    return int((time.time() + 11644473600) * 10_000_000)


def user_sid():
    out = subprocess.run(['whoami', '/user', '/fo', 'csv', '/nh'], capture_output=True, text=True).stdout
    return out.strip().split(',')[-1].strip('"')


def sync_root_keys():
    keys = []
    for hive, hname in ((winreg.HKEY_LOCAL_MACHINE, 'HKLM'), (winreg.HKEY_CURRENT_USER, 'HKCU')):
        try:
            with winreg.OpenKey(hive, r'Software\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager') as k:
                i = 0
                while True:
                    try:
                        keys.append(f'{hname}\\{winreg.EnumKey(k, i)}')
                    except OSError:
                        break
                    i += 1
        except OSError:
            pass
    return keys


def attrs(rel):
    return k32.GetFileAttributesW(os.path.join(ROOT, rel.rstrip('/')))


def register():
    reg = CF_SYNC_REGISTRATION()
    reg.StructSize = ctypes.sizeof(reg)
    reg.ProviderName = 'PaneroTest'
    reg.ProviderVersion = '1.0'
    ident = b'panero-test-root'
    reg.SyncRootIdentity = ctypes.cast(ctypes.c_char_p(ident), ctypes.c_void_p)
    reg.SyncRootIdentityLength = len(ident)
    reg.ProviderId = GUID(0x5a1e7e57, 0x1234, 0x4abc, (ctypes.c_ubyte * 8)(1, 2, 3, 4, 5, 6, 7, 8))
    pol = CF_SYNC_POLICIES()
    pol.StructSize = ctypes.sizeof(pol)
    pol.Hydration = CF_POLICY(2, 0)   # FULL: the whole file on first read (as OneDrive)
    pol.Population = CF_POLICY(3, 0)  # ALWAYS_FULL: we create every placeholder ourselves
    keepalive.append(ident)
    return cld.CfRegisterSyncRoot(ROOT, ctypes.byref(reg), ctypes.byref(pol), 0) & 0xffffffff


def create_placeholders(base, names):
    """Placeholders for the direct children @names (paths relative to ROOT) of the folder @base."""
    infos = (CF_PLACEHOLDER_CREATE_INFO * len(names))()
    idents = []
    now = filetime_now()
    for i, rel in enumerate(names):
        isdir = rel.endswith('/')
        ident = ctypes.create_unicode_buffer(rel)
        idents.append(ident)
        info = infos[i]
        info.RelativeFileName = rel.rstrip('/').split('/')[-1]
        bi = info.FsMetadata.BasicInfo
        bi.CreationTime = bi.LastAccessTime = bi.LastWriteTime = bi.ChangeTime = now
        bi.FileAttributes = 0x10 if isdir else 0x80
        info.FsMetadata.FileSize = 0 if isdir else len(CONTENT[rel])
        info.FileIdentity = ctypes.cast(ident, ctypes.c_void_p)
        info.FileIdentityLength = ctypes.sizeof(ident)
        info.Flags = 0x2 | (0x1 if isdir else 0)  # MARK_IN_SYNC (+ DISABLE_ON_DEMAND_POPULATION)
    done = wt.DWORD(0)
    hr = cld.CfCreatePlaceholders(base, infos, len(names), 0, ctypes.byref(done)) & 0xffffffff
    return hr, [infos[i].Result & 0xffffffff for i in range(len(names))]


shutil.rmtree(ROOT, ignore_errors=True)
os.makedirs(ROOT)
state = {'root': ROOT.replace('\\', '/'), 'name': args.name, 'keys_before': sync_root_keys()}
hr = register()
if hr != 0:
    # the Cloud Files filter may not be running on a server image
    state['filter_start'] = subprocess.run(['fltmc', 'load', 'cldflt'], capture_output=True, text=True).stdout.strip()
    hr = register()
state['register_hr'] = f'0x{hr:08x}'
if hr != 0:
    state['ok'] = False
    json.dump(state, open(args.ready, 'w'), indent=1)
    sys.exit(2)

key = LI(0)
hr = cld.CfConnectSyncRoot(ROOT, table, None, 0x2 | 0x4, ctypes.byref(key)) & 0xffffffff  # process info, full path
state['connect_hr'] = f'0x{hr:08x}'
top = [n for n in CONTENT if '/' not in n.rstrip('/')]
hr1, res1 = create_placeholders(ROOT, top)
hr2, res2 = create_placeholders(os.path.join(ROOT, 'Shared'), [n for n in CONTENT if n.startswith('Shared/') and n != 'Shared/'])
state['placeholders_hr'] = [f'0x{hr1:08x}', f'0x{hr2:08x}', [f'0x{r:08x}' for r in res1 + res2]]

# one file downloaded (read by this provider's own process), one also always kept
for rel in ('local-copy.txt', 'kept.txt'):
    with open(os.path.join(ROOT, rel), 'rb') as f:
        f.read()
k32.SetFileAttributesW(os.path.join(ROOT, 'kept.txt'), (attrs('kept.txt') | PINNED) & 0x1831a7)  # settable attributes

# registered sync roots show up in the SyncRootManager registry key when the
# shell integration registers them (StorageProviderSyncRootManager, as OneDrive
# does); CfRegisterSyncRoot alone may not: then add the same entry for this user
keys_after = sync_root_keys()
state['keys_after'] = keys_after
state['registry'] = 'api'
if not any(k.split('\\', 1)[1].startswith('PaneroTest!') for k in keys_after):
    sid = user_sid()
    path = rf'Software\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager\PaneroTest!{sid}!Test'
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path) as k:
        winreg.SetValueEx(k, 'DisplayNameResource', 0, winreg.REG_SZ, args.name)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path + r'\UserSyncRoots') as k:
        winreg.SetValueEx(k, sid, 0, winreg.REG_SZ, ROOT)
    state['registry'] = 'HKCU\\' + path
state['attributes'] = {n: f'0x{attrs(n):08x}' for n in CONTENT}
state['ok'] = True
log({'type': 'READY'})
json.dump(state, open(args.ready, 'w'), indent=1)

while not os.path.exists(args.stop):
    time.sleep(0.3)

state['attributes_at_stop'] = {n: f'0x{attrs(n):08x}' for n in CONTENT}
cld.CfDisconnectSyncRoot(key)
if state['registry'] != 'api':
    try:
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, state['registry'][5:] + r'\UserSyncRoots')
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, state['registry'][5:])
    except OSError:
        pass
state['unregister_hr'] = f'0x{cld.CfUnregisterSyncRoot(ROOT) & 0xffffffff:08x}'
json.dump(state, open(args.ready, 'w'), indent=1)
log({'type': 'STOPPED'})
