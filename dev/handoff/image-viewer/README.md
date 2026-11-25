# Panero Image Viewer — visual references

Design studies made with the owner before planning (Linux, light and dark).
They show the intended layout and behaviour; the implementation follows
`dev/IMAGE-VIEWER-PLAN.md` and Panero's own Qt styling and icons, so it does
not have to match these pictures pixel for pixel. Camera data shown in them
is sample data. These files are planning references, not product assets
(except the icon, see below).

| File | What it shows | Notes |
|---|---|---|
| `mockups/01-toolbar-and-tone.jpg` | **Layout C** (the chosen one): viewer and editor, light and dark; the toolbar grouping and the Tone panel with the histogram handles | This is the toolbar to build. Leave out the permanent numeric input boxes that earlier studies had |
| `mockups/02-before-and-paste.jpg` | The held **Before** button and the result of **Paste Previous Edit** ("Edits pasted from …") | |
| `mockups/03-crop-choices.jpg` | Crop & Rotate: **Aspect Ratio** mode and the later **Fixed Size** mode (mm at 300 DPI → 1,181 × 1,772 px) | |
| `mockups/04-marking-and-trash.jpg` | Marked photos (red checks on thumbnails), the blue current-photo frame, and the state after moving the current photo to Trash | Its toolbar is from an earlier iteration; use the order in the plan |
| `mockups/05-exif-and-save.jpg` | Tabbed **Image Properties** (General / EXIF / IPTC / XMP / ICC) and the **Save Edits** dialog | Ignore its older toolbar and the Before/After toggle; Before is one momentary button |
| `mockups/06-date-taken.jpg` | The **Date Taken** column in Panero's file panel, sortable, separate from Modified | |
| `mockups/07-full-screen.jpg` | Full screen: normal window chrome hidden, a clear exit control | |
| `mockups/08-folder-color-options.jpg` | Icon study: option **C · Both panels recolored** was chosen; the image viewer is the first symbol (teal) | Video (violet), PDF (red) and audio (amber) are future examples only |
| `icon/panero-image-viewer.svg` + 16–64 px PNGs | The **approved application icon** for the viewer (teal split folder with a white picture card, "C2") | Product asset: in milestone M1 move it into `app/icons/` with the rest of Panero's icons, render the sizes with Panero's icon pipeline (`dev/scripts/render-icons.sh` style) and record its provenance in `dev/ICON-PROVENANCE.md`. It is derived from Panero's `app/icons/panero/io.github.norbcraft.panero-small.svg` and keeps its GPL-2.0-or-later notice (`icon/GPL-2.0-or-later.txt`) |

The studies show an older blue corner badge in their title bars; use the teal
icon above.

## Photo credits (sample photos in the mockups)

From the Ubuntu wallpaper collections; file names in the mockups are
examples. Resized for display, cropped for thumbnails; the editor shows a
tone-adjusted copy of DSC2943 with a crop overlay. Adaptations keep the
original licences.

| Sample | Original | Photographer | Licence |
|---|---|---|---|
| DSC_1841.jpg | ryan-stone-skykomish-river.jpg | Ryan Stone | CC0 |
| DSC_1842.jpg | brad-huchteman-stone-mountain.jpg | Brad Huchteman | CC0 |
| DSC_1843.jpg | ubuntu_by_arman1992.jpg | Arman Esmaeili | CC BY-SA 4.0 |
| DSC_1844.jpg | ubuntu2_by_arman1992.jpg | Arman Esmaeili | CC BY-SA 4.0 |
| DSC_1845.jpg | DSC2943_by_kcpru.jpg | kcpru (Kacper Ślusarczyk) | CC BY-SA 4.0 |
| DSC_1846.jpg | Mirror_by_Uday_Nakade.jpg | Uday Nakade | CC BY-SA 4.0 |
| DSC_1847.jpg | Cherry_Tree_in_Lakones_by_elenastravoravdi.jpg | Elena Stravoravdi | CC BY-SA 4.0 |
| DSC_1848.jpg | Blue_flower_by_Elena_Stravoravdi.jpg | Elena Stravoravdi | CC BY-SA 4.0 |
| DSC_1849.jpg | Optical_Fibers_in_Dark_by_Elena_Stravoravdi.jpg | Elena Stravoravdi | CC BY-SA 4.0 |

Sources: Ubuntu artwork packages `ubuntu-wallpapers-jammy_22.04.4-0ubuntu1`
and `ubuntu-wallpapers-focal_20.04.2-0ubuntu1`;
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/),
[CC0](https://creativecommons.org/publicdomain/zero/1.0/).
