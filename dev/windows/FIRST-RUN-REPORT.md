# Windows first-run report: a Total Commander user's view

Build: branch `experiment/windows`, CI artifact `panero-windows-x64`
(`panero-0.1.0-preview.4-windows-x64.zip`, about 51 MB, 304 files).
Test machine: a fresh GitHub runner, Windows Server 2025 Datacenter, 1024×768
screen. `C:\msys64` was renamed and Panero's `PATH` limited to the Windows
folders. Automated with `dev/windows/gui-test.py` (pywinauto) on disposable
files in `C:\pt\work`; every result was checked on disk. Screenshots and logs
are in the CI artifact `panero-windows-test-results`.

## Works as a TC user expects

| Check | Result |
|---|---|
| Starts from the unzipped folder | No installer and no MSYS2. All 187 loaded modules come from the ZIP folder or `C:\Windows`, and only in-box Windows DLLs are used (no VC++ redistributable). |
| Drives | Alt+F1 / Alt+F2 open the drive list with Explorer's names and icons ("Windows (C:)", "Temporary Storage (D:)"). Enter switches the panel to that drive. The button above each panel shows the current drive. Drives are also pinned in the sidebar. |
| Navigation | Enter opens a folder, Backspace goes up, Tab switches panels. The window title shows the active folder. |
| Keyboard selection | Insert marks a file and moves down; F5 copies the marked files. |
| Mouse selection | Click + Ctrl+click marks two files; F5 copies exactly those. |
| F5 copy / F6 move | The dialog proposes the other panel (`C:\pt\work\right\`); Enter starts. |
| F2 rename | In place, with the name selected without the extension (Windows default). |
| F3 view / F4 edit | F3 opens the built-in viewer; F4 opens Notepad (Windows default). |
| F7 new folder | Created; the cursor stays in the folder. |
| F8 and Del | A confirmation, then the file is in the Recycle Bin. |
| Icons | Windows shell icons for drives, folders (Desktop, Documents, ... with their own icons) and files (by file association) in the list, the folder tree and the pins. |
| Columns | Name, Ext, Size, Modified, as in TC. Hidden and system files are hidden by default. |

## Feels unfamiliar

- **First run**: three windows before the panels appear:
  - Krusader's "Welcome" box.
  - A "Search results" table of Linux tools (kdiff3, kompare, xxdiff,
    thunderbird, kmail, krename, md5sum), saying "install new tools in your
    $PATH (e.g. /usr/bin)".
  - The Konfigurator.

  A TC user expects the panels straight away.
- **Shift+F6** opens "Move … to:" with a Queue button (move later). In TC,
  Shift+F6 renames in place.
- **Plain mouse click** moves the cursor without marking (Panero selection
  mode); marking needs Ctrl+click or Insert.
- **Path bar** is a breadcrumb ("Windows (C:) › pt › work"), not a text field.
- **Words**: "Konfigurator", "Trash" on the delete button (Windows says
  Recycle Bin), the Linux tool names on the first run.

## Missing on Windows

- Cloud drives and Connect to Server (rclone mount needs WinFsp on Windows).
- Single-instance mode and remote control (they use D-Bus).
- ISO image browsing. Archive browsing needs 7-Zip on PATH (not tested).
- The internal text editor (needs the Kate part); F4 uses Notepad.
- Owner, group and permission columns (Windows has no Unix permissions).
- An installer, a Start menu entry and code signing. SmartScreen will warn
  on the first start.
- UNC network places (`\\server\share`) in the drive list; mapped network
  drives with a letter are listed.
- Known risk: upstream code sometimes uses `QUrl::path()` as a file path,
  which gives `/C:/...` on Windows. Fixed where the tests hit it (viewer,
  terminal, synchronizer content compare, links, listing). Less-used
  features may still have it.

## Needs an interactive Windows session

- SmartScreen and "unknown publisher" prompts on a normal desktop PC.
- High-DPI screens (125–200 %), several monitors, Windows 11 dark mode with
  Panero's dark theme.
- Drag and drop between Panero and Explorer or the desktop.
- Right-click menus (Panero's own menu; there is no Explorer shell menu).
- Long paths (over 260 characters), non-ASCII file names, network shares,
  OneDrive placeholder files, plugging in and removing USB drives.
- Large copies: the progress row, pause/cancel, overwrite prompts.
- Archive browsing with 7-Zip installed; F3 on images and PDFs.
- Non-US keyboard layouts.
