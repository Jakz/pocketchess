# Licensing assessment

Status: written 2026-10-01 for future reference. PocketChess is currently for **personal use only**;
nothing below has been acted on. Before any public release, work through "Before shipping".
This is a practical engineering assessment, not legal advice.

## Summary

| Area | Verdict for a public release |
| --- | --- |
| PocketChess code (GPL-3.0-or-later) | OK, but the licence text must ship and the source must be available |
| Stockfish 19 / Stockfish 11 (GPL-3.0) | OK, same obligations (licence, authors, corresponding source) |
| Third-party libraries, fonts, piece sets | OK, need a notices file |
| Lichess data (puzzles, openings, broadcasts) | OK (CC0) |
| `historical.db` built from PGN Mentor | **Blocking**: not redistributable |
| Packs built from Lumbra's Gigabase | Only as a separate CC BY-NC-SA download, only if PocketChess stays non-commercial |

## Components

### Code and engines

- **PocketChess**: GPL-3.0-or-later (`LICENSE`, SPDX headers). Binaries must ship with the licence
  text, and recipients must be able to get the corresponding source. The GitHub repository
  (`Jakz/pocketchess`) is **private**: either make it public and tag each release, or ship a source
  zip next to each release (or a written offer, valid 3 years).
- **Stockfish 19** (main engine) and **Stockfish 11** (Eco engine): GPL-3.0. Ship `Copying.txt` and
  `AUTHORS` from upstream, and point to the exact source: the pinned upstream tags
  (`projects/pocketbook/engines/*/engine.lock`) plus our build files (`projects/pocketbook/engines/`,
  `build-engine.ps1`). We do not patch Stockfish's sources. Upstream sources live in
  `libs/stockfish*` (local, not in Git).
- **Stockfish NNUE network** (`nn-1a298aa575a0.nnue`, shipped beside the binary): assumed to be
  distributed by the Stockfish project as part of the GPL program. **Not verified**: check the
  network's licence statement before a release.

### Libraries (linked into the app or the engines)

| Component | Licence | Where | Obligation |
| --- | --- | --- | --- |
| SQLite 3.53.4 | Public domain | app (both builds) | none |
| nanosvg (memononen, pinned commit) | zlib | app (both builds) | keep notice in source; credit in notices |
| SDL3 3.4.16 | zlib | Windows build (`SDL3.dll`) | credit in notices |
| musl libc | MIT | statically linked by Zig (reader app, engines) | include MIT notice |
| libc++ / libc++abi / libunwind | Apache-2.0 WITH LLVM-exception | statically linked by Zig | the LLVM exception waives notice in binaries; credit anyway |
| zstd 1.5.7 (decoder) | BSD-3-Clause | desktop tools only (PuzzleImporter, PgnImporter) | not shipped |
| InkView (PocketBook SDK) | proprietary | the reader's own system library, linked at runtime | not redistributed; GPL-3 "System Libraries" exception applies |

### Assets

| Asset | Licence | Status |
| --- | --- | --- |
| DejaVu Sans / Sans Bold | Bitstream Vera licence + public domain changes | `LICENSE-DejaVu.txt` ships |
| Cburnett pieces (Colin M.L. Burnett) | GPL-2.0-or-later | `LICENSE.txt` ships; compatible with GPL-3 |
| Merida pieces (Armando Hernandez Marroquin) | GPL-2.0-or-later | `LICENSE.txt` ships; compatible |
| Chessnut pieces (Alexis Luengas) | Apache-2.0 | `LICENSE.txt` ships; compatible with GPL-3 (not GPL-2-only) |
| Bold pieces (Cburnett with wider strokes) | GPL-2.0-or-later | derived; add a `LICENSE.txt` to the folder before release |
| Board themes (`data/themes`) | ours, GPL-3.0-or-later | — |

Lichess piece sets marked CC BY-NC-SA, "freeware" or "personal use" in Lichess's `COPYING.md`
(alpha, california, cardinal, staunty, …) are deliberately **not** bundled; users may import them.

### Content

| Content | Source | Licence | Redistributable |
| --- | --- | --- | --- |
| Puzzle packs (50k … all) | Lichess puzzle database | CC0 | yes (credit as courtesy) |
| Opening names (`data/openings`) | lichess-org/chess-openings | CC0 | yes |
| Lichess games (e.g. `lichess-2013-01`) | database.lichess.org | CC0 | yes (online games, not shipped now) |
| Broadcast games (2020+, OTB masters) | database.lichess.org/broadcast | CC0 | yes |
| Classics (`data/samples/pgn/classics.pgn`) | historical games + our comments | game scores are facts; comments ours | yes |
| `historical.db` | PGN Mentor (`scripts/fetch-historical.ps1`) | none stated ("completely free" downloads) | **no** |
| Lumbra's Gigabase packs | lumbrasgigabase.com | CC BY-NC-SA 4.0 | only separately, non-commercially, same licence, with attribution |

Notes on games:

- Individual game scores (moves) are widely treated as facts, but a **collection** can be protected
  (selection/arrangement copyright, and in the EU the sui generis database right for substantial
  extraction). PGN Mentor gives no licence to redistribute its compilation.
- CC BY-NC-SA for Lumbra: shipping a CC BY-NC-SA data file next to the GPL app is an aggregate
  (separate works), which both licences allow, provided the pack keeps CC BY-NC-SA, credits Lumbra's
  Gigabase, and nobody uses it commercially (PocketChess must be free).

### Names and trademarks

"Stockfish", "Lichess" and "PocketBook" are used descriptively only, which is fine. Avoid
suggesting endorsement (no logos; "for PocketBook" wording is fine, "PocketBook Chess" is not).

## Before shipping (checklist)

1. Release packages contain only redistributable content: Classics, CC0 Lichess packs (puzzles,
   broadcasts). Keep `historical.db` (PGN Mentor) out; add a "redistributable" flag to content
   staging so a release build cannot include it by mistake.
2. Ship in both bundles (reader zip and Windows zip):
   - `LICENSE` (GPL-3.0 text) for PocketChess;
   - Stockfish `Copying.txt` and `AUTHORS`, with the source tags used;
   - `THIRD-PARTY-NOTICES.txt` covering SQLite, nanosvg, SDL3 (Windows), musl, libc++, DejaVu,
     the piece sets, and the Lichess data (CC0);
   - a source pointer (public repository tag, or a source zip shipped alongside).
3. Make the source available: public repository with a tag per release, or a source zip per release.
4. Verify the Stockfish NNUE network's licence statement.
5. Add `data/pieces/bold/LICENSE.txt` (GPL-2.0-or-later, derived from Cburnett).
6. Historical games for users: either a separate CC BY-NC-SA Lumbra-derived download (only if the
   app stays free), user-built packs (`fetch-historical.ps1` + `build-content.ps1 -Historical`, or
   on the reader Import → "Make a game pack"), or permission from PGN Mentor.
