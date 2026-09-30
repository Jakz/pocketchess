# ADR 0012 — SVG piece sets rasterized on the device

- Status: Accepted (2026-09-30). Amends the M6 plan (`AssetConverter`, PNG sets per size) and the
  architecture doc §13 piece-set row; plan IDs D9, P2.

## Context

The plan converted SVG piece sets to PNGs at a list of square sizes with a desktop `AssetConverter`
(nanosvg) and decoded the PNGs on the reader, which needs a PNG decoder (`stb_image` or our own
inflate), a size list that has to match every layout (1072-px color board, 758-px mono profile,
future devices) and a desktop step for every set a user imports.

## Decision

- Vendor nanosvg + nanosvgrast (zlib licence, pinned in `libs/README.md`) and compile it into the
  UI library of **both** builds (`src/ui/theme/NanoSvg.cpp`, warnings off).
- A piece set is a folder `assets/pieces/<id>/` with `pieceset.yaml` (name, author, licence,
  source, optional `scale`, `stroke_scale`, `base`) and the twelve Lichess-named SVGs
  `{w,b}{K,Q,R,B,N,P}.svg`. `SvgPieceSet` parses the SVGs once when the set is chosen and rasterizes
  each piece at the exact square size on first use; `PieceCache` keeps the bitmaps, so nothing is
  rescaled on redraw (design notes §18.1).
- `stroke_scale` + `base` give derived sets without copying files: **Bold** is Cburnett with 1.8x
  strokes, the high-contrast e-ink set of design notes §18.
- Bundled: Cburnett, Merida (GPL-2.0-or-later), Chessnut (Apache-2.0), Bold. The DejaVu glyph set
  stays built in as the fallback when a set is missing or broken (§32) and is what tests use.
- No `AssetConverter` and no image decoder.

## Consequences

- Any board size works, including profiles not known at build time, and Import (M6) can install a
  set by copying its folder.
- nanosvg uses float math and libm (`sqrtf`, `cosf`, `atan2f`); the no-FMA flags keep x64 and ARM
  close, but bit-identity is not guaranteed across C libraries. Golden images therefore keep using
  the glyph set (test roots have no `assets/pieces`); SVG sets are covered by coverage/contrast
  tests instead of hashes.
- Loading costs a parse of 12 small SVGs (logged as "pieces: set X loaded in N ms") plus one
  rasterization per piece and size; to be measured on the reader with the other deferred checks.
