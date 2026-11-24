# Panero image viewer — plan and requirements workbook

Status: **proposal for later** (not scheduled). Written 2026-11-24 to help
define the requirements; nothing here is decided until it moves into
`dev/DECISIONS.md`.

How to use this file: read sections 1–4 for the picture, then fill in the
questions in **section 11** (tick `[x]`, add notes). The answers become the
requirements; sections 8–9 then turn into the implementation plan.

---

## 1. Goal

A simple, fast image viewer **inside Panero** that a beginner understands at
once, but that also covers what a power user does with photos in a file
manager: look through a folder, sort out the good ones, rotate, crop, resize,
convert, rename by date — without opening a second program.

Principles (from Panero's existing rules):

- **The file manager stays in charge.** The viewer works on the panel's
  folder, sort order, filter and marks; copy/move/delete/rename behave
  exactly like in the panels (F5/F6/F8/F2, Undo).
- **Fast.** Decoding in a background thread, the next and previous image
  preloaded, huge images shown downscaled first. The window never waits for a
  disk or a cloud drive (D-018).
- **Never downloads by itself.** Cloud and network files (rclone mounts,
  OneDrive placeholders) are only loaded when the user asks (Space / click),
  as in Quick View (D-022).
- **Never loses a photo.** Edits never overwrite silently; the original is
  kept (see 5.4) and every file operation can be undone.
- **One code base** for Linux and Windows (D-019).

## 2. Where it fits: three levels

| Level | What | Status |
|---|---|---|
| 1. Quick View (Ctrl+Q) | The other panel shows the image under the cursor: fit, zoom, pan, animated GIF/WebP, EXIF date | **preview 13** (`app/QuickView/`) |
| 2. Viewer window | F3 / Enter / double-click on an image opens a full viewer: next/previous through the folder, full screen, info, file actions, culling | this plan, phase 1–2 |
| 3. Basic editing | Lossless rotate, crop, resize, light/colour, convert, batch | this plan, phase 3–4 |

Level 2 reuses Quick View's image widget (`QuickImageView`) and loader
(`ImageResult` in `paneroquickviewdata.*`), so the viewer and Quick View show
images the same way and share the format support.

## 3. What Panero already has to build on

- **Quick View** (preview 13): image page with fit/zoom/pan, `+ - 0 1` keys,
  animated images via `QMovie`, EXIF date, cloud rule, loading off the GUI
  thread.
- **Krusader's F3 viewer** (`app/KViewer`, KParts): today F3 on an image opens
  whatever KPart the system offers (on Linux Gwenview's part if Gwenview is
  installed, otherwise often nothing useful; on Windows no image part). The
  new viewer would take over F3 for images.
- **Thumbnails** (KIO PreviewJob, cached; Windows shell thumbnails), and the
  finer thumbnail zoom steps (preview 13) for a contact-sheet view.
- **File operations with Undo** (preview 13), **Multi-Rename** with TC
  patterns (preview 11) — a natural place for EXIF date patterns.
- **Two panels** — unique for culling: mark keepers in the viewer, then F5
  copies them to the other panel.

## 4. Technology (stack) options

### 4.1 Recommended: Qt-native viewer + small, focused libraries

| Need | Library | Linux (Ubuntu 24.04) | Windows (MSYS2 UCRT64) | Licence |
|---|---|---|---|---|
| Decode JPEG/PNG/GIF/BMP/ICO/SVG | Qt `QImageReader` | yes | yes | LGPL |
| TIFF, WebP, JPEG 2000 | Qt image formats plugins | `qt5-image-formats-plugins` (**not installed on the owner's PC today**; add as Depends/Recommends) | in the build (qtiff, qwebp, qjp2) | LGPL |
| HEIC/HEIF, AVIF, JPEG XL, RAW, PSD, XCF, EXR, Krita, QOI | KDE `kimageformats` | `kimageformat-plugins` 5.115 (installed, already Recommended by the .deb) | **not in the live MSYS2 repo** — add pinned from the MSYS2 archive like the KF5 packages, or build the plugins against MSYS2's libheif 1.23 / libavif 1.4 / libjxl 0.13 / libraw 0.22 | LGPL (libde265 for HEVC: LGPL, patent caveat) |
| Metadata (EXIF, IPTC, XMP: camera, lens, GPS, rating, orientation) | **Exiv2** | `libexiv2-dev` 0.27.6 (or `libkf5kexiv2-dev`) | `exiv2` 0.28.9 | GPL-2.0+ (fine, Panero is GPL) |
| Lossless JPEG rotate/flip/crop | **libjpeg-turbo** TurboJPEG API (`tj3Transform`) | `libturbojpeg0-dev` 2.1.5 | `libjpeg-turbo` 3.2.1 (installed) | BSD-style |
| Colour management (ICC profiles, monitor profile) | **lcms2** | `liblcms2-dev` 2.14 | `lcms2` 2.20 (installed) | MIT |
| Basic edits (crop, resize, rotate, brightness, contrast, saturation, levels) | Qt `QImage` + own code (a few hundred lines) | — | — | — |
| Very large images (100+ MP, panoramas), fast high-quality resize for batch | *optional later:* **libvips** | `libvips-dev` 8.15 | `libvips` 8.18 | LGPL |
| PDF pages (already for Quick View) | Poppler-Qt5 | yes | `poppler-qt5` 26.10 | GPL |

Why this one: everything is available on both platforms, the binary stays
small, Panero keeps full control of the UI (Panero look, keys, cloud rules),
and it uses the same Qt/KF5 base as the rest of Panero.

### 4.2 Alternatives considered

| Option | For | Against | Verdict |
|---|---|---|---|
| Embed **Gwenview's KPart** | Mature viewer for free on Linux | Linux only, only when Gwenview is installed, its own keys/look, no culling/file-ops integration, no control over cloud reads | Not as the main viewer; could stay as "Open in Gwenview" |
| **ImageMagick / GraphicsMagick (Magick++)** | Every format and operation (PhotoX uses Magick.NET) | Very large dependency, slower for viewing, security history with untrusted files | Only if a feature really needs it |
| **OpenCV** | Powerful processing | Huge, not a viewing library | No |
| **Separate program** (from scratch or based on PhotoX) | Could be a standalone photo app | Duplicates browsing/thumbnails/file ops; PhotoX is C#/.NET/Avalonia + Magick.NET and studio-specific (printing, ID photos, copies) — none of its code fits Panero's C++/Qt | No; borrow PhotoX's *ideas* (5.4, 7) |
| **Windows WIC** for formats on Windows | HEIC/RAW with codecs the user installed from Microsoft | Windows-only code path, depends on Store extensions | Possible fallback for HEIC on Windows |

### 4.3 Qt 5 now, Qt 6 later

Panero is on Qt 5 / KF5 (D-001, D-007). Keep the viewer code free of
deprecated Qt 5 APIs so a later Qt 6 port is mechanical.

## 5. Feature catalogue

Priority suggestion: **M** = must (phase 1–2), **S** = should, **C** = could
(later). The owner decides in section 11.

### 5.1 Viewing

| Feature | Prio | Notes |
|---|---|---|
| Open with F3, Enter or double-click on an image (configurable) | M | F3 = Panero's view key; Enter could keep "open with default app" |
| Viewer as its own window, or full screen (F11 / F), or inside the panel (Quick View) | M | remembers size/position |
| Next / previous in the panel's order (Space/Backspace, arrows, PgUp/PgDn, wheel), Home/End | M | uses the panel's sort and filter; option: only marked files |
| Fit, 100 %, zoom with wheel/+/-, zoom to cursor, pan by drag | M | shared with Quick View |
| EXIF orientation applied automatically | M | |
| Animated GIF/WebP/APNG, multi-page TIFF (page keys) | M | |
| Preload next/previous, decode in background, downscaled first for huge images | M | `QImageReader::setScaledSize`, tiles for very large |
| Info panel (I): size, type, camera, lens, exposure, date, GPS (link to a map) | S | Exiv2 |
| Histogram | C | |
| Film strip of thumbnails at the bottom | S | uses the existing thumbnail jobs |
| Slideshow with interval, shuffle | C | |
| Compare two images side by side (synchronized zoom/pan) — or one per panel | S | Panero's two panels make this natural |
| Background: theme / black / checkerboard for transparency | S | |
| Colour management (embedded ICC → monitor profile) | C | lcms2 |
| RAW: show the embedded preview at once, full decode on demand | S | LibRaw via kimageformats |

### 5.2 File actions inside the viewer

| Feature | Prio | Notes |
|---|---|---|
| Delete (Del/F8, to Trash/Recycle Bin) with Undo, jumps to the next image | M | |
| Copy / move to the other panel (F5/F6) | M | |
| Rename (F2), with Multi-Rename for marked files | M | |
| Mark / unmark the image (Insert or a key of choice) — marks show in the panel | M | culling (5.3) |
| Open with… / external editor, show in folder (back to the panel at that file) | M | |
| Copy image to clipboard, drag the file out | S | drag uses the portal on Linux (preview 13) |
| Rating 1–5 / colour label (XMP sidecar or embedded) | C | Exiv2; shown as a panel column? |
| Set as wallpaper, print | C | |

### 5.3 Culling workflow (the Panero speciality)

Look through a folder of photos at full size, press a key to **mark** the
good ones (or the bad ones), then close the viewer: the marks are in the
panel, so **F5** copies the keepers to the other panel, **F8** deletes the
rejects, **Ctrl+M** renames them by date. Optional: "only marked" mode to
review the selection again. No other file manager does this with two panels.

### 5.4 Basic editing

| Feature | Prio | Notes |
|---|---|---|
| Rotate 90°/180°, flip — **lossless** for JPEG (no quality loss) | M | TurboJPEG `tj3Transform`; or only the EXIF orientation tag (option) |
| Crop: free, fixed ratios (1:1, 3:2, 4:3, 16:9), photo sizes (9×13, 10×15, 13×18, 15×20 — from PhotoX), lossless crop for JPEG on 16-px boundaries | S | |
| Resize / scale (pixels, %, long side), with good resampling | S | QImage smooth or libvips |
| Straighten (small angle) | C | |
| Light & colour: exposure/brightness, contrast, saturation, white balance (warm/cool), auto | S | non-destructive while editing, applied on save |
| Levels / curves | C | |
| Blur or black out an area (privacy) | C | |
| Save model: **Save as copy** (default) / **Overwrite, keep original** in an `Original` subfolder (PhotoX idea) or in the Trash; keep EXIF; JPEG quality kept close to the source | M | decide in 11 |
| Keyboard flow: Enter = save & next (PhotoX) | S | |

### 5.5 Batch (marked files, from the panel)

| Feature | Prio | Notes |
|---|---|---|
| Rotate by EXIF (normalize), lossless where possible | S | |
| Resize to long side / size, convert format (JPG/PNG/WebP/AVIF, quality) | S | one dialog, progress in the jobs window (preview 13) |
| Remove metadata / only GPS | S | privacy before sharing |
| Multi-Rename with EXIF patterns: `[EXIF:Date]`, `[EXIF:Camera]`, … | S | small addition to the existing Multi-Rename |
| Contact sheet / simple collage | C | |

## 6. Cloud, performance and safety rules

- Cloud/network files: nothing is read until Space/click; the viewer shows
  name, size and "Press Space to load". Next/previous does not preload cloud
  files. Windows placeholders never hydrate automatically (D-020).
- All decoding, metadata reading and saving in worker threads; the GUI
  thread never touches a slow path (D-018).
- Memory: keep at most current ± 1 decoded at screen size; full resolution
  only when zoomed in; downscaled decode (`setScaledSize`) for large files.
- Saving is atomic: write to a temporary file in the same folder, then
  rename; the original is kept as chosen in 5.4.
- Untrusted files: decoders run with Qt's/KDE's limits (allocation limit,
  `QImageReader::setAllocationLimit`); a crash-prone format can be disabled.

## 7. User interface sketch

```
┌ photo_0123.jpg — 12 / 248 — 6000×4000, 8.4 MB ───────────────── ─ □ × ┐
│ ◀ ▶   ⤢ Fit  100%  ⟲ ⟳   ✂ Crop  ☀ Adjust   ★ Mark   🗑   ⓘ   ⛶       │
├────────────────────────────────────────────────────────────┬──────────┤
│                                                            │ Camera   │
│                        (image)                             │ Lens     │
│                                                            │ 1/250 f4 │
│                                                            │ ISO 200  │
│                                                            │ 2026-08… │
├────────────────────────────────────────────────────────────┴──────────┤
│ [thumb][thumb][THUMB][thumb][thumb][thumb]  …  (film strip)            │
└────────────────────────────────────────────────────────────────────────┘
```

Proposed keys (Total Commander / IrfanView / Windows Photos habits):

| Key | Action |
|---|---|
| Space / → / PgDn / wheel down | next image (Space: next — or mark, see 11) |
| Backspace / ← / PgUp / wheel up | previous image |
| Home / End | first / last |
| + / - / Ctrl+wheel, 0, 1 | zoom in/out, fit, 100 % |
| F / F11, Esc | full screen, close (back to the panel at this file) |
| R / Shift+R (or Ctrl+R / Ctrl+L) | rotate right / left |
| Insert (or M) | mark / unmark |
| Del / F8, F5, F6, F2 | delete, copy, move, rename |
| I | info panel |
| C | crop, E edit/adjust, Enter save & next (in edit mode) |

## 8. Architecture (for the implementation)

```
app/ImageViewer/
  imageloader.*      background decoding (shared with Quick View), preloading,
                     cloud rule, downscaled decode, animation
  imageview.*        the zoom/pan widget (Quick View's QuickImageView, moved here)
  metadata.*         Exiv2 wrapper: read EXIF/XMP, orientation, rating; write rating/orientation
  viewerwindow.*     window, toolbar, film strip, info panel, keys, full screen
  navigator.*        the panel's file list, order, filter, marks; next/previous
  edits/             non-destructive operation list (rotate, crop, resize, adjust)
                     applied to the preview live and to the full image on save
  savejob.*          atomic save, original kept, EXIF kept, lossless JPEG path (TurboJPEG)
  batch/             batch dialog + job (resize, convert, strip metadata)
```

Integration points: F3/Enter dispatch in `panelfunc.cpp`/`KrViewer`
(images → viewer, other types unchanged); file operations through the
existing panel functions so Undo, jobs window and selection behave the same;
marks through the panel's view; Multi-Rename gets EXIF tokens.

Tests: unit tests for metadata, edit operations (pixel checks), lossless
rotate (byte-exact DCT check via TurboJPEG), save/keep-original; Linux window
tests under Xvfb (open, next/previous, mark → F5, delete → undo); Windows
GUI test in the VM (D-022) and on the GitHub runner.

## 9. Phases and rough effort

Effort in "sessions" like a preview of Panero (a preview ≈ 1 session of
focused work incl. tests); rough, before requirements are fixed.

| Phase | Content | Effort |
|---|---|---|
| 0 | Requirements (section 11), mockup page with 2–3 layouts | small |
| 1 | Viewer window: open from F3/Enter, next/previous, zoom/pan, full screen, film strip, file actions (F5/F6/F8/F2, Undo), cloud rule; formats as in 4.1 on Linux | 1–1.5 |
| 2 | Culling (marks, only-marked mode), info panel (Exiv2), compare, Windows formats (kimageformats or WIC) | 1 |
| 3 | Lossless rotate/flip/crop, crop with presets, resize, light & colour, save model | 1–1.5 |
| 4 | Batch resize/convert/strip metadata, Multi-Rename EXIF tokens | 0.5–1 |
| 5 | Colour management, RAW preview-first, slideshow, ratings, histogram | 1 (pick and choose) |

## 10. Risks and open points

- **Windows formats:** kimageformats is not in the live MSYS2 repository;
  pinning it from the archive (like KF5) or building the plugins is needed for
  HEIC/AVIF/JPEG XL/RAW on Windows. HEVC (HEIC) has patent questions;
  libde265 is LGPL. Alternative: Windows' own WIC codecs (HEIF extension).
- **Linux TIFF/WebP:** `qt5-image-formats-plugins` must become a dependency
  (it is missing on the owner's PC today).
- **Package size:** Exiv2 + TurboJPEG + lcms2 are small (a few MB); libvips
  adds more (only if large-image/batch performance needs it).
- **Metadata writing** (rating, orientation) changes files — must follow the
  same "never lose a photo" rule (backup or sidecar).
- **Scope creep:** this is a viewer with light editing, not a photo editor;
  anything beyond 5.4 belongs in "Open with" (GIMP, darktable, Photos).
- **Qt 6 port** later: keep the code Qt-6-ready.

## 11. Requirements questionnaire (fill in)

Tick one or more, add notes freely.

**A. Opening**
- [ ] F3 opens images in the viewer
- [ ] Enter opens images in the viewer (instead of the system's default app)
- [ ] Double-click opens images in the viewer
- [ ] Viewer opens as: [ ] separate window [ ] full screen [ ] inside the panel (bigger Quick View)
- Notes:

**B. Navigation**
- [ ] Next/previous follow the panel's sort order and filter
- [ ] Option "only marked files"
- [ ] Space = next image  /  [ ] Space = mark (TC style)  — choose one
- [ ] Film strip of thumbnails
- Notes:

**C. File actions in the viewer**
- [ ] Delete (to Trash) + Undo   [ ] Copy/Move to other panel   [ ] Rename
- [ ] Mark for later (culling)   [ ] Rating/labels (stored in the file / sidecar)
- Notes:

**D. Information**
- [ ] Info panel (camera, lens, exposure, date)   [ ] GPS with map link   [ ] Histogram
- Notes:

**E. Editing** (which ones, and how important 1–3)
- [ ] Lossless rotate/flip ___   [ ] Crop (free/ratios) ___   [ ] Crop presets (photo sizes) ___
- [ ] Resize ___   [ ] Brightness/contrast/saturation ___   [ ] Auto enhance ___
- [ ] Straighten ___   [ ] Blur/redact area ___   [ ] Levels/curves ___
- Notes:

**F. Saving edits**
- [ ] Save as a copy (name_edited.jpg) by default
- [ ] Overwrite, original kept in an `Original` subfolder (PhotoX style)
- [ ] Overwrite, original to the Trash
- [ ] Ask every time
- [ ] Enter = save & next
- Notes:

**G. Batch (marked files)**
- [ ] Resize   [ ] Convert format (which: ____ )   [ ] Remove metadata/GPS
- [ ] Rename with EXIF date/camera in Multi-Rename
- Notes:

**H. Formats that must work** (others: best effort)
- [ ] JPEG/PNG/GIF/WebP   [ ] HEIC (iPhone)   [ ] AVIF   [ ] RAW (which cameras: ____ )
- [ ] TIFF (multi-page)   [ ] PSD/XCF   [ ] SVG   [ ] JPEG XL
- Must they work on Windows too? [ ] yes, all  [ ] only the common ones

**I. Who is it for first?**
- [ ] Beginners looking at photos   [ ] Photographers sorting shoots
- [ ] Studio-like quick edits (PhotoX ideas)   [ ] General "view anything" use
- Notes:

**J. Look**
- [ ] Panero light/dark theme   [ ] Always dark background for photos
- [ ] Minimal (toolbar hides in full screen)   [ ] Everything visible
- Notes:

---

*Sources checked on 2026-11-24: Ubuntu 24.04 package versions on the
development PC (`apt-cache policy`), image plugins installed there, MSYS2
UCRT64 package list in the Windows VM, Panero's Quick View branch
(`feature/p13-quickview`), PhotoX README (C#/.NET 10, Avalonia 12.1,
Magick.NET 14.17).*
