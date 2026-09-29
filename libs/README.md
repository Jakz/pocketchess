# libs/ — pinned third-party snapshots

Builds never download anything (build doc §2). Updating a library = replace the snapshot, update
this table, rebuild **both** targets.

| Dir | Library | Version / pin | Licence | Used by | Status |
| --- | --- | --- | --- | --- | --- |
| `sdl3/` | SDL 3.4.16 (`include/`, `src/`, licence only) | [release-3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16), commit `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` (Enigmistica's pin) | Zlib | Windows app (`PocketChess.exe`, as `SDL3.dll`) | copied from Enigmistica by `scripts/vendor-libs.ps1`, run automatically by the first build of `SDL3.vcxproj`; **not in Git** |
| `sqlite/` | SQLite 3.53.4 amalgamation (`sqlite3.c`, `sqlite3.h`) + our `pchess_sqlite_config.h` | [sqlite-amalgamation-3530400.zip](https://sqlite.org/2026/sqlite-amalgamation-3530400.zip), SHA3-256 `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` | Public domain | storage (M3+) | in Git (2026-09-29); options in `pchess_sqlite_config.h` |
| `yaml/` | yaml-cpp 0.9.0 (Enigmistica's snapshot) | — | MIT | themes, descriptors (M2/M6) | deferred to M2 |
| `zstd/` | zstd decoder | — | BSD | PuzzleImporter (M4) | deferred to M4 |
| `nanosvg/` | nanosvg + nanosvgrast | — | Zlib | AssetConverter (M6) | deferred to M6 |
| `stockfish/` | Stockfish 19 + NNUE | `projects/pocketbook/engines/stockfish/engine.lock` | GPL-3.0 | engine plug-in | **local only, not in Git** (D15) — `scripts/fetch-engines.ps1` |

Not needed any more (ADR 0005): `stb/` (own TrueType rasterizer and PNG writer in `src/ui/canvas`)
and `doctest/` (own doctest-compatible test framework in `tests/support`). `stb_image` comes back
with PNG piece sets in M6.

Fonts are data, not libraries: `data/fonts/DejaVuSans*.ttf` (Bitstream Vera licence + public
domain changes, see `data/fonts/LICENSE-DejaVu.txt`).
