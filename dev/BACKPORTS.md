# Backports from upstream Krusader 2.9.0 / master

Panero is based on Krusader v2.8.1 (Qt5/KF5, D-001, D-007). Upstream v2.9.0
and master are Qt6/KF6, so fixes are hand-ported. Each applied item is one
commit on branch `feature/backports` with subject `Backport: <upstream subject>`
and `Upstream:` / `Original-Author:` lines in the body. Most upstream patches
do not apply mechanically because upstream commit ab6fd00c4 reformatted the
whole tree; the logic was ported, not the formatting.

Base: `origin/main` at bfa10f64b. Upstream refs: tags v2.8.1, v2.9.0 and
`upstream/master` (fe91f0d47) as fetched on 2026-11-17.

Legend: **applied** = taken unchanged apart from formatting; **adapted** =
changed for KF5/Qt5 or for Panero (how is noted); **partial** = only part of
the upstream commit; **skipped** = not taken (why is noted).

## Applied

| # | Upstream | In | Subject | Status | Panero commit | Notes |
|---|----------|----|---------|--------|---------------|-------|
| 1 | d80aa5568 | master | Dialogs: avoid a division by zero when searching | adapted | a81d18218 | `qsizetype` → `int` (Qt5) |
| 2 | 5021e931d | master | Handle a zero-length list in emitProgress() | adapted | 6f5698aba | `int` length, no cast |
| 3 | 503d41eae | v2.9.0 | Panel: fix crash when dropping on status bar (bug 474596) | adapted | 6db9c2060 | QPointer captured by value; `dropped(QDropEvent*)` |
| 4a | 31c4b50d3 | v2.9.0 | Fix wrong position of file drop menu in Wayland | adapted | 3405166bc | `KJobWidgets::setWindow()` on the DropJob; target widget passed down |
| 4b | f65ad42cd | v2.9.0 | Panel: fix wrong position of drop menu when dropping on status bar | adapted | 8576ebd6c | Qt5 `event->pos()` kept |
| 5 | 5ca2a1e54 | master | Panel: fix a premature destruction (Preview submenu) | adapted | bdc6030f9 | KrPreviewPopup on the heap, parented to the menu |
| 6 | c6945e68e | master | kio_iso: recognize `application/vnd.efi.iso` (bug 457791) | applied | 73c2eeb37 | noble's canonical ISO mime type; see "ISO on noble" below |
| 6+ | 4c85272cd | master | kio_iso: fix the inability to open iso files (part 1) | partial | 2b3f22609 | **Was on the skip list; taken after verification (coordinator request).** Only the `kdemain()` hunk: create a `QCoreApplication` (worker crashed with SIGSEGV in `SlaveBase::SlaveBase` without it). The `KIO::WorkerFactory` port is KF6-only and not taken. |
| 7a | 760806797 | master | kio_iso: reset a variable to avoid accumulating texts | applied | e0097c334 | Joliet dir no longer "El Torito BootJoliet level 3" |
| 7b | 91d1b2c65 | master | kio_iso: solve a warning by adding a '.' entry | applied | d7c3bf1be | KF5 also warns "UDSEntry for '.' not found" |
| 8 | 948880e60 | v2.9.0 | Panel: Back/Forward ignore synchronized browsing (bug 480319) | adapted | a2fc53fd6 | cosmetic hunks dropped; as upstream, history in a locked tab follows the locked-tab rule (opens a new tab) |
| 9a | 03fe3ae39 | v2.9.0 | Use KCrash; save settings on crash and on signals (bug 491861) | adapted | e1c4f576e | `KF5::Crash` component + link; `libkf5crash-dev` in dev/build-deps.txt, bootstrap script, README; `emergencySaveSettings()` skips while the window is starting/being destroyed |
| 9b | 736d1084c | v2.9.0 | Handle SIGINT as exit signal, drop SIGPIPE handling | applied | 2e633f8d1 | |
| 10a | 9bfc64479 | master | Synchronizer: enable sorting (part 1) | adapted | b368e9c03 | Panero: list starts sorted A-Z by name (Qt default indicator is descending) |
| 10b | 28ba3fb9e | master | Synchronizer: enable sorting (part 2: numeric size/date) | applied | b2ec3a574 | `<limits>` included |
| 11 | 085753c53 | v2.9.0 | Synchronizer: "Copy from right to left" default with Asymmetric + Ignore Date (bug 476194) | applied | 5763b72c9 | |
| 12 | 499b3cacd | v2.9.0 | KViewer: url not changed after "Save as" (bug 496184) | adapted | 6f7b5b41b | KF5 KParts; clang-format-only hunk dropped |
| 13 | 1260116f3, 95458c05a, ba01cf976, f06b8f0f9, 9ed504bc2, a8395f601, e733bde6d, c889811cc, 9d4711902, 37f75b4f5, 44ec4780c | master | BookMan crash / use-after-free series (bugs 520538, 524076) | adapted (one set) | c857eb215 | KF5 `changed(QString,QString)` signal, `associatedWidgets()`; QPointer in QVariant via Qt5 smart-pointer metatype. Same-MR 6fd66a4be / b2ad6a741 not taken (KF6 KBookmarkMenu dummy menu, teardown-only leak) |
| 14a | 57042d1b2 | master | Enter `.tar.bz2`/`.tbz` archives | applied | b3a7cfa8b | noble reports `application/x-bzip2-compressed-tar`, no KIO protocol listed it |
| 14b | e4e9f35ed | master | Enter `.jar` archives (bug 497820) | adapted | 28624a953 | noble reports `application/java-archive` |
| 15 | 87308c6c7 | master | Read files inside `.deb` archives | adapted | e722bfdc8 | `localeEncodedString()` kept; "./" member prefix |
| 16a | bd0dea2ec | master | Search inside `.tar.xz`/`.txz` | applied | a43ffa811 | xz magic enabled |
| 16b | 4acf615b1 | master | Search inside `.tgz`/`.tbz` | applied | 3f3186a2e | |
| 16c | b28a0eb6b | master | Search inside `gz`, `xz`, `lzma`, `bz2` (krarc `abort()`) | adapted | dab58138d | KF5 krarc members; `KArchiveDirectory` include |
| 17a | 39ac05713 | v2.9.0 | Panel: remove separate adding of archive actions (duplicate Compress/Extract) | adapted | 83a254f45 | separate loader kept only for KIO < 5.79 |
| 17b | b30994ec9 | master | Solve "wildcard call disconnects" and related problems | applied | 7f45c66f1 | Qt5 does not warn, the over-broad disconnects behave the same |
| 18a | 8dde5af1e | master | Disallow empty names when renaming | applied | 0db6dfa3d | |
| 18b | b89d6643d | master | Message when the proposed name equals the old one | applied | 4f3a2a152 | Message only after an explicit Enter; Esc/Tab/focus loss stay silent. 46ec093b6 deliberately not taken |
| 19a | a351507d2 | v2.9.0 | Fix file filter of user action import/export dialog | adapted | 9d0ae8df6 | `I18N_NOOP` kept (msgid changes → falls back to English until catalogs update) |
| 19b | c1983c38e | v2.9.0 | No passwords in krarc debug log | adapted | 3c0acb84f | also the 4th, pre-5.96 KF5 copy |
| 19c | 4bf3c111c | v2.9.0 | KrArc: fail correctly when copying from krarc to krarc | adapted | 768fefbbf | both WorkerResult and error() variants |
| 19d | e9cd57f91 | v2.9.0 | konsole part destroyed() during TerminalDock destruction | partial | 7c15e15fd | destructor hunk only |
| 19e | 1c152fd1a | master | Solve two memory leaks | partial | 8f7a4dcb2 | krquery `delete[]` only (other hunk is KF6 KBookmarkMenu) |
| 20a | 12320983d | master | Archive: fix a memory leak (QTemporaryDir) | applied | 5df02e203 | |
| 20b | 0c55355e1 | master | PopularUrls: fix a memory leak | applied | 64b9f6926 | |
| 20c | 0eaa6f80c | master | PopularUrls: fix an incoherence | applied | 1bbdeeb60 | |
| 20d | be0d1a7bc | master | UserAction: fix a memory leak | applied | b49d1f536 | |
| 20e | 38030e731 | master | UserAction: remove an unnecessary destructor | applied | 263bcd3fd | |
| 20f | 34a01dbe6 | master | Fix extra lines in console output window (bug 466961) | adapted | e290f247c | Panero: insert at the end even if the user moved the cursor |

### Verification done (Ubuntu 24.04 host, KF5 5.115, isolated HOME/XDG)

- ISO on noble: an ISO built with `genisoimage -J -R` has mime
  `application/vnd.efi.iso`; before the fix no protocol was mapped and the
  worker (Panero's and the distro Krusader's alike) crashed on start, so the
  client waited forever. After 2b3f22609 + 73c2eeb37: `KProtocolManager`
  maps the mime to `iso`, `kioclient5 ls iso:/…/test.iso/` lists `.`,
  `ISO9660`, `Joliet level 3` (no accumulated name, no "UDSEntry for '.'"
  warning), `kioclient5 cat` reads a file inside, and the staged Panero
  started offscreen with `--left=iso:/…/test.iso/Joliet level 3/sub/` opens
  it (URL saved as left tab and popular URL, no errors).
- Archives: `.tar.bz2` → `tar` protocol; `.jar` lists/reads via krarc;
  a `.deb` built with `dpkg-deb`: reading a file failed ("Access denied")
  before, works after; `plain.txt.{xz,bz2,lzma}` killed the krarc worker
  before, list and read after; a `detectArchive()` probe classifies
  tgz/tbz/txz/xz/bzip2/gzip/deb/zip test files correctly. `rpm2cpio` is not
  installed on the host; cpio extraction with the "./" pattern was checked on
  a newc archive with rpm2cpio-style names.
- KCrash/signals: offscreen run with an isolated HOME; SIGTERM, SIGINT and
  SIGSEGV each print the emergency-save messages and write panerorc. Without
  DrKonqi installed KCrash re-raises the signal (exit 139), so the normal
  core-dump/apport path is unchanged.
- BookMan: offscreen run with a bookmarks file containing four "Docs" entries
  and a folder, rewritten three times while running, then SIGTERM: no crash.
- krarc→krarc copy now fails with "Copying from archive to archive is not
  supported." and leaves the destination archive untouched.
- Build: warning-clean for touched files (no new warnings against the
  baseline; two KPluginLoader deprecation warnings in panelcontextmenu.cpp are
  gone). `ctest`: 4/4 pass.

## Skipped

| Upstream | Subject | Why |
|----------|---------|-----|
| 1fc5ce50f (merge) items | KDE Connect share, Plasma 6 mime fix, thumbnails plugin-list fix, "Export and compress" | only exist in the KF6 merge; depend on KF6/Plasma 6 APIs |
| 577693a34 | Read files inside 'rpm' archives | removes `localeEncodedString()` and `setTextModeEnabled(false)` because they break under Qt6; on Qt5 the encoding helper works (reading inside a .deb verified with it) and the brief says to keep it. The `./` member prefix from 87308c6c7 covers the rpm get path. |
| af45ad10f | Enter 'gz', 'xz', 'lzma', 'bz2' archives | adds gzip/bzip2/xz/lzma to krarc.json's archive mimetypes (+ an lzma magic), so Enter on a single compressed file would browse into it instead of opening it with an application: a behaviour change. The krarc crash for these files is fixed by b28a0eb6b; they can still be browsed explicitly. |
| 8651aa12e | Tray icon click does not toggle window state | uses `KStatusNotifierItem::setAssociatedWindow(QWindow*)`, which is KF6-only (KF5 has `setAssociatedWidget()`, already used); fixes a Qt6 window-handle issue |
| 8b0b6869e | KTerminalLauncherJob instead of terminal setting | removes the terminal setting; Panero keeps the setting |
| 34e948902, a317605af | Synchronizer skip/rename dialog fixes | fix code that only exists after the KF6 port |
| MountMan fixes | — | fix code that only exists after the KF6 port |
| 06d9a4855 | KIO Iso: KCompressionDevice only for filename if mimetype empty | fixes KF6-port code; KF5 kio_iso works (verified above) |
| 099c4cfc9, eee2de3ce | Panel: NameToMakeCurrent before refresh / re-add search fix | fix code that only exists after the KF6 port |
| 6fd66a4be, b2ad6a741 | BookMan memory leak / dummy menu lifetime | KF6 `KBookmarkMenu` dummy menu; Panero (KF5) uses `KBookmarkManager::slotEditBookmarks` and a QPointer'd dummy menu |
| 46ec093b6 | Combine similar code into a single place | swaps the two rename error messages; still buggy upstream |

## Owner decision (not applied)

| Upstream | Subject | What it would change |
|----------|---------|----------------------|
| cfae74772 | KrViewer: fix editing a non-local file in an external editor | F4 with an external editor on sftp:// or in-archive files would pass a kio-fuse path or a kioexec temp copy (uploaded back after the editor exits) instead of a raw URL; behaviour change for remote editing, needs a KF5 port of `KIO::DesktopExecParser` usage and testing with/without kio-fuse. |
| 3ff36af32 | Entering 'rpm', 'deb', 'war' archives (drops `setNavigatorUrl()` in `doRefresh()`) | Location bar would no longer be updated before the listing finishes; fixes sub-directory navigation in rpm/deb/war but changes when the URL bar updates during every navigation (Panero's navigation dock/pins follow it). |
| 6713c4132 | FileSystem: do not fail silently if deletion/trash fails | Adds error dialogs when delete/move-to-trash cannot be performed (e.g. read-only or missing trash); visible behaviour change in the delete flow, interacts with Panero's marks handling. |
| 4a0d063a3 + c7a4ac114 | Fix crash on any action in toolbar context menu (main window + KrViewer) | Replaces the manual toolbar/statusbar/shortcut/configure-toolbar actions with standard KXmlGui setup and edits the .rc files; Panero has a customised toolbar layout and labels, so menus and saved toolbar state would change. |
| e78991813 | Fix restoring a window's position after restart (bug 498325) | Changes how the main window geometry is restored; Panero sets its own default window size (~1280x800) and Wayland ignores positions, so it needs checking against Panero's startup geometry code. |
