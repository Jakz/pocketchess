# Project status — resume point

Last updated: 2026-09-29 (M2 engine subsystem, first part). Plan: `docs/ai/`. Conventions: `AGENTS.md`.

## Milestone overview

| Milestone | State |
| --- | --- |
| M0.1 Repository skeleton | ✅ done; history on `github.com/Jakz/pocketchess` since 2026-09-29 (M0.1b done; CI still optional) |
| M0.2 Vendor libraries | 🟡 SDL3 copied from Enigmistica automatically by the first build of `SDL3.vcxproj` (not in Git); stb/doctest replaced (ADR 0005), yaml-cpp replaced (ADR 0007); sqlite deferred to M3 |
| M0.3 MSVC solution | 🟡 Debug build of `PocketChess.exe` works on Windows (2026-09-29: Home, Analyze, Diagnostics, touch test ran); `PocketChessTests` not built/run with MSVC yet |
| M0.4 PocketBook build | 🟡 written; validated with a stub toolchain on Linux; real Zig/InkView build pending |
| M0.5 Device spike (Diagnostics) | 🟡 screen implemented; needs the device run |
| M0.6 Stockfish + plug-in skeleton | 🟡 scaffolding (pin, CMake, fetch/build scripts, descriptor, probe); sources not fetched yet |
| M1 Chess core + board UI | 🟢 core complete; board UI, sandbox, promotion, golden images done; device self-test pending |
| M2 Engine integration | 🟡 descriptors + registry, `UciEngine`, `EngineManager`, jobs, power profiles, accounting, Analyze engine panel done (tests with a fake engine); benchmark, Eco engine, real-Stockfish integration test and the device checklist pending |
| M3–M6 | not started (Home shows them disabled with their milestone) |

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

## 2026-09-29 — M2 engine subsystem (first part, ADR 0007, ADR 0008)

- `core/util/Yaml`: YAML subset reader for descriptors (replaces yaml-cpp, ADR 0007).
- `core/engine`: `EngineDescriptor` + `EngineRegistry` (engines/*/engine.yaml, disabled with a
  reason when broken or missing), `EngineTypes` (limits, option validation), `IEngine` +
  `UciEngine` (state machine with timeouts and retry-once, option deltas and resets, PV → moves),
  `PowerProfile` (Performance/Balanced/Eco table), `EngineManager` (roles, one process, lazy start,
  idle timeout, lifecycle stop, BestMove/Analysis/Hint/Validate jobs, per-job CPU-s/wall/nodes/RSS
  log and totals). `util::Scheduler` carries post/timers into core; the app adapts `Host`
  (`Host::now()` added, fake time in `HeadlessHost`).
- Analyze screen: Engine button (Off → Quick → Deep) replaces the FEN/New buttons (both stay in
  Menu); panel with evaluation (White's view), best move, depth and PV in SAN, throttled to one
  refresh per second; stops when covered/left/asleep; "Engine missing" dialog.
- Diagnostics: installed engines with their state, power profile, engine job totals; the probe
  uses the registry's analysis engine and quits the manager's engine first.
- Tests: `FakeUci` (scripted engine, in-process `FakeProcess` and the `--fake-engine` child, now
  playing legal moves), `ManualScheduler`; new suites for YAML, descriptors/registry, UciEngine,
  EngineManager and the analysis panel; goldens updated for the new button row, plus
  `analyze-engine-quick` and `analyze-engine-missing`.

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
- **Engines (fake engine only)**: descriptor parsing (the shipped Stockfish descriptor), registry,
  UCI start/options/search/stop/supersede/crash/timeouts, manager roles/profiles/idle/lifecycle,
  analysis panel with throttling; the `--fake-engine` child process end to end.
- **Tests**: 69 suites / ~1,960 assertions pass with GCC and with Clang `-funsigned-char`,
  GCC ASan + UBSan clean; golden-image hashes identical between GCC and Clang.
- **PocketBook CMake**: configured and linked with a stub toolchain; `pocketchess-selftest.app`
  ran all suites against the bundle layout (golden checksums from `selftest/golden.txt`).

## Unverified (written without access to the tool or API)

| Item | Why unverified | First check |
| --- | --- | --- |
| MSVC projects, `/W4` cleanliness | the app builds; tests and the M2 code not yet compiled with MSVC | `projects/msvc/build.ps1 -Configuration Desktop -Target Test` |
| Engine subsystem against real Stockfish | Stockfish sources not fetched (GitHub not reachable from the session) | `scripts/fetch-engines.ps1 -Pin`, build, Analyze → Engine |
| SDL3 backend (`src/platform/sdl`) | SDL3 not available; syntax-checked against a stub of the 3.2 API | F5 the desktop app |
| `Win32Process.cpp` | no Windows headers | `PocketChessTests` process/probe tests |
| InkView backend | syntax-checked against a stub `inkview.h` | build with the real SDK, run on the reader |
| `build-zig.ps1` vs `ZigToolchain.cmake` | Enigmistica's script/toolchain could not be read | compare variable names with Enigmistica's `build-zig.ps1` |
| PowerShell scripts | no PowerShell in the session | `check-sources.ps1` fixed on Windows; the others still untested |
| Stockfish CMake flags / INCBIN | SF 19 sources not available | BUILDING.md checklist |

## Not working yet

- M2 remainder: engine benchmark (Diagnostics → Engines, `logs/engine-bench.yaml`), Stockfish 11
  Eco candidate, `[integration]` test with the real binary, profile numbers from measurements.
- No Settings screen yet: power profile / engine per role / MultiPV live in `app::Settings` defaults.
- No SQLite, settings persistence, session resume (M3), puzzles (M4), games (M5), themes from
  YAML / PNG piece sets / Import (M6).
- InkView Fast mode uses PartialUpdate (ADR 0002); `post()` polls while background work runs
  (ADR 0003).
- `--replay` (recorded input logs) not implemented; `--script` covers scripted sessions.

## Resume point (next steps, in order)

1. Windows: `projects/msvc/build.ps1 -Configuration Desktop -Target Test` (build + `PocketChessTests`); fix any
   MSVC errors/warnings. Goldens must match (if not, compare `tests/golden/actual/*.png`).
2. `scripts/fetch-engines.ps1 -Pin`, build Stockfish for `windows-x64` (and `arm`), then F5 →
   Analyze → Engine (Quick/Deep) and Diagnostics → Engine; record readyok/RSS/knps.
3. `projects/pocketbook/build-zig.ps1 -Configuration Release`; deploy; run
   `pocketchess-selftest.app`, then Diagnostics and Analyze with the engine; fill the table below.
4. ADR 0004 checkpoint (readyok < 3 s, RSS < 250 MB) → decide whether the Eco engine moves up.
5. Rest of M2: engine benchmark, Stockfish 11 descriptor/CMake, `[integration]` test, M2 device
   checklist. Then close M0 + M2 with `scripts/verify-all.ps1`.

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
