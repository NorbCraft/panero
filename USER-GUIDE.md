# Panero quick guide

Panero is a twin-panel file manager based on Krusader. Everything Krusader
can do is still there (menus, F-keys, archives, remote locations, search,
synchronizer, viewer/editor, user actions); this page explains what is
specific to Panero.

## Window

- **Toolbar** (one row, icons only): sidebar button (wide, far left), Up, Home,
  Refresh, Compare, Pack, Unpack, Search, Trash, Hidden Files, Details |
  Thumbnails, and at the right edge **Quick Settings** (cogwheel) and **Menu**
  (☰). Right-click the toolbar to change the icon size or show text; one setting
  changes all buttons. To move the toolbar, uncheck *Lock Toolbar Positions* in
  that menu and drag the dotted grip.
- **Menu** (☰) holds all menus (File, Edit, Go, View, ..., Help). If you prefer
  the classic menu bar (Alt+letter navigation), choose **Menus: Menu bar** in
  Quick Settings.
- The window title shows the active panel's folder ("Projects — Panero").
- The **tab bar** appears when a panel has two or more tabs; the new-tab button
  sits next to the path.
- Each panel is a rounded card; the **active panel** has a coloured outline and
  a coloured bottom row (selection count and free space). Next to the path, the
  **drive button** ("System (/)", USB sticks, network locations) opens the list
  of devices.
- **Copying, moving and deleting** show their progress in the bottom row of the
  panel that receives the files ("Copying photo.jpg to Projects — 45 %"), with
  a button for the details window and a Cancel button.

## Cursor and marks

- The **cursor** is the row with the solid **blue frame** and the blue bar on
  its left. It is where the keyboard acts. In the inactive panel the frame is
  grey.
- **Marks** are the files you have picked for an operation. Marked files are
  shown in **red** (a check badge on the icon can be switched on in Quick
  Settings). Marks stay until
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
| **hold** the right button ~0.6 s on a file | a small pie next to the pointer fills up, then the normal context menu opens (Open With, Properties, service menus, ...). If files are marked, the menu works on the marked files and says "Marked: N items" at the top. Marks are not changed. |
| hold on empty space or on ".." | opens the folder menu (Create New, Paste) |
| context menu extras | **Copy Path** (full path of the files, or of the folder on empty space) and **Open Terminal Here** |
| right-click a **column header** | immediately opens the column chooser (show/hide columns such as Ext, Type, Owner) |
| Menu key or **Shift+F10** | context menu from the keyboard |

To use the original Krusader way instead (left click selects, Ctrl+click adds),
choose **Selection: Krusader** in **Quick Settings**. The hold time and more
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
- **Cloud**: your cloud drives and servers (can be switched off in Quick
  Settings). The **+** button offers:
  - **Add Cloud...**: Google Drive, OneDrive, Dropbox, pCloud, Box,
    Nextcloud/WebDAV, **Import from rclone** and **Other services** (rclone's
    own setup in a terminal). See *Cloud sign-in and safety* below.
  - **Connect to Server...**: Nextcloud/ownCloud or other WebDAV servers, SFTP,
    Windows shares/NAS (SMB) and FTP. Passwords are asked by the system when
    needed and are not stored in Panero's settings.
  - Click an entry to open it in the active panel (cloud drives connect first;
    files are loaded on demand). Right-click: Open Offline Files, Offline
    Files..., Disconnect, Sign Out and Remove, Sign Out of All Clouds. Cloud
    drives are disconnected when Panero closes.
- Drag the handles between Pinned, Cloud and Folders to give a part more room.

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

## Quick Settings

**Quick Settings** (cogwheel at the right of the toolbar, also in the View menu):

- Appearance: follow the desktop's light/dark setting, or force light or dark.
  Only Panero is affected.
- Density: Compact / Comfortable / Spacious or a custom row height (16–48 px)
  and icon size. Rows never get so small that text is cut off.
- "Use Panero's look": the modern Panero look; unchecked, Panero uses the plain
  system style like Krusader.
- **Icons**: the system's icons or **Panero Line** (thin line icons for the
  toolbar and sidebar). **Folder color**: keep the icon theme's folders or pick
  a colour (or a custom one).
- **Menus**: Menu button or the classic menu bar. **Cloud** section on or off.
- **Hold feedback** while the right button is held: pie, ring, bar or off.
- Changing the appearance, icons or folder colour asks to **restart** Panero
  (Restart / Later); folders and tabs are kept.
- **Selection**: Panero (click moves the cursor, right click marks) or Krusader
  (click selects).
- Alternating row backgrounds and all colours (both stripes, marked text and
  background, cursor fill (or *Transparent*: frame only) and frame, and the
  **active panel and selection**
  colour, which is also the colour of the current folder in the tree),
  separately for light and dark. The default active colour is Krusader's blue.
  **Restore Defaults** resets them.
- "All Settings..." opens the complete original configuration.

## Cloud sign-in and safety

- **Google, Microsoft, Dropbox, pCloud, Box**: you sign in on the provider's
  own page in your web browser (check the address). Panero never sees your
  password. Before the browser opens, Panero says what access is granted:
  the files of the drive only, never your e-mail, contacts or calendar, and
  never your password. Google Drive and OneDrive can be added **read-only**;
  OneDrive asks only for your own files unless you tick *Also files others
  shared with me*.
- **Nextcloud/WebDAV**: use an **app password** (Nextcloud: Settings >
  Security), which you can revoke for this computer alone.
- **Where sign-ins are kept**: in Panero's own, **encrypted** file
  `~/.config/panero/cloud/rclone.conf`. Its key is kept in your desktop
  keyring (GNOME Keyring on Zorin), which unlocks when you log in. A copy of
  your home folder or a backup does not reveal your sign-ins. Your own
  rclone setup (`~/.config/rclone`) is not used or changed; *Import from
  rclone* moves remotes from it into the encrypted file.
- **Sign Out and Remove** cancels Panero's access at the provider (Google,
  Dropbox, Box, pCloud) and deletes the sign-in and downloaded files. For
  Microsoft, Panero opens the page where you remove the access
  (<https://account.live.com/consent/Manage>). You can always check:
  Google <https://myaccount.google.com/connections>, Dropbox
  <https://www.dropbox.com/account/connected_apps>, pCloud *Settings > Linked
  apps*.
- Panero uses its own, up-to-date copy of the free tool *rclone* (part of the
  package), not the older one of the system.
- Protect the computer itself: a login password, a screen lock, and
  full-disk encryption if it could be lost or stolen. Panero cannot protect
  sign-ins from malicious programs that already run under your user account.

## Offline files

- Files you open from a cloud drive stay **available on this device**
  (green check outline) for up to 30 days, within a size limit; others are
  **online-only** (cloud badge) and are downloaded when you open them.
- Right-click files or folders of a cloud drive:
  - **Make Available Offline**: downloads them now (folders: everything
    inside), in the background with a progress window.
  - **Free Up Space**: removes the downloaded copies from this computer; the
    files stay in the cloud. Files with changes that are not uploaded yet are
    never removed.
  - **Always Keep on This Device (Synced Copy)** (folders): a copy in
    `~/Cloud Offline/<drive>/...` (green filled check) kept in sync both ways,
    also without internet. Opening the folder opens the copy. **Stop Keeping on
    This Device** uploads changes and deletes the copy. Conflicting changes are
    kept side by side as `*.conflict1/2` files.
- Right-click the cloud drive > **Offline Files...**: the synced folders and
  their state, *Synchronise Now*, *Repair*, the space for opened files, and
  whether offline files are deleted when you remove the drive (default: yes).
- **Ctrl+R** (Reload) on a cloud drive fetches the folder again from the cloud.
  Changes made elsewhere also appear by themselves within about a minute on
  Google Drive, OneDrive, Dropbox and Box.
- **Google Docs, Sheets and Slides** have no file size on the drive (shown as
  0 B). Opening one opens it in Google's editor in your browser. Right-click >
  **Save a Copy as Office File in Other Panel** saves a .docx/.xlsx/.pptx copy.

## Comparing files (KDiff3)

**F9** opens a terminal in the active panel's folder (the desktop's terminal
if Krusader's default, Konsole, is not installed).

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
- Cloud sign-ins (encrypted): `~/.config/panero/cloud/`; downloaded cloud
  files: `~/.cache/panero/cloud/`; folders kept offline: `~/Cloud Offline/`
- Toolbar/menu customisations: `~/.local/share/kxmlgui5/panero/` (when a new
  version changes the toolbar, an older file is saved as `paneroui.rc.bak` and
  only your shortcuts are kept)

## Updates

Panero does not update itself and adds no software source. New versions are
published at <https://github.com/NorbCraft/panero/releases>: download the
newer `.deb` and open it (or `sudo apt install ./panero_<version>_amd64.deb`).
Your settings are kept.
