# ADR 0005 — Self-contained text rendering, PNG output and test framework

- Status: Accepted (2026-09-28). Amends P2 (stb_truetype), P9 (doctest) and the Noto Sans font choice.

## Context

The implementing session had no network access and could not read Enigmistica's `libs/`, so the
planned stb headers, doctest and Noto Sans were not available. Everything else could be built and
verified, so the dependencies were replaced by small in-repo code instead of waiting.

## Decision

- **TrueType**: `ui/canvas/TrueTypeFont` (cmap 4/12, simple + composite glyphs, hmtx) and
  `ui/canvas/Rasterizer` (signed-area coverage accumulation, quadratic flattening). ~600 lines,
  deterministic, verified against reference renders; no hinting, no kerning (not needed at these sizes).
- **PNG**: `ui/canvas/PngWriter` (fixed-Huffman deflate with LZ77, per-row filter choice, CRC,
  Adler) for screenshots and golden images. Decoding PNG piece sets comes with M6 (`stb_image`
  or a small inflate).
- **Font**: DejaVu Sans / Sans Bold (`data/fonts`, Bitstream Vera licence + public domain changes).
  It also provides the Unicode chess glyphs, so the **interim piece set** (`GlyphPieceSet`) is
  drawn from U+2654–265F with the enclosed area flood-filled white. Noto Sans can replace the UI
  font later by dropping the TTF in and changing two file names (it has no chess glyphs, so keep
  DejaVu for the glyph set).
- **Tests**: `tests/support/Test.h`, a doctest-compatible subset (`TEST_CASE`, `CHECK`,
  `CHECK_EQ`, `REQUIRE`, `MESSAGE`, `[slow]`/`[integration]` tags) with pluggable reporters, used
  by `PocketChessTests.exe` and `pocketchess-selftest.app`. Switching to real doctest later only
  needs the header swap if ever wanted.

## Consequences

- No third-party code in the device binary except libc++ (Zig) and InkView.
- The golden hashes depend on the exact font file bytes and on the no-FMA rule (ADR 0002).
