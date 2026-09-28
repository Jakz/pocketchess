# libs/ — pinned third-party snapshots

Builds never download anything (build doc §2). Updating a library = replace the snapshot, update
this table, rebuild **both** targets.

| Dir | Library | Version / pin | Licence | Used by | Status |
| --- | --- | --- | --- | --- | --- |
| `sdl3/` | SDL 3 | same snapshot as Enigmistica (`scripts/vendor-libs.ps1` copies it) | Zlib | Windows app (`PocketChess.exe`) | **to vendor** — run `scripts/vendor-libs.ps1` |
| `sqlite/` | SQLite amalgamation | latest 3.x | Public domain | storage (M3/M4) | deferred to M3 |
| `yaml/` | yaml-cpp 0.9.0 (Enigmistica's snapshot) | — | MIT | themes, descriptors (M2/M6) | deferred to M2 |
| `zstd/` | zstd decoder | — | BSD | PuzzleImporter (M4) | deferred to M4 |
| `nanosvg/` | nanosvg + nanosvgrast | — | Zlib | AssetConverter (M6) | deferred to M6 |
| `stockfish/` | Stockfish 19 + NNUE | `projects/pocketbook/engines/stockfish/engine.lock` | GPL-3.0 | engine plug-in | **local only, not in Git** (D15) — `scripts/fetch-engines.ps1` |

Not needed any more (ADR 0005): `stb/` (own TrueType rasterizer and PNG writer in `src/ui/canvas`)
and `doctest/` (own doctest-compatible test framework in `tests/support`). `stb_image` comes back
with PNG piece sets in M6.

Fonts are data, not libraries: `data/fonts/DejaVuSans*.ttf` (Bitstream Vera licence + public
domain changes, see `data/fonts/LICENSE-DejaVu.txt`).
