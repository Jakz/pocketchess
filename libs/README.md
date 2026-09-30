# libs/ — pinned third-party snapshots

Builds never download anything (build doc §2). Updating a library = replace the snapshot, update
this table, rebuild **both** targets.

| Dir | Library | Version / pin | Licence | Used by | Status |
| --- | --- | --- | --- | --- | --- |
| `sdl3/` | SDL 3.4.16 (`include/`, `src/`, licence only) | [release-3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16), commit `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` (Enigmistica's pin) | Zlib | Windows app (`PocketChess.exe`, as `SDL3.dll`) | copied from Enigmistica by `scripts/vendor-libs.ps1`, run automatically by the first build of `SDL3.vcxproj`; **not in Git** |
| `sqlite/` | SQLite 3.53.4 amalgamation (`sqlite3.c`, `sqlite3.h`) + our `pchess_sqlite_config.h` | [sqlite-amalgamation-3530400.zip](https://sqlite.org/2026/sqlite-amalgamation-3530400.zip), SHA3-256 `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` | Public domain | storage (M3+) | in Git (2026-09-29); options in `pchess_sqlite_config.h` |
| `yaml/` | yaml-cpp 0.9.0 (Enigmistica's snapshot) | — | MIT | themes, descriptors (M2/M6) | deferred to M2 |
| `zstd/` | zstd 1.5.7 decoder as one file (`zstddeclib.c`, made with the release's `build/single_file_libs/combine.sh`, ASM off) + `zstd.h`, `zstd_errors.h`, `LICENSE` | [zstd-1.5.7.tar.gz](https://github.com/facebook/zstd/releases/tag/v1.5.7), SHA-256 `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3` | BSD-3-Clause | PuzzleImporter (desktop tool) | in Git (2026-09-29) |
| `nanosvg/` | nanosvg + nanosvgrast (`nanosvg.h`, `nanosvgrast.h`, `LICENSE.txt`, unmodified) | [memononen/nanosvg](https://github.com/memononen/nanosvg) commit `239e102ec2c691f2902e20ace2ed36ee4a35cfe6` (2026-07-09) | Zlib | SVG piece sets on both targets (`src/ui/theme/NanoSvg.cpp`, ADR 0012) | in Git (2026-09-30) |
| `stockfish/` | Stockfish 19 + NNUE | `projects/pocketbook/engines/stockfish/engine.lock` | GPL-3.0 | engine plug-in | **local only, not in Git** (D15) — `scripts/fetch-engines.ps1` |

Not needed any more (ADR 0005): `stb/` (own TrueType rasterizer and PNG writer in `src/ui/canvas`)
and `doctest/` (own doctest-compatible test framework in `tests/support`). PNG piece sets were dropped for
on-device SVG rasterization (ADR 0012), so no image decoder is needed.

Fonts are data, not libraries: `data/fonts/DejaVuSans*.ttf` (Bitstream Vera licence + public
domain changes, see `data/fonts/LICENSE-DejaVu.txt`).
