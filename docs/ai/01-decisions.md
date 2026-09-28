# 01 — Decisions and deviations from the design notes

Each decision has an ID so later documents, ADRs and commits can refer to it. "Agreed" means
Jack confirmed it on 2026-09-28 (three rounds of answers). The "P" items are technical defaults
proposed by the plan. Any of them can be changed later through an ADR.

## Agreed

| ID | Decision |
| --- | --- |
| D1 | **Two builds are developed in parallel from day one**: Windows desktop (MSVC + SDL3) and PocketBook (Zig cross-compile). A feature is done only when both builds compile and the device checklist for that milestone passes. |
| D2 | **Windows build = native MSBuild projects** in `projects/msvc/` (regular static-library/application/test `.vcxproj` files that are debuggable with F5 and editable in Project Properties). **PocketBook build = CMake + Ninja + Zig** in `projects/pocketbook/`, invoked by `build-zig.ps1`. A Makefile-type `PocketBook.vcxproj` in the same solution calls that script, so one Visual Studio build produces both. `scripts/check-sources.ps1` fails when the two source lists drift. |
| D3 | **Shared Zig toolchain stays outside the repo** at `C:\Users\jack\Documents\Dev\libs\pocketbook` (Zig 0.16.0, InkView 6.10, `ZigToolchain.cmake`, `compat/`). It is found in this order: `-ToolchainRoot`, `%POCKETBOOK_TOOLCHAIN_ROOT%`, then `..\..\libs\pocketbook` relative to the repo, which is the same relative path Enigmistica uses. |
| D4 | **Engine = latest Stockfish release (Stockfish 19, released 2026-09-05), NNUE network embedded, run as a child process over UCI.** Its source and network are kept locally in `libs/stockfish/`, which is not in Git (D15). It is cross-compiled with Zig for `arm-linux-gnueabi` (Cortex-A7, NEON, softfp) and built for Windows x64. M0/M2 measure startup time, RAM use and nodes/s on the device. Stockfish is the default engine, but it is **one plug-in among others** (D8). |
| D5 | **Desktop preprocessing tools are C++** in the same solution (`src/tools/…`, `projects/msvc/Tools*.vcxproj`). They reuse `pchess-core`, so imported data is validated with the same FEN/SAN/move-encoding code the reader uses. |
| D6 | **Plan depth**: M0–M6 (MVP + themes) are planned in detail. M7 study features (openings, endgames, personal mistakes, lessons) are a structured backlog. |
| D7 | **Folder layout**: `src/` code, `projects/msvc/` and `projects/pocketbook/` build projects, `libs/` local copies of third-party libraries, `data/` source data/assets, `docs/`, `tests/`, `scripts/`, and `out/` (ignored build output). |
| D8 | **Engines are plug-ins with power profiles.** Screens use jobs through an `IEngine` interface. Engines are described by `engines/<id>/engine.yaml` (binary, roles, strength mapping, measured resources). The user picks a **power profile** (Performance / Balanced / Eco) that sets engine choice per role, threads/hash, search budgets and idle timeout. An **energy-saver engine** (first candidate: Stockfish 11 classical) is bundled only if the M2 benchmark shows it saves CPU-seconds/RAM at the needed strengths. Details: architecture doc §8. |
| D9 | **Resource flexibility first, optimize later.** The device has 1 GB RAM and 16 GB storage (both confirmed by the Verse Pro Color spec). Engines, puzzle packs, game packs, piece sets and themes are all swappable units with manifests and size/RAM budgets (architecture doc §13). Every job logs engine CPU-seconds, so defaults can be tuned from device data. |
| D10 | **Own chess core** (bitboards, legal move generation, FEN, SAN, PGN, UCI move text, 16-bit move codec), validated by perft. |
| D11 | **Device** = PocketBook Verse Pro Color PB634K3, firmware 6.10 (the same unit as Enigmistica). **Data folder** = `/mnt/ext1/applications/pocketchess/`, beside `pocketchess.app` (Enigmistica's pattern). The Windows build stages the same tree in `pocketchess/` beside the executable. |
| D12 | **Name = PocketChess**: `pocketchess.app` (PB), `PocketChess.exe` (Windows), C++ namespace `pchess`, target/library prefix `pchess-`. |
| D13 | **VS configurations**: `Debug|x64` and `Release|x64` always build Windows **and** the PocketBook app **and** the engines. `Desktop|x64` is the fast edit–F5 configuration that skips the Makefile projects. |
| D14 | **Default puzzle pack = Large, 50k** (≈ 5–8 MB). Starter/Standard/Huge remain available as extra packs. |
| D15 | **Licence GPL-3.0-or-later**, hosted in a **private GitHub repository**. Vendored libraries keep their licences in `libs/<name>/`. A Windows CI build can follow (M0.1b). **Engine sources and networks (Stockfish etc.) are not committed for now**: only their build files, pins and descriptors are, and `scripts/fetch-engines.ps1` restores the local copies. |

## Technical defaults (proposed by the plan, accepted with it; revisit through an ADR)

| ID | Decision | Rationale |
| --- | --- | --- |
| P2 | **Software canvas rendering.** All views draw into a platform-independent RGB canvas (`ui/canvas`). Backends only blit dirty rectangles and issue refreshes. Text uses stb_truetype with a bundled font on both platforms. | Pixel-identical output on Windows and the device, exact golden-image tests, and one alpha-blending path for piece bitmaps. Deviates from Enigmistica, which draws through InkView primitives and InkView fonts. |
| P4 | **SQLite (amalgamation)** vendored in `libs/sqlite/`; four databases as in the design notes (`chess.db`, `progress.db`, `puzzles.db`, `games.db`). Content databases open read-only. Schema versions use `PRAGMA user_version` migrations. | As in the design notes. |
| P5 | **Puzzle themes as a 64-bit bitmask column**, plus a `themes` dictionary table, instead of the `puzzle_themes` join table in the notes. A random-order column `rnd` is indexed together with `rating`. | Bounded, index-only queries on the device (design notes §31.7). |
| P8 | **yaml-cpp** for theme descriptors and custom content (design notes §19, §35). Settings live in `chess.db`, not YAML. | Same pinned snapshot as Enigmistica. |
| P9 | Unit tests use **doctest** (single header, `libs/doctest/`), in one `PocketChessTests` executable, plus a device self-test app (`pocketchess-selftest.app`) built from the same tests. | The ARM/32-bit/unsigned-`char` differences get tested on the real CPU. |
| P10 | Engine strength: Stockfish's `UCI_Elo` has a floor (≈1320 in recent versions; confirm in the vendored source), so **Beginner/Casual use `Skill Level` + short move time**, and Club/Strong use `UCI_LimitStrength` + `UCI_Elo`. The UI labels stay as in the notes (§7.2). | Design notes §7.2 ask for ~800/~1200, which `UCI_Elo` cannot reach. |
| P11 | Engine lifecycle: started lazily on the first job and stopped after the **power profile's idle timeout** (Balanced: 60 s). Always stopped immediately on background/sleep/exit. Runs with `nice` priority on the device. | Battery rules (§31) balanced against the engine load time. |
| P12 | Lichess puzzle semantics are kept: the stored FEN is **before** the opponent's move, and the solution's first move is the opponent's. The trainer auto-plays it with a last-move highlight. | Matches the source data (CC0, `lichess_db_puzzle.csv.zst`) and lets the UI show the opponent's move. |

## Unchanged from the design notes

Everything else in the design notes stands: the four modes, MVP (§38), the e-ink refresh rules (§22),
engine activation policy (§8.2), Elo-like rating with K = 24 and binary scoring (§10), the review schedule
(§11), the game schema (§26), battery rules (§31), graceful errors (§32), and UX principles (§39).

## Changed during implementation (2026-09-28)

| Ref | Change | Record |
| --- | --- | --- |
| P2 | Text uses an in-repo TrueType rasterizer (`ui/canvas/TrueTypeFont`, `Rasterizer`) instead of stb_truetype; PNG output uses an in-repo encoder. Same goal: identical pixels on both platforms. | ADR 0005 |
| P9 | Tests use an in-repo doctest-compatible framework (`tests/support/Test.h`) instead of doctest. | ADR 0005 |
| Fonts | DejaVu Sans (+ Bold) instead of Noto Sans; its Unicode chess glyphs are the interim piece set until M6's PNG sets. | ADR 0005 |
| Arch §2 | `Process` is declared in `src/core/engine/Process.h`; implementations stay in `src/platform/{win32,posix}`. | ADR 0006 |
| Build §3.1 | `Sqlite`/`YamlCpp` projects are added when first used (M2/M3), not in M0. `PocketChessHeadless.vcxproj` added (screenshots without a window). | ADR 0001 |
| Build §4.1 | Platform sources form an OBJECT library in CMake (link order of `Process::spawn`). | ADR 0001 |
