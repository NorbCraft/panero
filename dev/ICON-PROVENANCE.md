# Panero icon provenance

## Final asset

| File | Purpose |
|---|---|
| `app/icons/panero/io.github.norbcraft.panero.svg` | master vector icon (editable source), used for ≥ 32 px and as the scalable icon |
| `app/icons/panero/io.github.norbcraft.panero-small.svg` | simplified variant (wider split, no sheen) for 16–24 px |
| `app/icons/panero/sc-apps-io.github.norbcraft.panero.svg` | copy of the master, named for `ecm_install_icons` |
| `app/icons/panero/<size>-apps-io.github.norbcraft.panero.png` | raster exports 16, 22, 24, 32, 48, 64, 128, 256 px |

Raster exports are generated with `dev/scripts/render-icons.sh`
(`rsvg-convert`) and committed so builds do not need SVG tooling.

## How it was made

- The owner selected **icon concept A** (blue/teal split twin-panel folder)
  during a design conversation. That concept raster,
  `dev/handoff/references/icon-a-reference.png`, was **AI generated** and
  serves only as visual direction.
- The final SVGs were drawn from scratch as plain geometry (rounded rectangles,
  one tab curve, linear gradients, a mask for the split) by the implementation
  agent (Claude, Anthropic) on 2026-11-17 for the Panero project. No pixels or
  traced paths from the reference raster, from Krusader/KDE logos or from any
  other application's artwork were used.
- The upstream Krusader application icons (`krusader_user`, `krusader_root`,
  etc.) were removed from Panero's install set; Panero does not use Krusader or
  KDE branding as its own.

## License / permissions

- The SVG/PNG files are distributed with Panero under GPL-2.0-or-later (as
  stated in their SPDX headers / `.reuse` metadata), so they can be modified
  and redistributed together with the program.
- No exclusive copyright or trademark rights are claimed. Copyright protection
  for AI-assisted or AI-generated design elements may be limited or
  unavailable depending on jurisdiction; the license grant above is given to
  the extent any rights exist.
- No trademark clearance has been performed for the name or the icon.
