# ADR 0002 — Software canvas and the InkView present path

- Status: Proposed (2026-09-28) — confirm with the M0.5 Diagnostics measurements. Implements P2.

## Decision

- Every screen draws into `ui::Canvas` (24-bit RGB, device resolution). Views invalidate rects
  with a hint (Selection / Content / Screen); `Application::flush()` redraws the dirty rects once
  per event; `RefreshPolicy` maps hints to Fast / Partial / Full (full every 12 content updates,
  on screen changes and when > 50 % of the screen is dirty).
- Board changes are computed as a per-square diff of `BoardVisual` (`affectedSquares`): a quiet
  move redraws 2 squares, castling 4, en passant 3, check adds the king square, the previous
  last-move squares are cleared.
- InkView presenter (initial): `Stretch(rgb, IMAGE_RGB, w, h, stride, x, y, w, h, 0)` per rect
  (Enigmistica's proven fallback), then `PartialUpdate` per rect, `FullUpdate` for Full. Fast is
  mapped to `PartialUpdate` until the latency/ghosting of `DynamicUpdate` and the BW variants is
  measured on the Kaleido 3 panel.
- Determinism: all pixel math is integer except the rasterizer's float accumulation, which is
  compiled without FMA contraction (`-ffp-contract=off`, `/fp:precise`). Golden images compare
  64-bit canvas hashes; the device self-test checks the same hashes on ARM.

## To measure (M0.5)

`GetCanvas()` depth/scanline, latency of PartialUpdate (1 square, 12 squares), FullUpdate and
DynamicUpdate in color and mono content, and ghosting by eye. If direct writes to
`GetCanvas()->addr` are much faster than `Stretch`, add that path to `InkViewPresenter` only.
