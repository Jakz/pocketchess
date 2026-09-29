# Project status — resume point

Last updated: 2026-09-29 (first MSVC build, Git repository). Plan: `docs/ai/`. Conventions: `AGENTS.md`.

## Milestone overview

| Milestone | State |
| --- | --- |
| M0.1 Repository skeleton | ✅ done; local Git history created 2026-09-29, remote `github.com/Jakz/pocketchess` (M0.1b: first push pending) |
| M0.2 Vendor libraries | 🟡 SDL3 copied from Enigmistica automatically by the first build of `SDL3.vcxproj` (not in Git); stb/doctest replaced (ADR 0005); sqlite/yaml deferred to first use |
| M0.3 MSVC solution | 🟡 first Windows build 2026-09-29 hit two harness bugs, both fixed (see below); full build + tests pending |
| M0.4 PocketBook build | 🟡 written; validated with a stub toolchain on Linux; real Zig/InkView build pending |
| M0.5 Device spike (Diagnostics) | 🟡 screen implemented; needs the device run |
| M0.6 Stockfish + plug-in skeleton | 🟡 scaffolding (pin, CMake, fetch/build scripts, descriptor, probe); sources not fetched yet |
| M1 Chess core + board UI | 🟢 core complete; board UI, sandbox, promotion, golden images done; device self-test pending |
| M2–M6 | not started (Home shows them disabled with their milestone) |

## 2026-09-29 — first MSVC build fixes

- `scripts/check-sources.ps1`: Windows PowerShell 5.1 ignores `-Include` together with `-LiteralPath`,
  so folders reached `Get-Content` ("Access to the path ...\src\core\chess is denied"). It now uses
  `-File` plus an extension filter. The source lists and the layering rule were re-checked: all match.
- SDL3: `libs\sdl3` had never been copied, and the old `vendor-libs.ps1` could not adapt Enigmistica's
  project (it imports `Importer.props` and uses `$(RepoRoot)` paths). `projects/msvc/SDL3.vcxproj`
  (+ filters) is now committed, adapted from Enigmistica's (SDL 3.4.16 as a DLL, output
  `out\msvc\sdl3\<cfg>\`). Its `VendorSdl3` target runs the simplified `vendor-libs.ps1` (copies
  `include\`, `src\` and licences) when `libs\sdl3` is missing. `PchessPlatformDesktop` references
  SDL3 for build order, `PocketChess` copies `SDL3.dll` beside the exe, and the Desktop configuration
  uses SDL3's Debug build. `libs/sdl3/` is ignored by Git, like the engine sources.

## Working now (verified in the implementation session, Linux x64, GCC 13 and Clang 18)

- **Chess core**: bitboards, legal move generation with pin/check masks, FEN (strict, normalized
  ep/castling), SAN (generation with minimal disambiguation, tolerant parsing), UCI moves,
  16-bit move codec, Zobrist keys, game tree with variations, PGN reader/writer (headers,
  comments, NAGs, nested variations, error recovery per game), draw rules (threefold/fivefold,
  50/75 moves, insufficient material, stalemate/mate).
- **Perft**: all 6 standard positions to depth 5, start position and position 3 to depth 6
  (119,060,324 / 11,030,083 nodes), plus 14 en-passant/castling/promotion edge cases. Bulk-counted
  perft runs at ~100 Mnps on the desktop (`PocketChessTests --bench-movegen`).
- **UI**: software canvas, own TrueType rasterizer and PNG encoder, DejaVu glyph piece set, board
  view with coordinates, selection, legal targets, last move and check markers in color and mono
  styles, 4 built-in themes, minimal per-square invalidation, dirty-rect coalescing, refresh
  policy (Fast / Partial / Full every 12).
- **App**: Home, Analyze sandbox (tap-to-move, drag-to-move, promotion dialog, back/forward through
  the tree, variations, flip, FEN entry, new game, status line with mate/stalemate/draw claims,
  move list with the current move highlighted), Diagnostics (system/canvas info, 1-square and
  12-square partial-update timing, full update, touch mapping test, key capture, lifecycle log,
  engine probe, timing log toggle), message/choice/text-entry dialogs, scripted input, headless
  screenshots.
- **Processes**: POSIX process implementation tested for real against the fake UCI engine;
  `UciProbe` full cycle (uci → setoption → isready → go → bestmove → quit).
- **Tests**: 45 suites / ~1,500 assertions pass with GCC and with Clang `-funsigned-char`,
  ASan + UBSan clean; golden-image hashes identical between GCC and Clang.
- **PocketBook CMake**: configured and linked with a stub toolchain; `pocketchess-selftest.app`
  ran all suites against the bundle layout (golden checksums from `selftest/golden.txt`).

## Unverified (written without access to the tool or API)

| Item | Why unverified | First check |
| --- | --- | --- |
| MSVC projects, `/W4` cleanliness | no MSVC in the session | open the sln, build Desktop, run tests |
| SDL3 backend (`src/platform/sdl`) | SDL3 not available; syntax-checked against a stub of the 3.2 API | F5 the desktop app |
| `Win32Process.cpp` | no Windows headers | `PocketChessTests` process/probe tests |
| InkView backend | syntax-checked against a stub `inkview.h` | build with the real SDK, run on the reader |
| `build-zig.ps1` vs `ZigToolchain.cmake` | Enigmistica's script/toolchain could not be read | compare variable names with Enigmistica's `build-zig.ps1` |
| PowerShell scripts | no PowerShell in the session | `check-sources.ps1` fixed on Windows; the others still untested |
| Stockfish CMake flags / INCBIN | SF 19 sources not available | BUILDING.md checklist |

## Not working yet

- No engine integration beyond the probe (M2: `IEngine`, `EngineManager`, jobs, power profiles).
- No SQLite, settings persistence, session resume (M3), puzzles (M4), games (M5), themes from
  YAML / PNG piece sets / Import (M6).
- InkView Fast mode uses PartialUpdate (ADR 0002); `post()` polls while background work runs
  (ADR 0003).
- `--replay` (recorded input logs) not implemented; `--script` covers scripted sessions.

## Resume point (next steps, in order)

1. Open `projects/msvc/PocketChess.sln` → Desktop|x64 → build (the first build copies SDL3 from
   `..\enigmistica`). Fix any MSVC warnings/errors; `PocketChessTests` must pass (goldens must match: if they do not,
   compare `tests/golden/actual/*.png`; a mismatch means FP differences — see ADR 0002).
2. F5 `PocketChess` (simulator keys in README) and try Analyze, Diagnostics.
3. `projects/pocketbook/build-zig.ps1 -Configuration Release` — align with Enigmistica's script
   if the toolchain file expects other variables. Deploy, run `pocketchess-selftest.app`, then
   `pocketchess.app` → Diagnostics; record the measurements below.
4. `git push -u origin main` to `github.com/Jakz/pocketchess` (M0.1b; local history already exists).
5. `scripts/fetch-engines.ps1 -Pin`, build Stockfish for both targets, Diagnostics → Engine on the
   reader → ADR 0004 checkpoint.
6. Close M0 with `scripts/verify-all.ps1` and the M0 device checklist; start M2.

## Verified on hardware

| Date | Firmware | Item | Result |
| --- | --- | --- | --- |
| — | 6.10 | App launch time to first screen | |
| — | 6.10 | `GetCanvas()` depth / scanline / format | |
| — | 6.10 | PartialUpdate latency: 1 square / 12 squares / FullUpdate | |
| — | 6.10 | Touch mapping (corners + center errors) | |
| — | 6.10 | Key codes (Prev, Next, Menu, Home, Back delivered?) | |
| — | 6.10 | Lifecycle events on sleep/wake, library, exit | |
| — | 6.10 | post(): Poll vs SendEvent | |
| — | 6.10 | glibc version, free RAM, exec allowed in applications/pocketchess | |
| — | 6.10 | Self-test result | |
| — | 6.10 | Stockfish: readyok ms, RSS, knps, CPU-s | |
