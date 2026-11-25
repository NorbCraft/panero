# Panero Image Viewer — specification and implementation plan

Status: **approved for implementation** (owner, 2026-11-25; decisions in
`dev/DECISIONS.md` **D-023**). Branch: `feature/image-viewer`, started from
preview 13. Progress: `dev/PLAN.md` Phase 18. Start guide for the
implementing agent: `dev/handoff/IMAGE-VIEWER-START.md`. Visual references
and the approved icon: `dev/handoff/image-viewer/`.

This file is self-contained: it restates everything agreed with the owner,
so no earlier prompt is needed. If something here conflicts with code you
find, the code may be newer — check `dev/LOG.md` and `dev/DECISIONS.md`,
then ask the owner rather than guessing (section 12).

Contents: 1 Goal and scope · 2 Behaviour in the file manager · 3 Viewer
window · 4 Marking and Trash · 5 Editor · 6 Saving · 7 Info and metadata ·
8 Date Taken and Photo view · 9 Panero as an "Open with" image viewer ·
10 Technical design · 11 Milestones · 12 Open questions · 13 Verification ·
14 Versions, branches, records · 15 Windows follow-up · 16 Facts verified
on 2026-11-25

---

## 1. Goal and scope

A fast, simple image viewer with a basic editor, built into Panero (a
Qt 5 / KF5 twin-panel file manager forked from Krusader 2.8.1). It must feel
snappy: opening, browsing and zooming never wait for the disk in the GUI
thread.

In scope: viewing and browsing images from a panel; marking photos and
moving them to Trash; a basic editor (tone, crop & rotate, paste previous
edit) with safe saving; image properties (EXIF, IPTC, XMP, ICC); a sortable
**Date Taken** column in Panero's panels; a per-panel **Photo view**; Panero
selectable as an image viewer from other programs.

Out of scope: other viewers (video, PDF, audio), printing, ID photos,
collages, batch editing, a photo library/manager, cloud-specific editing.
Linux (Zorin 18 / Ubuntu 24.04) is the first target; Windows is planned
(section 15) but not built now — keep interfaces portable and the Windows
build compiling.

Quick View (preview 13, Ctrl+Q, `app/QuickView/`) stays independent: it turns
the other panel into a simple preview of the current item, for images and
the other types it already handles. The viewer is a separate window.

## 2. Behaviour in the file manager

- **Double-click, Enter and F3** on a supported image in a file panel open
  the Panero Image Viewer — also while Quick View is on. Other files keep
  their current behaviour (Enter: default application; F3: Krusader's
  viewer). "Open With" keeps listing other applications and also lists the
  Panero Image Viewer.
- The viewer is a **normal top-level window**: the user can switch back to
  the file manager while it stays open. It has **its own taskbar entry and
  icon** (teal viewer icon), separate from Panero's (section 10.5).
- **Session contents:** all supported images that are visible in the source
  panel, in the panel's sort and filter order at the moment of opening,
  marked and unmarked together. The session starts at the image that was
  opened. Later changes to the panel's sort or filter do not reorder an
  open session.
- **Closing** (Esc or double-click on the photo, in View mode; or the window's
  close button) reactivates the source panel and tab and puts the panel's
  current row (cursor highlight) on the **last photo viewed**. Marks are not
  changed by closing.
- **Fallbacks** (D-023): source tab moved → follow it; source tab closed →
  open the photos' folder in the active panel; last viewed photo gone → the
  nearest surviving neighbour in the session order, otherwise just the
  folder; no photo left in the session → the viewer closes itself and says so
  (a short message in the panel's status/notification area, not a modal
  box). If Quick View is on in the other panel, it follows the restored row
  (it already follows the cursor).

## 3. Viewer window

Layout **C** (reference `01-toolbar-and-tone.jpg`), with Panero's Qt styling
and Panero's icon set, in light and dark.

- **Toolbar order:** Previous / Next → Mark (checkbox) / Thumbnails →
  Fit / 100% / Full Screen → View / Edit / Crop / 90° Left / 90° Right, then
  at the far right **Info** and, separated, **Delete**. At small widths the
  toolbar becomes compact; actions that do not fit go into an overflow
  menu — every action stays reachable.
- **Browsing:** the plain mouse wheel browses (down = next); **Ctrl+wheel**
  zooms toward the pointer. **Space** = next, **Backspace** = previous.
  Previous/Next buttons, arrow keys (Left/Right) and the wheel **stop** at
  the last/first photo; **Space/Backspace wrap around** (last → Space →
  first; first → Backspace → last).
- **Zoom:** Fit (default), 100%, Ctrl+wheel, `+` `-`, `0` = fit, `1` = 100%;
  drag to pan when zoomed. **Every newly shown photo starts fitted.** EXIF
  orientation is always applied.
- **Thumbnail strip** at the bottom: thumbnails with file names, a **blue
  frame** on the current photo, the current thumbnail kept **centred** as the
  strip scrolls, can be hidden with the Thumbnails button; the last
  show/hide choice is remembered.
- **Full screen:** toolbar button and **F11**; hides the normal window chrome
  (and toolbar/strip, which reappear on mouse movement near the edge or
  stay hidden — implementer's choice, keep it simple); a clear **exit
  control** is always visible on mouse movement. Esc in full screen leaves
  full screen first. Reference `07-full-screen.jpg`.
- **Status bar:** file name, pixel size, "n of N", zoom state, number of
  marked photos (as in the references).
- **Multi-frame images** (D-023): animated GIF/WebP/APNG play in View mode;
  editing them is disabled (Edit/Crop/90° greyed with a tooltip). Multi-page
  TIFF shows page 1 with page buttons in the status bar.
- **Touch / trackpad:** two-finger sideways swipe browses; on a touch screen
  a swipe browses while the photo is fitted; drag pans when zoomed (mouse
  drag always pans when zoomed).
- **Cloud and network files** (rclone mounts, Windows placeholders): never
  read automatically (Panero's rule, D-018/D-020/D-022). Show name, size and
  "Press Space to load" — careful: Space is also "next". Use **Enter or a
  click on the photo area** to load a cloud photo in the viewer, and say so
  in the placeholder text. Neighbours on cloud drives are not preloaded.

## 4. Marking and Trash

- The toolbar **Mark** checkbox and the **Insert** key toggle the current
  photo's mark **without advancing**. A mark is Panero's own mark: the file
  becomes red in the source panel immediately (Panero stays the owner of
  marks; the viewer always goes through the panel's view).
- As soon as **any** photo in the session is marked, **checkboxes appear on
  all thumbnails**, so photos can be marked while browsing (like the Android
  gallery). Clicking a thumbnail's body shows that photo; clicking its
  checkbox toggles its mark. The **blue current-photo frame** is independent
  of the **red marks** (reference `04-marking-and-trash.jpg`).
- **Delete** (toolbar, also the Delete key) moves **only the displayed
  photo** to the Trash through Panero's own delete path (same confirmation,
  jobs window and **Undo** as in the panel, preview 13). On success: remove
  only that file from the session, keep all other marks, show the next
  surviving photo — or the previous one when it was the last; the new
  current photo keeps its own mark state. A cancelled or failed delete
  leaves the session and all marks unchanged.
- **No Trash** (some network drives, cloud mounts; D-023): ask "Delete
  permanently?" with **Cancel as the default**; there is no Undo for that.
- **Last photo deleted:** the session is empty → the viewer closes itself as
  in section 2.

## 5. Editor

One editor with two entry points; both tabs share the current **edit
recipe** and one **Undo/Redo** history.

- **Edit** opens the **Tone** tab; **Crop** opens the **Crop & Rotate** tab
  with its handles ready. **90° Left/Right** stage their change into the same
  recipe and the same Save.
- In Edit mode the plain wheel **does not browse** (it scrolls/zooms as
  appropriate). Each tab has its own **Reset** (tone and crop reset
  independently).
- **Leaving with unsaved changes** (next/previous photo, close, Esc, switch to
  View) asks **Save / Discard / Stay**; the viewer leaves only after the chosen
  save **succeeded**. Esc in Edit mode means "leave the editor" (with this
  guard), not "close the viewer".
- **Editor bar** (above the photo, reference `02-before-and-paste.jpg`):
  Paste Previous Edit, **Before**, Undo, Redo, Save….

### 5.1 Tone

- Exposure (EV), contrast, saturation; a **histogram with integrated
  draggable black, midtone and white handles** (levels). Values show while
  dragging and in tooltips; **no permanent numeric input boxes**.
- **White Balance** (warmth, tint) in a section that is **collapsed** by
  default.
- **Before:** one **momentary** button: while held, the original is shown;
  on release the edited preview returns. Keyboard: **hold backslash `\`**
  (D-023, as in Lightroom). The momentary state resets when the window loses
  focus or the pointer grab ends (never stuck on "before").

### 5.2 Crop & Rotate

- **Free** crop by default; **Original** aspect ratio; common landscape,
  square and portrait ratios (e.g. 3:2, 4:3, 16:9, 5:4, 1:1 and their portrait
  forms); **thirds grid** (toggle); the **output size in pixels** shown live;
  **rotate left/right 90°**, **flip horizontal/vertical**, **straighten**
  (small angle; the crop shrinks automatically to stay inside the rotated
  photo).
- **Fixed Size** (a later editor milestone, M5b): width × height in **px, cm
  or mm**; a **DPI** field only for cm/mm; the resulting pixel size shown
  (e.g. 100 × 150 mm at 300 DPI = 1,181 × 1,772 px, rounded). The crop uses
  the photo's pixels; if the requested pixel size is **larger** than the
  largest rectangle of that shape the photo allows, that rectangle is
  cropped and **scaled up** to the requested pixels (owner, D-023), showing
  the upscale factor and the effective DPI. Reference `03-crop-choices.jpg`.

### 5.3 Paste Previous Edit

- Applies the **last successfully saved edit of this viewer session** to the
  current photo as **one undoable step**: tone values and all geometry (crop,
  90° steps, flips, straighten). The crop is stored **relative** to the photo;
  on a photo with another aspect ratio it keeps its own ratio, centred, at
  the largest size that fits.
- The editor shows which photo supplied it ("Edits pasted from
  DSC_1844.jpg"). Session only — not remembered after closing.

## 6. Saving

- **Save…** opens a dialog (reference `05-exif-and-save.jpg`): **Save a Copy**
  (default) / **Replace Original** (only where safe, see below) / Cancel;
  file name (collision-safe suggestion beside the original:
  `DSC_1845-edited.jpg`, then `DSC_1845-edited-2.jpg` …); format; quality
  where it applies (JPEG default 92); optional resize; keep metadata
  (EXIF/IPTC/XMP, on by default).
- Full output resolution by default, except where crop/geometry or the
  chosen resize changes it. Colour profile kept. EXIF Orientation written as
  "normal" (the pixels are already turned); width/height updated; the
  original **capture time is never changed**.
- **Safe writing (local folders):** write to a temporary file in the **same
  folder**, read it back (decode check), then rename into place. A failed
  save leaves the original **byte-identical** and the edit still open with
  an error that says what went wrong.
- **Replace Original** (D-023): the original is **moved to the Trash first**
  (restorable, also with Ctrl+Z through Panero's Undo), then the edited file
  takes its name. Before replacing, check the original did not change since
  it was opened (size + modification time); if it did, ask. Where there is
  **no Trash**, Replace Original is **not offered** (Save a Copy only).
  Symlinks: replace the target file in its own folder. Cloud and remote
  folders: Save a Copy only.
- **RAW is never replaced** (D-023): saving an edit of a RAW photo writes the
  edit recipe next to it as an **XMP sidecar** `DSC_1850.CR2.xmp` (Panero's
  own XMP namespace, written with Exiv2). Opening that RAW again in Panero
  applies the recipe (non-destructive, like Lightroom/darktable). **Export
  JPEG** writes an edited JPEG copy (quality 92, all metadata). Other
  programs and the panel thumbnails show the unedited RAW.
- **HEIC:** edits are saved as a **JPEG copy** with all metadata; a HEIC copy
  is offered as a format choice with a "metadata is not kept" note (Exiv2
  0.27 cannot write HEIC metadata).

## 7. Info and metadata

- **Info** opens a tabbed properties view (reference `05-exif-and-save.jpg`):
  **General** (file name, folder, pixel size, format, file size, colour
  space/profile name, dates), then **EXIF**, **IPTC**, **XMP**, **ICC** tabs
  when the photo has that data.
- EXIF grouped into readable sections: **Date & time** (Date taken, UTC
  offset), **Camera & lens** (make, model, lens), **Exposure** (shutter,
  aperture, ISO, focal length, exposure bias, flash), **Image**
  (orientation, colour space). The full raw tag list stays available (e.g.
  an "All tags" section).
- Read with Exiv2 in a worker thread, cached per file (path + size +
  modification time). Cloud files: nothing until loaded.

## 8. Date Taken and Photo view (in Panero's panels)

- A sortable **Date Taken** column, separate from Modified (reference
  `06-date-taken.jpg`), from EXIF `DateTimeOriginal` + `SubSecTimeOriginal`
  (`OffsetTimeOriginal` shown in Info only). This keeps photos from several
  cameras chronological after editing.
- **Sort policy** (D-023): the camera's local clock time (date + time +
  subseconds), time zones ignored; ties by name; photos without a capture
  time come after the dated ones, ordered by Modified; on cloud/network
  drives the column fills only for files already read (never downloads).
- Extraction is asynchronous, cached (path + size + mtime), shared with the
  viewer's Info. The viewer simply follows the panel's order.
- **Photo view** (D-023, after the viewer and Date Taken): a per-panel toggle
  that switches the panel to thumbnails, Date Taken order and a filter for
  images and videos; switching it off restores the panel exactly. It reuses
  existing panel features.

## 9. Panero as an "Open with" image viewer (D-023)

- Install a **visible** desktop entry **"Panero Image Viewer"**
  (`io.github.norbcraft.panero.imageviewer.desktop`, `MimeType=` the image
  types Panero can read, `Exec=panero --image-viewer %F`, the teal icon).
  Other file managers then offer "Open with Panero Image Viewer", and it can
  be chosen as the system's default image application. The same entry gives
  the viewer window its taskbar identity (section 10.5).
- `panero --image-viewer FILE...`: if Panero is already running, the running
  instance opens a viewer window (single instance over D-Bus); otherwise only
  the viewer starts, **without** a file-manager window.
- Such a **standalone session** browses the other supported images of that
  file's folder in **name order**, starting at the file (only the given files
  when several are passed). Marks are kept in the session; a **"Show in
  Panero"** action opens the folder in Panero with that photo as the cursor
  and the session's marks applied. Delete works as in section 4.
- Closing the last viewer window ends Panero if no file-manager window was
  open.

## 10. Technical design

### 10.1 Stack (D-023)

- Native C++ in Panero's existing **Qt 5 Widgets / KF5 / CMake** setup. The
  code base compiles as **C++11 (gnu++11)** through KDE's compiler settings
  (`KDE_COMPILERSETTINGS_LEVEL 5.68`) — do not use C++14/17 features.
- **Decoding/encoding:** Qt `QImageReader`/`QImageWriter` with Qt's image
  format plugins and KDE's **kimageformats** plugins (HEIC/HEIF, AVIF, JPEG
  XL, camera RAW through LibRaw, PSD, …). `setAutoTransform(true)` for
  orientation; `setScaledSize()` for screen-sized decoding;
  `QImageReader::setAllocationLimit()` for huge files.
- **Metadata:** **Exiv2** (Ubuntu: 0.27.6; MSYS2 for Windows later: 0.28 —
  write an adapter that compiles with both: different exception types,
  `auto_ptr` vs `unique_ptr`). Call `Exiv2::enableBMFF(true)` once (0.27) to
  read HEIC/CR3.
- **Colour:** Qt's `QColorSpace` (Qt ≥ 5.14) — convert embedded profiles to
  sRGB for display in both the viewer and the edit preview; keep the profile
  when saving.
- **Editing:** Panero's own small pipeline — no ImageMagick. One **recipe**
  type (tone values + geometry) and **one apply function** used for the
  screen-sized preview and for the full-resolution export, so preview and
  output cannot drift apart. Quality safeguards: process in **16 bit per
  channel** (`QImage::Format_RGBA64`), Panero's own **Lanczos** resampler for
  resizing and **bicubic** interpolation for rotation/straighten (Qt's
  `SmoothTransformation` is only bilinear), and a development-only test that
  compares results against ImageMagick (`convert`/`magick` if installed;
  skipped otherwise). If that comparison shows a visible gap, raise it with
  the owner (MagickWand is the documented fallback).
- **Lossless JPEG 90°/flip** (optional optimisation for 90°-only edits of
  JPEG): libjpeg-turbo's TurboJPEG `tj3Transform`/`tjTransform`. Not
  required for the first release; full re-encode at quality 92 is acceptable.

### 10.2 Modules and files

New code lives in **`app/ImageViewer/`** as static libraries, following
`app/QuickView/CMakeLists.txt` (a data library without widgets, unit-tested,
plus a widget library):

| Unit (suggested names) | Responsibility |
|---|---|
| `ImageViewerData` library: `paneroimagedecoder.*` | bounded worker queue; screen-sized and full decode; generation tokens so stale results are dropped; memory cap; cloud guard (`PaneroSlowPaths::isSlowPath`, `PaneroCloudFiles::avoidReading` on Windows) |
| `paneroimagemetadata.*` | Exiv2 adapter: read EXIF/IPTC/XMP/ICC, Date Taken, write metadata into saved files, read/write the RAW recipe sidecar |
| `paneroeditrecipe.*` | the recipe (tone + geometry), JSON/XMP (de)serialisation, relative crop, paste adaptation |
| `paneroimageops.*` | the one apply function: levels, exposure, contrast, saturation, white balance, crop, 90°, flips, straighten, Lanczos/bicubic |
| `paneroimagesaver.*` | staged write, read-back check, collision-safe names, replace via Trash, concurrent-change check |
| `ImageViewer` library: `paneroimageviewer.*` | the window: toolbar, overflow, full screen, status bar, modes (View/Edit) |
| `paneroimagecanvas.*` | the zoom/pan canvas, **extracted from Quick View's `QuickImageView`** (`app/QuickView/paneroquickviewwidgets.*`) and shared; each window supplies its own **gesture policy** (Quick View: as today; viewer: wheel browses, Ctrl+wheel zooms to the pointer) |
| `panerothumbstrip.*` | thumbnail strip with names, blue current frame, centred scrolling, mark checkboxes |
| `paneroimagesession.*` | the session: list of URLs, current index, the source `ListPanel` (guarded pointer) + folder URL, marks through the panel's `KrView`, delete through Panero's delete path, fallbacks |
| `paneroeditorpanel.*`, `panerohistogram.*`, `panerocropoverlay.*` | Tone tab, histogram with handles, Crop & Rotate tab and overlay |
| `paneroimageinfo.*` | Info dialog/panel with tabs |
| `app/ImageViewer/harness/` | `imageviewer-harness` executable (built with `BUILD_TESTING`, not installed) |

Keep Quick View passing its tests after the canvas extraction (it has a unit
test `autotests/paneroquickviewtest.cpp` and a window test
`dev/scripts/gui-quickview-test.sh`).

### 10.3 Integration points in Panero (verified in preview 13 code)

- **Opening:** double-click and Enter both reach
  `ListPanelFunc::execute()` (`app/Panel/panelfunc.cpp`) via
  `KrMouseHandler` (`app/Panel/PanelView/krmousehandler.cpp`) →
  `emitExecuted`. F3 is `ListPanelFunc::view()`. Route supported images to
  the viewer in one place; keep other files on their current paths.
- **Session order:** iterate the source view with `KrView::getFirst()` /
  `getNext()` (`app/Panel/PanelView/krview.h`) — that is the sorted, filtered
  order.
- **Marks:** `KrView::setSelected()` / `isSelected()` (Krusader calls marks
  "selection"); the cursor row: `KrView::setCurrentItem()`.
- **Delete:** `ListPanelFunc::deleteFiles(true)` path (with confirmation,
  preview 13's jobs window and `PaneroUndo` in `app/JobMan/`). The viewer
  needs a variant that deletes one given file (not the panel's current
  selection) — add it next to the existing code rather than duplicating it.
- **Panels and tabs:** `ListPanel` objects survive tab moves; `PanelManager`
  (`app/panelmanager.*`) owns tabs. Keep a `QPointer<ListPanel>` plus the
  folder URL for the fallbacks.
- **Columns:** `KrViewProperties::ColumnType`
  (`app/Panel/PanelView/krviewproperties.h`) ends at `Accessed = 0xa`; add
  `DateTaken = 0xb` at the end so saved column layouts stay valid; sorting in
  `krsort.*`, model in `listmodel.*`.
- **Settings:** Panero's Settings window (`app/Dialogs/panerosettings.*`) and
  the Quick Settings popup (`app/Dialogs/paneroquicksettings.*`) — add viewer
  options there only if needed (e.g. thumbnail strip state is just
  remembered, no setting).
- **Toolbar/actions:** actions are registered in `app/kractions.cpp` and
  `app/Panel/listpanelactions.cpp`; the default toolbar is `app/paneroui.rc`
  with `PaneroToolBar::LayoutVersion` (`app/panerotoolbar.*`) — bump it only
  when the default toolbar changes.

### 10.4 Performance and safety rules

- **No file access in the GUI thread for cloud/network paths** (D-018 —
  Panero once froze for this). Decode, metadata and saving always in
  workers; the GUI thread only receives results.
- Bounded queues: current photo first, then next, then previous; cancel
  stale work when the user moves on; keep at most current ± 1 decoded at
  screen size; full resolution only when zoomed in or saving.
- Thumbnails for the strip: reuse Panero's thumbnail machinery (KIO
  previews, `app/Panel/krpreviews.*`) or the decoder at small size — never on
  cloud paths automatically.
- Corrupt or unreadable files show an error page in the viewer, never a
  crash or a modal box per file.

### 10.5 Own taskbar identity (verified 2026-11-25)

Qt 5's Wayland plugin sends `xdg_toplevel.set_app_id` per window from
`QGuiApplication::desktopFileName()` **at the time the window's native window
is created**. Verified in a headless GNOME Shell 46 (Zorin's shell): set
`QGuiApplication::setDesktopFileName("io.github.norbcraft.panero.imageviewer")`,
create the viewer's native window (`winId()`), show it, then set the name
back to `io.github.norbcraft.panero` — the viewer window got the viewer app
id; the main window and windows created afterwards kept Panero's. GNOME
groups windows by app id and matches it to the `.desktop` file (section 9),
so the viewer gets its own taskbar entry and icon. On X11/XWayland set the
viewer window's own `WM_CLASS` after creation (xcb property). Check on the
real desktop: the separate entry, Alt+Tab, taskbar click (milestone M1).
One process; no helper process.

## 11. Milestones

Each milestone ends with tests passing, `dev/PLAN.md` / `dev/LOG.md` updated,
and screenshots for the owner. Commit in small, reviewable steps; push to
`feature/image-viewer`. Test builds are versioned `0.2.0~dev.N` (section 14).

| | Content | Done when |
|---|---|---|
| **M0** Foundation | `app/ImageViewer/` skeleton and CMake; shared canvas extracted from Quick View; decoder with bounded queue, stale tokens, memory cap, cloud guard; `imageviewer-harness <folder>`; fixture generator (synthetic JPEG/PNG/TIFF/WebP/HEIC/AVIF with EXIF, orientations 1–8, a truncated file, a 20,000 px PNG) | harness browses 500 fixtures with no GUI-thread stall over ~16 ms; Quick View's unit and window tests still pass; unit tests for orientation and the queue |
| **M1** Viewer window | layout C toolbar (order, overflow), Fit/100%/pan, wheel browse with stop rule, Space/Backspace wrap, thumbnail strip, full screen + exit control, status bar, own taskbar identity + the teal icon installed; opening from double-click/Enter/F3 with the panel order; closing returns to the panel row (section 2 fallbacks) | window tests open from both panels with Quick View on and off and with a filter; return row correct; headless GNOME Shell shows the viewer's app id; owner checklist on Zorin (taskbar entry, Alt+Tab) |
| **M2** Marks and Trash | Mark checkbox, Insert, thumbnail checkboxes, live sync with the panel, Delete via Panero's delete + Undo, no-Trash question, last-photo case | panel marks match after every action; cancelled and failed deletes keep session and marks |
| **M3** Info | Exiv2 adapter; Info tabs; grouped EXIF; cache | fixtures with known tags show the right values; cloud file shows nothing until loaded |
| **M8** Date Taken (branch `feature/date-taken`, own PR) | column `DateTaken = 0xb`, background reader shared with M3, cache, sort policy | mixed folder (3 cameras, undated, HEIC, CR2) sorts as specified; 2,000 photos scroll smoothly; nothing read on cloud mounts |
| **M9a** Packaging for 0.2.0 | build-dep `libexiv2-dev`; `kimageformat-plugins` from Recommends to **Depends**; icon + provenance; the viewer `.desktop` (section 9); `panero --image-viewer`; `test-deb.sh` decodes HEIC, AVIF, JXL, TIFF, WebP and a CC0 RAW in a clean container | package test green → **release 0.2.0** (viewer, marks, Trash, Info, Date Taken, Open with) |
| **M4** Tone | recipe + apply function, preview worker, histogram with handles, exposure/contrast/saturation, White Balance, Before (button + `\`), Undo/Redo, reset, Save/Discard/Stay guard | preview vs full export of one recipe within 1 level per channel after scaling; Before resets on focus loss |
| **M5** Crop & Rotate | handles, ratios, thirds grid, output px, 90°, flips, straighten; **M5b** Fixed Size with upscaling | exact expected rectangles at all 8 orientations; Fixed Size example 100×150 mm @300 DPI = 1,181×1,772 px |
| **M6** Save | dialog, staged write, read-back, atomic rename, collision-safe names, Replace via Trash + change check, metadata/ICC kept, Orientation normal, RAW sidecar + Export JPEG, HEIC rules | forced failure (read-only folder, full disk) leaves the original byte-identical and the edit open; EXIF and capture-time sort survive saving; ImageMagick comparison test within tolerance |
| **M7** Paste Previous Edit | per section 5.3 | portrait/landscape/square targets give the expected crops; one Undo removes it |
| **M9b** Packaging for 0.3.0 | release notes, user guide | → **release 0.3.0** (editor) |
| **M10** Photo view | per section 8 | toggling restores the panel exactly; window test |

Order: M0 → M1 → M2 → M3 (+ M8 in parallel on its branch) → M9a = **0.2.0**;
then M4 → M5 → M6 → M7 → M9b = **0.3.0**; M10 after. M5b may follow 0.3.0.

## 12. Open questions (defaults let work continue — confirm with the owner)

1. **Standalone "Open with" order:** name order now (decided). Once Date
   Taken exists, should standalone sessions use Date Taken order? Default:
   keep name order.
2. **RAW sidecar name:** `DSC_1850.CR2.xmp` (darktable style) avoids clashing
   with Lightroom's `DSC_1850.xmp`. Default: as decided; confirm before M6.
3. **Edited RAW thumbnails** in Panero's panels: default **no** for 0.3.0
   (panels show the unedited RAW).
4. **Fixed Size upscaling limit:** default: Lanczos, warning above 2×,
   no hard limit.
5. **Cloud photo load key:** Space browses, so Enter/click loads a cloud
   photo in the viewer (section 3). Confirm when showing M1.

Any new product question: ask the owner, record the answer in
`dev/DECISIONS.md` (new entry, never silently change an old one) and here.

## 13. Verification

Isolated Panero settings (temporary HOME/XDG — see `dev/scripts/run-dev.sh`,
`dev/scripts/dev-seed-config.sh`), synthetic or licensed fixtures only,
never the owner's files or Trash.

| Promise | Test |
|---|---|
| Opening with Quick View on/off; order and filter in both panels | window test: double-click/Enter/F3 in left and right panel, sorted by name/size/Date Taken, filter active; compare the session list to the panel |
| Marks via checkbox and Insert persist | window test reads the panel's marks after each action and after closing |
| Focus returns to the right panel, tab and row | window test, also with the tab moved and closed |
| Trash cancel/failure keep marks | cancelled confirmation; read-only file |
| Full screen; own taskbar entry | F11 and exit control; headless GNOME Shell app id (`WAYLAND_DEBUG=1` shows `set_app_id`); Zorin checklist |
| Before hold/release, focus loss | widget test with synthetic press/release/focus-out |
| Paste/Undo on different sizes; crop geometry | unit tests on the recipe with exact rectangles |
| Preview equals export | unit test: one recipe at preview and full size |
| EXIF kept; capture-time sorting | save fixtures, read back with Exiv2; panel sort test |
| Corrupt files, huge images, bounded loading | truncated JPEG, 20,000 px PNG; memory and queue assertions |
| Failed save keeps the original | read-only folder and forced disk-full; checksum before/after |
| Codecs in the installed package | `dev/scripts/test-deb.sh` in a clean Ubuntu 24.04 container |
| RAW | CC0 samples (e.g. from raw.pixls.us): Canon CR2 + CR3, Nikon NEF, Sony ARW, Fujifilm RAF, DNG, plus an iPhone HEIC — downloaded by the test script with a SHA-256 check, **not committed** |

Window tests follow the existing `dev/scripts/gui-*-test.sh` (Xvfb, xdotool,
screenshots, temporary HOME). Headless GNOME Shell command used for the app
id check: `gnome-shell --headless --wayland --no-x11 --virtual-monitor
1024x768 --wayland-display <name>` inside `dbus-run-session`, with a short
`XDG_RUNTIME_DIR` (Unix socket paths are limited to 108 characters).

## 14. Versions, branches, records

- **Versions** (owner, D-023): preview 13 was the last "preview"
  (`0.1.0-preview.13`). From now on semantic versions: **0.2.0** = viewer +
  marks + Trash + Info + Date Taken + Open with; fixes 0.2.1 …; **0.3.0** =
  editor; **1.0.0** = official release. The version is in `CMakeLists.txt`
  (`set(VERSION …)`), `debian/changelog` and
  `app/io.github.norbcraft.panero.metainfo.xml`. Test builds from the branch:
  `0.2.0~dev.N` (Debian sorts `~` before the release, so 0.2.0 replaces them).
- **Branches:** `feature/image-viewer` (this work, from preview 13);
  `feature/date-taken` for M8. Merge `main` into them when preview 13 or
  later fixes land there. The owner decides when to merge into `main` and
  release; do not merge, tag or publish releases unless asked.
- **Records:** progress in `dev/PLAN.md` Phase 18 (tick the boxes) and
  `dev/LOG.md` (append); decisions in `dev/DECISIONS.md`; user-facing docs in
  `USER-GUIDE.md`; release notes in `dev/release-notes/v0.2.0.md`.
- **In the product:** `app/ImageViewer/`, the icon (moved from
  `dev/handoff/image-viewer/icon/` with provenance), the `.desktop` file,
  generated fixtures and tests, Exiv2 in `debian/control` and
  `debian/copyright` notes, translatable strings via KI18n (`i18n()`).
  **Not in commits:** mockup sources, prototype scripts, binaries, private
  photos, downloaded RAW samples.

## 15. Windows follow-up (planned, not now)

Same code (D-019: one code base; `.github/workflows/windows.yml` builds it on
every pull request). Keep the Windows build compiling; Windows-only parts
are added in a later "Windows round" (owner decides when):

- Own taskbar button: per-window AppUserModelID
  (`SHGetPropertyStoreForWindow` + `PKEY_AppUserModel_ID`).
- KDE's image plugins are **not** in MSYS2's live repository: pin them from
  the MSYS2 archive like the KF5 packages (`dev/windows/install-kf5.sh`,
  `kf5-packages.sha256`) or build them against MSYS2's libheif 1.23, libraw
  0.22, libjxl 0.13; Windows' own HEIC codec (WIC) as a fallback.
- Exiv2 0.28 API (the adapter from M3 already handles it).
- "Open with" registration in the Inno Setup installer
  (`dev/windows/panero.iss`); Recycle Bin and Undo already exist (preview 13).

## 16. Facts verified on 2026-11-25 (Zorin 18.1 / Ubuntu 24.04)

| Format | Qt 5.15 + KDE plugins (`kimageformat-plugins` 5.115) | ImageMagick 6.9.12 (Ubuntu) | Exiv2 0.27.6 |
|---|---|---|---|
| JPEG, PNG, BMP, GIF | read / write | read / write | JPEG, PNG read+write |
| TIFF, WebP | read / write with `qt5-image-formats-plugins` (a dependency since preview 13) | read / write | read+write |
| HEIC/HEIF, AVIF | read / write (libheif 1.17) | read / write | read (BMFF compiled in); HEIC write not supported |
| JPEG XL | read / write | no | read |
| Camera RAW (CR2, CR3, NEF, ARW, DNG, RAF, ORF, RW2 … ~45 types) | read (LibRaw 0.21) | decodes through `ufraw-batch`, not shipped by Ubuntu: fails | CR2/NEF/ARW/DNG; CR3 via BMFF |

- Measured: a 1200×800 HEIC decoded in 86 ms and an AVIF in 11 ms through Qt.
- ImageMagick on Ubuntu has a `policy.xml` limit of 1 GiB memory, 256 MP,
  32k px per side — another reason not to depend on it.
- Package sizes: libexiv2 ≈ 3 MB (already installed on Zorin);
  MagickCore + MagickWand ≈ 8 MB plus delegates (not used).
