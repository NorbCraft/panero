# Panero quick guide

Panero is a twin-panel file manager based on Krusader. Everything Krusader
can do is still there (menus, F-keys, archives, remote locations, search,
synchronizer, viewer/editor, user actions); this page explains what is
specific to Panero.

## Window

- **Toolbar** (one row, icons only): sidebar button (wide, far left), Up, Home,
  Refresh, Compare, Pack, Unpack, Search, Trash, Hidden Files, Details |
  Thumbnails, and at the right edge **View & Colors** (colour wheel) and the
  **menu button ☰**. Right-click the toolbar to change the icon size or show
  text; one setting changes all buttons.
- The **menu button ☰** holds all menus (File, Edit, Go, View, ..., Help).
  **Settings > Show Menubar** brings the classic menu bar back.
- Each panel is a rounded card; the **active panel** has a coloured outline and
  a coloured bottom row (selection count and free space). Next to the path, the
  **drive button** ("System (/)", USB sticks, network locations) opens the list
  of devices.

## Cursor and marks

- The **cursor** is the row with the solid **blue frame** and the blue bar on
  its left. It is where the keyboard acts. In the inactive panel the frame is
  grey.
- **Marks** are the files you have picked for an operation. Marked files are
  shown in **red** with a red **check badge** on their icon. Marks stay until
  you remove them: moving the cursor with the mouse or the arrow keys,
  switching panels or changing the view does not clear them.
- Mark or unmark: **short right-click**, **Space** (current file; on a folder it
  also calculates and shows the folder's size), **Insert**
  (current file, then moves down), **Ctrl+click**, **Shift+click** (range).
  Dragging with the right button held marks a range.
- Clear all marks: **Edit > Clear All Marks** (Alt+Minus).
- Operations (F5 copy, F6 move, F8 delete, ...) act on the **marked files**; if
  nothing is marked, on the file under the cursor. Confirmation dialogs show
  the actual files/count.

## Right mouse button

| You do | Panero does |
|---|---|
| short right-click on a file | toggles its mark |
| **hold** the right button ~0.6 s on a file | opens the normal context menu (Open With, Properties, service menus, ...). If files are marked, the menu works on the marked files and says "Marked: N items" at the top. Marks are not changed. |
| hold on empty space or on ".." | opens the folder menu (Create New, Paste) |
| right-click a **column header** | immediately opens the column chooser (show/hide columns such as Ext, Type, Owner) |
| Menu key or **Shift+F10** | context menu from the keyboard |

To use the original Krusader way instead (left click selects, Ctrl+click adds),
choose **Selection: Krusader** in **View & Colors**. The hold time and more
modes (Konqueror, Total Commander, custom) are in **Settings > Configure Panero
> Panel > Selection Mode**.

## Folder tree and pins

- The wide **sidebar button** at the far left of the toolbar shows or hides the
  left sidebar. The sidebar always works on the **active panel**.
- **Pinned**: press **+** to pin the active panel's folder, or **drag folders**
  from a panel (or another file manager) onto the list. Click a pin to open it.
  A pin stays highlighted while exactly its folder is shown.
  Right-click a pin to move or unpin it. If a pinned folder's drive is not
  connected or not mounted, Panero tells you and offers to mount it (your system
  may ask for authorisation), retry, or unpin.
- **Folders**: a tree that follows the active panel. Click a folder to open it;
  it unfolds to show its subfolders. Click the open folder again to fold it.
  The options button next to "Folders" chooses where the tree starts: **Home**
  (always Home while you are inside Home; on another drive or outside Home it
  starts at that drive or at /) or the **whole file system (/)**, and whether
  hidden folders are shown.
- Drag the handle between Pinned and Folders to give either part more room.

## Views

- **Details**: sortable columns; the **Ext** column shows extensions
  separately (folders, hidden files and names without an extension are not
  split; `.tar.gz` stays together). Renaming always uses the real file name.
- The **Details | Thumbnails** button in the toolbar shows which view the active
  panel uses; click it to switch to the other one.
- **Thumbnails**: a grid of tiles with real previews for images, PDFs, videos
  and other files that have a previewer installed (from KDE's preview plugins);
  other files show their icon.
- **Ctrl+mouse wheel**: in Details, zooming in switches to Thumbnails; there it
  makes the tiles bigger or smaller; zooming out of the smallest tiles returns
  to Details. The Details | Thumbnails button follows.
- **Hidden Files** in the toolbar (Alt+.) shows or hides dot files.
- **Trash** in the toolbar opens a menu: **Open Trash** (shows it in the active
  panel, where files can be restored) or **Empty Trash** (asks first).

## View & Colors

**View & Colors** (colour wheel at the right of the toolbar, also in the View
menu):

- Appearance: follow the desktop's light/dark setting, or force light or dark.
  Only Panero is affected.
- Density: Compact / Comfortable / Spacious or a custom row height (16–48 px)
  and icon size. Rows never get so small that text is cut off.
- "Use Panero's look": the modern Panero look; unchecked, Panero uses the plain
  system style like Krusader.
- **Selection**: Panero (click moves the cursor, right click marks) or Krusader
  (click selects).
- Alternating row backgrounds and all colours (both stripes, marked text and
  background, cursor fill and frame, and the **active panel and selection**
  colour, which is also the colour of the current folder in the tree),
  separately for light and dark. The default active colour is Krusader's blue.
  **Restore Defaults** resets them.
- "All Settings..." opens the complete original configuration.

## Comparing files (KDiff3)

**Compare** in the toolbar compares by content with KDiff3:

- one file selected/current in each panel, or
- two files marked in the active panel, or
- a file with the same name in the other panel.

Two folders can be compared too. If KDiff3 is not installed, Panero asks
**"Install KDiff3?"**; choosing Install installs it from your system's
software sources (your system asks for authorisation) and then starts the
comparison. You can also install it yourself with `sudo apt install kdiff3`.
KDiff3 compares text; for binary formats (CAD, images, archives) it can only
show that files differ. *Compare Folders* (Edit menu) and the *Synchronizer*
(Tools) are different features and compare names, sizes and dates.

## Coming from Krusader

On the first start Panero offers to copy your Krusader settings, bookmarks and
user actions. You can also do it later with **Settings > Import Krusader
Settings**. Krusader's own files are never changed, and both programs can be
installed side by side.

## Files Panero uses

- Settings: `~/.config/panerorc`
- Bookmarks, user actions: `~/.local/share/panero/`
- Toolbar/menu customisations: `~/.local/share/kxmlgui5/panero/` (when a new
  version changes the toolbar, an older file is saved as `paneroui.rc.bak` and
  only your shortcuts are kept)

## Updates

Panero does not update itself and adds no software source. New versions are
published at <https://github.com/NorbCraft/panero/releases>: download the
newer `.deb` and open it (or `sudo apt install ./panero_<version>_amd64.deb`).
Your settings are kept.
