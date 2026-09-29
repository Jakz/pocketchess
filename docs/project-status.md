# Project status — resume point

Last updated: 2026-09-29 (M2: Stockfish 11 Eco engine, engine benchmark, device build with both engines). Plan: `docs/ai/`. Conventions: `AGENTS.md`.

## Milestone overview

| Milestone | State |
| --- | --- |
| M0.1 Repository skeleton | ✅ done; history on `github.com/Jakz/pocketchess` since 2026-09-29 (M0.1b done; CI still optional) |
| M0.2 Vendor libraries | 🟡 SDL3 copied from Enigmistica automatically by the first build of `SDL3.vcxproj` (not in Git); stb/doctest replaced (ADR 0005), yaml-cpp replaced (ADR 0007); sqlite deferred to M3 |
| M0.3 MSVC solution | ✅ Desktop x64 builds on Windows with `PocketChessTests`: 74 suites pass (2026-09-29); the app ran (Home, Analyze, Diagnostics incl. Bench with real engines) |
| M0.4 PocketBook build | 🟡 `build-zig.ps1 -Configuration Release` builds `pocketchess.app` + `pocketchess-selftest.app` with the real SDK and stages the bundle (2026-09-29); NEEDED/GLIBC check skipped (no `llvm-readelf`); device run pending |
| M0.5 Device spike (Diagnostics) | 🟡 screen implemented; needs the device run |
| M0.6 Stockfish + plug-in skeleton | 🟡 sf_19 pinned; Zig builds for windows-x64 (bench, `[integration]`) and arm (static musl hard-float, ADR 0009); device run pending |
| M1 Chess core + board UI | 🟢 core complete; board UI, sandbox, promotion, golden images done; device self-test pending |
| M2 Engine integration | 🟡 engine subsystem, Analyze engine panel, Stockfish 11 Eco candidate, engine benchmark (Diagnostics → Bench) done; `[integration]` tests pass with both real engines on Windows; device benchmark, profile ADR and the device checklist pending |
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

## 2026-09-29 — M2 engines on Windows and ARM (ADR 0009)

- Stockfish 19 builds with Zig for windows-x64 (`GIT_DATE` defined: Zig's clang rejects
  `__DATE__`) and for arm. Zig's `arm-linux-gnueabi` is soft-float, so NEON code crashed LLVM;
  engines now use their own toolchain `engines/zig-arm-engine.cmake` (static
  `arm-linux-musleabihf`, `-mcpu=cortex_a7`), ADR 0009. Engines and the Release app are stripped
  (app 16 MB → 0.9 MB).
- Stockfish 11 Eco candidate: `engine.lock` (sf_11 pinned), CMake (`armv7` / `x86-64-modern`),
  descriptor `data/engines/stockfish11/engine.yaml` (energy class low), listed in
  `projects/engines.txt`. Builds for both targets (0.7 MB).
- `EngineBenchmark` (core): 6 fixed positions × every strength level of every engine + Quick
  analysis, with the profile's Threads/Hash/priority; report in `logs/engine-bench.yaml`.
  Diagnostics' redundant "‹ Back" button became "Bench" (tap again to cancel).
- `[integration]` tests advance the fake clock with real time (startup times were 0 ms).
- Desktop benchmark (Balanced, i.e. 500 ms play budget, 1.5 s Quick analysis):

  | Engine | readyok | Peak RSS | knps | Depth (max / analysis) | CPU-s per move |
  | --- | --- | --- | --- | --- | --- |
  | Stockfish 19 | 793 ms | 321 MiB | ~1,670 | 20.7 / 23.8 | = movetime |
  | Stockfish 11 | 95 ms | 42 MiB | ~3,500 | 20.7 / 24.3 | = movetime |

  With time-bounded searches both engines burn the same CPU per move; the Eco engine saves RAM
  and startup time, and saves CPU only under node limits (Eco profile). Stockfish 19's peak RSS
  already exceeds the ADR 0004 limit (250 MB) on the desktop.

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
- **Engines** (real Stockfish 19/11 on Windows via `[integration]` and Bench; the rest with the
  fake engine): descriptor parsing (the shipped descriptors), registry,
  UCI start/options/search/stop/supersede/crash/timeouts, manager roles/profiles/idle/lifecycle,
  analysis panel with throttling; the `--fake-engine` child process end to end.
- **Tests**: 69 suites / ~1,960 assertions pass with GCC and with Clang `-funsigned-char`,
  GCC ASan + UBSan clean; golden-image hashes identical between GCC and Clang.
- **PocketBook CMake**: configured and linked with a stub toolchain; `pocketchess-selftest.app`
  ran all suites against the bundle layout (golden checksums from `selftest/golden.txt`).

## Unverified (written without access to the tool or API)

| Item | Why unverified | First check |
| --- | --- | --- |
| Analyze → Engine panel with real engines | the benchmark and `[integration]` tests run real engines through `UciEngine`/`Win32Process`; the panel itself only with the fake engine | F5 → Analyze → Engine (Quick/Deep) |
| InkView backend | builds with the real SDK; never run | run on the reader |
| Device engines (static musl hard-float) | build and link; never run on the reader | Diagnostics → Engine, then Bench; `stockfish bench 16 1 8` = 170203 nodes |
| App NEEDED libraries / GLIBC versions | `check-elf.ps1` needs `llvm-readelf` (VS "C++ Clang tools") | install it, re-run `build-zig.ps1` |
| PowerShell scripts | `check-sources`, `fetch-engines`, `build-engine`, `build-zig`, `stage-bundle` ran on Windows; `deploy.ps1`, `verify-all.ps1` not yet | run them |

## Not working yet

- M2 remainder: device benchmark, the ADR with per-profile defaults and whether Stockfish 11 is
  bundled (`projects/engines.txt`), `resources:` in the descriptors from device numbers, the M2
  device checklist. The "Engine missing" dialog has no Settings button yet (no Settings screen).
- No Settings screen yet: power profile / engine per role / MultiPV live in `app::Settings` defaults.
- No SQLite, settings persistence, session resume (M3), puzzles (M4), games (M5), themes from
  YAML / PNG piece sets / Import (M6).
- InkView Fast mode uses PartialUpdate (ADR 0002); `post()` polls while background work runs
  (ADR 0003).
- `--replay` (recorded input logs) not implemented; `--script` covers scripted sessions.

## Resume point (next steps, in order)

1. Windows: F5 → Analyze → Engine (Quick/Deep) with real Stockfish (quick visual check).
2. Device: `projects/pocketbook/build-zig.ps1 -Configuration Release` (already builds and stages
   both engines), `deploy.ps1`; run `pocketchess-selftest.app`, then Diagnostics (spike items,
   Engine, Bench → `logs/engine-bench.yaml`) and Analyze; fill the table below.
3. ADR 0004 checkpoint (readyok < 3 s, RSS < 250 MB): Stockfish 19 peaks at 321 MiB on the desktop,
   so expect it to fail → Stockfish 11 takes play/hint/validate (and maybe Quick analysis).
4. ADR: per-profile defaults and bundling of Stockfish 11 from the device benchmark; fill the
   descriptors' `resources:`. M2 device checklist, then close M0 + M2 with `scripts/verify-all.ps1`.

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
| — | 6.10 | Stockfish 19: readyok ms, RSS, knps, CPU-s | |
| — | 6.10 | Stockfish 11: readyok ms, RSS, knps, CPU-s | |
| — | 6.10 | Engine benchmark (`logs/engine-bench.yaml`) | |
