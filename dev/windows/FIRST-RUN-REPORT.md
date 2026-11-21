# Windows first-run report: a Total Commander user's view

Build: branch `experiment/windows`, CI artifact `panero-windows-x64`
(`panero-0.1.0-preview.4-windows-x64.zip`, about 51 MB, 304 files).
Test machine: fresh GitHub runner, Windows Server 2025 Datacenter, 1024×768,
`C:\msys64` renamed and Panero's `PATH` limited to the Windows folders.
Automated with `dev/windows/gui-test.py` on disposable files in `C:\pt\work`.
Screenshots and logs: CI artifact `panero-windows-test-results`.

## Works as a TC user expects

| Check | Result |
|---|---|
| Starts from the unzipped folder, no installer, no MSYS2 or other DLLs | every loaded module is from the ZIP or `C:\Windows` (187) |
| Drives | Alt+F1 / Alt+F2 open the drive list with Explorer names and icons ("Windows (C:)", "Temporary Storage (D:)"); Enter switches the panel; the drive button above each panel shows the drive |
| Navigation | Enter opens a folder, Backspace goes up, Tab switches panels, the title shows the active folder |
| Keyboard selection | Insert marks and moves down |
| Mouse selection | click + Ctrl+click marks two files |
| F5 copy / F6 move | dialog with the other panel's folder (`C:\pt\work\right\`), Enter starts |
| F2 rename | in place, name selected without the extension |
| F7 new folder | created, cursor stays in the folder |
| F8 and Del | confirmation, then the file goes to the Recycle Bin |
| Icons | Windows shell icons for drives, folders (Desktop, Documents, ... with their own icons) and files (by association) in the list, the tree and the pins |
| Columns | Name, Ext, Size, Modified, as in TC |

## Feels unfamiliar

- **First run**: three windows before the panels: Krusader's "Welcome" box, a
  "Search results" table of Linux tools (kdiff3, kompare, xxdiff, thunderbird,
  kmail, krename, md5sum; "install new tools in your $PATH (e.g. /usr/bin)")
  and the Konfigurator. A TC user expects the panels straight away.
- **Shift+F6** opens "Move … to" with a Queue button (Move delayed). In TC it
  renames in place.
- **Path bar** is a breadcrumb ("Windows (C:) › pt › work"); typing a path
  needs a click on the bar. TC users type into the command line or use Ctrl+D.
- **Dialog titles and terms**: "Konfigurator", "Trash" (button text in the
  delete confirmation), "Media" in the menus.
- **Selection**: a plain click moves the cursor without marking (Panero
  selection mode); TC users toggle with the right mouse button or Ctrl+click.
- **Space** calculates folder size and marks, like TC; Num+ / Num− / Num* are
  Panero's select/unselect group shortcuts (Alt++ / Alt+− clear and mark all).

## Missing on Windows

- Cloud drives and Connect to Server (rclone mount needs WinFsp).
- Single-instance mode and remote control (D-Bus).
- ISO image browsing; archive browsing needs 7-Zip on PATH (not checked).
- Internal text editor (needs the Kate part); F4 opens Notepad instead.
- Owner/group/permissions columns and dialogs (no Unix permissions).
- Installer, Start menu entry, file associations, code signing (SmartScreen
  warns on the first start).
- Network places (\\\\server\\share) are not listed in the drive list.

## Needs an interactive Windows session

- SmartScreen / "unknown publisher" prompt on a normal desktop PC.
- High-DPI screens (125–200 %) and multiple monitors.
- Dark mode of Windows 11 and Panero's own dark theme.
- Drag and drop between Panero and Explorer, and to the desktop.
- Right-click context menus (Panero's menu; no Explorer shell menu).
- Long paths (> 260 characters), non-ASCII file names, network shares,
  OneDrive placeholder files, USB drives being plugged in or removed.
- Large copies: progress row, pause/cancel, overwrite prompts.
- Archive browsing with 7-Zip installed; F3 on images and PDFs.
- Keyboard layouts other than US (Alt+F1 etc. on non-US keyboards).
