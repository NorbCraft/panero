# Panero Image Viewer — review of the brief and proposed plan

Status: **waiting for the owner's decisions** (2026-11-25). Nothing is
implemented. The interactive version of this review, where the answers are
chosen and saved, is the private decision page
<https://claude.ai/artifact/8MXmejL9MsN6eGHGkYzyDc>. This file is the same
content for reading on GitHub. Once the decisions are in, they become
`D-023` in `dev/DECISIONS.md` and the approved plan replaces the content of
`dev/IMAGE-VIEWER-PLAN.md` (the exploratory workbook from 2026-11-24 on the
preview 13 branch).

Brief reviewed: `PANERO-IMAGE-VIEWER-PROMPT.md` with its mockups and icon
(handoff folder outside the repository).

## 1. Repository review

Inspected: `feature/preview13` at `4f90473fe` (everything in preview 13:
Quick View, copy windows, Undo, Settings window, Windows work). `main` is
still at preview 12. No viewer code exists yet.

| Need | What is there | Reuse |
|---|---|---|
| Image canvas | `QuickImageView` (`app/QuickView/paneroquickviewwidgets.*`): fit, zoom, pan, `+ - 0 1`, animated GIF/WebP | Extract the drawing/zoom core into a shared canvas; each window keeps its own gesture policy (Quick View vs viewer: wheel browses, Ctrl+wheel zooms to the pointer) |
| Decoding off the GUI thread | `ImageResult` loader in `paneroquickviewdata.*` (scaled first frame, EXIF date), cloud rule | Grow into a bounded decode queue with stale-result tokens |
| Opening by double-click | Double-click and Enter both end in `ListPanelFunc::execute()` via `KrMouseHandler` → `emitExecuted` | Pass how it was opened, so only double-click (and F3, see D3) opens the viewer |
| Panel order, filter, marks | `KrView::getFirst/getNext` (sorted, filtered), `setSelected/isSelected` (red marks), `setCurrentItem` | The viewer takes the visible images in that order; marks always go through `KrView` |
| Trash, confirmation, Undo | `ListPanelFunc::deleteFiles(true)`, jobs windows and `PaneroUndo` (preview 13) | The viewer's Delete uses the same path |
| Source panel and tab | `ListPanel` objects survive tab moves; `PanelManager` | Guarded pointer to the source panel + folder URL; fallbacks in D4 |
| Date Taken column | `KrViewProperties::ColumnType` ends at `Accessed = 0xa` | `DateTaken = 0xb` at the end keeps saved layouts valid |

Conflicts with the brief: the MagickWand direction (D1, the Ubuntu facts
below change it) and "keep Enter and F3 as in the proposal" (D3).

## 2. Verified on Zorin 18.1 / Ubuntu 24.04

Qt 5.15 with KDE's image plugins (`kimageformat-plugins` 5.115, already
recommended by Panero's package) versus the system ImageMagick 6.9.12-98.
A HEIC and an AVIF decoded through Qt in 86 ms and 11 ms.

| Format | Qt + KDE plugins | ImageMagick 6 (Ubuntu) | Exiv2 0.27.6 |
|---|---|---|---|
| JPEG, PNG, BMP, GIF | read / write | read / write | JPEG, PNG read+write |
| TIFF, WebP | read / write with `qt5-image-formats-plugins` (preview 13's package depends on it) | read / write | read+write |
| HEIC/HEIF, AVIF | read / write (libheif 1.17) | read / write | read (BMFF compiled in); HEIC metadata cannot be written in 0.27 |
| JPEG XL | read / write | not supported | read |
| Camera RAW (CR2, CR3, NEF, ARW, DNG, RAF, ORF, RW2, ~45 types) | read through LibRaw 0.21, no extra program | decodes through `ufraw-batch`, which Ubuntu no longer ships: fails | CR2/NEF/ARW/DNG; CR3 via BMFF |
| Limits | — | `policy.xml`: 1 GiB memory, 256 MP, 32k px per side | — |
| Size | installed | MagickCore + Wand ≈ 8 MB + delegates | ≈ 3 MB, installed |

The handoff's cloud test (HEIC + CR2) used ImageMagick 7 with LibRaw built
in; Zorin's ImageMagick 6 behaves differently.

**Taskbar identity, verified** in a headless GNOME Shell 46 under Wayland:
one Panero process can give the viewer window the app id
`io.github.norbcraft.panero.imageviewer` (public Qt API: the desktop file
name is set while the viewer's native window is created), while the main
window and later dialogs keep `io.github.norbcraft.panero`. With a hidden
`io.github.norbcraft.panero.imageviewer.desktop` the viewer gets its own
taskbar entry and teal icon; X11/XWayland: its own `WM_CLASS`. Still to
check on the real desktop: the entry itself, Alt+Tab and the taskbar click.

## 3. Decisions needed (recommended answer first)

| # | Question | Recommended | Alternatives |
|---|---|---|---|
| D1 | Imaging library | Qt + KDE plugins for decode/encode, own small edit pipeline (one recipe for preview and export), Exiv2 for metadata, Qt `QColorSpace` for colour profiles; no ImageMagick | MagickWand as in the brief; Qt + MagickWand only for export resizing |
| D2 | Process | One process, viewer is a top-level window with its own taskbar identity | Separate viewer process over D-Bus |
| D3 | Enter / F3 on an image | Double-click and F3 open the viewer; Enter keeps the default app; Open With lists the viewer | Only double-click; or double-click, Enter and F3 |
| D4 | Returning when things changed | Follow the moved tab; tab closed → open the folder in the active panel; land on the nearest surviving photo; no photo left → the viewer closes and says so; Quick View follows the restored row | If the source tab is gone, close without touching panels |
| D5 | No Trash (some network/cloud drives) | Ask "Delete permanently?", Cancel is the default | Disable Delete there |
| D6 | Fixed Size crop | After the first viewer release; if it does not fit, offer the largest rectangle of that shape and show the effective DPI | First release, refuse when it doesn't fit; leave it out |
| D7 | Paste Previous Edit | Last *saved* edit of the session; tone and all geometry travel; crop stored relative and adapts by keeping its ratio; one Undo step; session only | Last applied edit, tone only; remembered after closing |
| D8 | Key for the momentary Before | Hold B | Hold `\` (Lightroom) |
| D9 | Replace Original recovery | Original to the Trash first (restorable, Ctrl+Z), then replaced; not offered where there is no Trash | `Originals` subfolder (PhotoX style); no backup |
| D10 | Saving HEIC and RAW edits | Copy only; JPEG (quality 92) with all metadata by default; HEIC as an option with a "metadata not kept" note | Copy only, JPEG, no HEIC output |
| D11 | RAW test files | CC0 samples: Canon CR2 + CR3, Nikon, Sony, Fujifilm, DNG, iPhone HEIC (never private photos) | Only the cameras the owner names |
| D12 | Multi-frame, swipe, touch | Animations play (no editing), TIFF pages via page buttons; two-finger sideways swipe browses; drag pans when zoomed | First frame only, no swipe |
| D13 | Date Taken sort | Camera clock time (date + time + subseconds), time zones ignored; ties by name; undated after dated, by Modified; on cloud drives only for files already read | UTC where an offset exists |
| D14 | Photo mode idea | A per-panel "Photo view" toggle (thumbnails, Date Taken order, images and videos only) after the viewer and Date Taken | Full Photo mode for the window later; not needed |
| D15 | Standalone harness | Yes, `imageviewer-harness` in the repo, built with the tests, not shipped | Build inside Panero directly |
| D16 | First delivery | Preview 14: viewer, marks, Trash, Info, Date Taken; preview 15: the editor | Everything in one preview |

Assumptions that let planning continue: Esc in Edit mode leaves the editor
(with the Save / Discard / Stay guard), Esc in View closes the viewer; the
toolbar overflows into a menu when narrow; JPEG quality 92 unless the
source's quality can be estimated; capture time is never changed on save.

## 4. Phased plan

New module `app/ImageViewer/` (static library like `app/QuickView/`), the
`imageviewer-harness` next to it, a few hooks in Panero. Linux first.

| | Content | Done when |
|---|---|---|
| M0 | Module skeleton; shared canvas from Quick View; decode service (bounded queue, screen-sized decode, stale tokens, memory cap, cloud guard); harness; fixture generator (JPEG/PNG/TIFF/WebP/HEIC/AVIF with EXIF, orientations 1–8, a corrupt file) | harness browses 500 fixtures with no GUI-thread frame over 16 ms; Quick View still passes |
| M1 | Viewer window: layout C toolbar (agreed order, overflow menu), Fit/100%/pan, wheel browse with stop-at-last, Space/Backspace wrap, thumbnail strip (filenames, centred, blue frame, remembered), full screen (F11, exit control), own taskbar identity + teal icon; opening from the panel order; return to the panel row | window tests from both panels, Quick View on/off, filter active; correct return row; headless GNOME Shell app id; owner checklist on Zorin |
| M2 | Mark checkbox, Insert, thumbnail checkboxes after the first mark, live sync with the panel's marks, Delete via Panero's delete + Undo, D4/D5 cases | marks match the panel; cancelled/failed delete keeps list and marks; last/only photo as decided |
| M3 | Exiv2 adapter (0.27 Ubuntu, 0.28 Windows later), Info tabs General/EXIF/IPTC/XMP/ICC, grouped EXIF, cache | known tags shown; cloud file shows nothing until loaded |
| M4 | Editor Tone: recipe + one apply function, preview in a worker, histogram with handles, exposure/contrast/saturation, White Balance, Before (button + key, released on focus loss), Undo/Redo, reset, guard | preview vs full export within 1 level per channel; Before restores on release and focus loss |
| M5 | Crop & Rotate: handles, ratios, thirds grid, output px, 90°, flips, straighten; Fixed Size per D6 | exact expected crop rectangles at every orientation |
| M6 | Save: copy default, replace per D9, staged write + read-back + atomic rename, collision-safe names, format/quality/resize, metadata + ICC kept, Orientation normalised, D10; Replace only on local paths, symlinks to the target's folder, cloud/remote copy only | forced write failure leaves the original byte-identical and the edit open; EXIF and capture-time sort survive |
| M7 | Paste Previous Edit per D7 | portrait/landscape/square targets give expected crops; one Undo removes it |
| M8 | Date Taken column (separate PR): `DateTaken = 0xb`, background reader shared with M3, cache by path/size/mtime, sort per D13, cloud guard; Photo view per D14 | mixed folder (three cameras, undated, HEIC, CR2) sorts as specified; 2,000 photos scroll smoothly; nothing read on cloud mounts |
| M9 | Packaging: build-dep `libexiv2-dev`; `kimageformat-plugins` to Depends; icon + provenance; hidden viewer `.desktop`; `test-deb.sh` decodes HEIC/AVIF/JXL/RAW in a clean container | package test green; Zorin checklist (Wayland + XWayland) signed off |
| W | Windows follow-up (planned only): per-window AppUserModelID for its own taskbar button; KDE image plugins pinned from the MSYS2 archive or built against MSYS2's libheif/libraw/libjxl; Exiv2 0.28; Windows' HEIC codec as fallback; Recycle Bin path from preview 13 | — |

## 5. Verification

Isolated settings (temporary HOME/XDG), synthetic or licensed fixtures only;
Xvfb window tests like the existing `gui-*-test.sh`; headless GNOME Shell for
the app id; a short manual checklist on Zorin.

- Opening with Quick View on/off; order and filter in both panels — compare the viewer's list to the panel.
- Marks via checkbox and Insert persist — read the panel's marks after each action and after closing.
- Focus returns to the right panel, tab and row — also with the tab moved or closed.
- Trash cancel / failure keep marks — cancelled confirmation, read-only file.
- Full screen and own taskbar entry — F11 and exit control; app id; Zorin checklist.
- Before hold/release, also on focus loss — widget test.
- Paste/Undo on different sizes; crop geometry; preview equals export — unit tests on the recipe.
- EXIF kept and capture-time sorting — save fixtures, read back with Exiv2, sort in the panel.
- Corrupt files, huge images, bounded loading — truncated JPEG, 20,000 px PNG, memory and queue assertions.
- Failed save keeps the original — read-only folder and forced disk-full, checksum before/after.
- Codecs in the installed package — `test-deb.sh` in a clean Ubuntu container.

## 6. Repository and handoff

- Base `main` after preview 13; branch `feature/image-viewer`; Date Taken on
  `feature/date-taken`; one pull request per milestone.
- Decisions → `D-023`; approved plan → `dev/IMAGE-VIEWER-PLAN.md`;
  milestones → `dev/PLAN.md` Phase 18; progress → `dev/LOG.md`.
- In the product: `app/ImageViewer/`, the teal icon (SVG + PNGs) with
  provenance in `dev/ICON-PROVENANCE.md`, the hidden viewer `.desktop`,
  generated fixtures and tests, Exiv2 in `debian/control` and licence notes,
  translatable strings through Panero's i18n.
- Kept out: mockups, prototype scripts, binaries, private photos; licensed
  RAW samples are downloaded by the test script with a checksum.
- Next starting point for any agent: read `D-023` and
  `dev/IMAGE-VIEWER-PLAN.md`, then start M0 on `feature/image-viewer`.
