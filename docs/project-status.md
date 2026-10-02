# Project status — resume point

Last updated: 2026-10-03 (M7 started: Guess the move and My mistakes on desktop and in the reader build; device runs batched). Plan: `docs/ai/`. Conventions: `AGENTS.md`.

## Milestone overview

| Milestone | State |
| --- | --- |
| M0.1 Repository skeleton | ✅ done; history on `github.com/Jakz/pocketchess` since 2026-09-29 (M0.1b done; CI still optional) |
| M0.2 Vendor libraries | 🟡 SDL3 copied from Enigmistica automatically by the first build of `SDL3.vcxproj` (not in Git); stb/doctest replaced (ADR 0005), yaml-cpp replaced (ADR 0007); sqlite deferred to M3 |
| M0.3 MSVC solution | ✅ Desktop x64 builds on Windows with `PocketChessTests`: 74 suites pass (2026-09-29); the app ran (Home, Analyze, Diagnostics incl. Bench with real engines) |
| M0.4 PocketBook build | 🟡 `build-zig.ps1 -Configuration Release` builds `pocketchess.app` + `pocketchess-selftest.app` with the real SDK and stages the bundle (2026-09-29); NEEDED/GLIBC check skipped (no `llvm-readelf`); device run pending |
| M0.5 Device spike (Diagnostics) | 🟡 screen implemented; needs the device run |
| M0.6 Stockfish + plug-in skeleton | ✅ sf_19 pinned, net beside the binary; Zig builds for windows-x64 and arm (static musl hard-float, ADR 0009); runs on the reader (self-test `[integration]`, probe, Bench), ADR 0004 checkpoint passed |
| M1 Chess core + board UI | 🟢 core complete; board UI, sandbox, promotion, golden images done; device self-test pending |
| M2 Engine integration | 🟢 functionally done: engine subsystem, Analyze engine panel, benchmark, device lineup (ADR 0010: Stockfish 19 main, Stockfish 11 for Eco), all run on the reader. Device fine-tuning checks deferred until the app is feature-complete (decision 2026-09-29, see below) |
| M3 Play, Analysis, Home, session | 🟢 features done: SQLite + `core/storage` (`chess.db` v3), Settings v1, Play mode, Analyze (MultiPV lines, comments, main line / promote / delete, Load PGN), SessionState + Resume (ADR 0011), Home summary; pending: device acceptance run, strength calibration (deferred, see below) |
| M4 Tactics trainer, rating, review | 🟢 features done: zstd 1.5.7 decoder, `fetch-lichess.ps1`, `PuzzleImporter` + `build-content.ps1` (50k pack = 6 MB), `core/puzzles` (pack, selector, session, progress/rating/review), Tactics and Review screens, Home rating/due count, Tactics settings; pending: device acceptance (30 puzzles, selection < 50 ms, WAL on the user partition) |
| M5 Historical games | 🟢 features done: `data/openings` (Lichess ECO names, CC0), `core/games` (opening classification, `GameImporter`, `GamePack` filters/pages), `PgnImporter` + `build-content.ps1 -Games` (20k games = 6.5 MB in 27 s), bundled `classics.db`, favorites in `chess.db`, Games browser + study screens, "Analyze from here"; pending: device acceptance (≥ 10k pack, filters < 150 ms, partial page turns) |
| M6 Themes, piece sets, import, polish | 🟢 features done: YAML board themes (`ThemeManager`, Walnut + Newsprint), SVG piece sets rasterized on the reader (nanosvg, ADR 0012: Cburnett default, Merida, Chessnut, Bold), Import screen (ADR 0013: installs under `user/`, PGN → game pack), Settings v2 (piece set, page-button swap, debug logging), key-only navigation, portable release zips; pending: device acceptance in color and mono, refresh/resource tuning (deferred), touch-target check on the reader |
| M7 Backlog | 🟡 started: Guess the move, My mistakes (ADR 0015); next candidates: opening drill from the packs' book, endgame positions, stats screen, user/ backup |

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

## 2026-10-03 — M7: Guess the move, My mistakes (ADR 0015)

- Decided with the user: no full device pass per milestone (batched, the user tests occasionally);
  every mode records its statistics in the existing user databases; menus stay as tidy as now.
- Guess the move: Game → Menu → "Guess the moves…" (from the current main-line position) or
  Games → Menu → "Guess the moves of a random game…" (among the filtered games). Play White, Black
  or both; two tries per move, ◀ hint (the piece; counts as a second try), ▶ show move; replies after
  0.5 s; comments of annotated games appear as the moves are played; final score with the average
  over all runs. `chess.db` v5: `guess_runs` (updated after every move, resumable via the session)
  and `guess_moves` (outcome, first wrong try, hint). Golden: `guess`.
- My mistakes: Home → Puzzles → My mistakes. New finished Play games are checked first (Hint job
  per needed position, progress dialog with Stop); a move that drops the winning chance by ≥ 20
  points and differs from the engine's becomes a puzzle (the opponent's move, then the better move).
  Unsolved first, then due reviews; after solving: "In your game you played b3: winning chance
  59% → 4%". `chess.db` v6: `mistakes`, `mistake_scans`; attempts in `progress.db` (pack
  "My mistakes", ids `m<id>`, unrated). The fake test engine takes an evaluation hook.
- Device (to check when convenient): scan time per game under Eco and Balanced (log lines
  "mistakes: game N: …"), the "guess: random game … ms" time on the 80 MB historical pack.

## 2026-10-01 — large collections: packs v2, historical games, all puzzles, openings

- Device: the frame was shifted down and wrapped (photo): the 6.x system panel offsets the
  framebuffer. `SetPanelType(PANEL_DISABLED)` at start in the app and the self-test; fixed on the
  reader. Controls moved to the bottom for one-hand use (Games chips, Home launcher, Diagnostics).
- Game packs v2: moves one byte each (`encodeMoveIndexes`: index among the legal moves sorted by
  raw value), events/sites/openings in lookup tables, numeric dates/results, URL sites dropped,
  duplicates skipped (hash of players, date, moves), an index per filter in list order, counts
  capped at 10,000 ("N+"). Machine annotations ([%clk], [%eval], Lichess computer analysis) are
  dropped at import. Lichess sample 20k: 6.8 → 3.5 MB; a broadcast month (47k games): 222 → 12 MB.
- Opening statistics: each game pack has a `book` table (position key, move → games, white, draws,
  black) for the first 20 plies (moves played in < 3 games dropped in packs of ≥ 10k games);
  Analyze shows "Book (N): e4 41% d4 33% …" above the move list and names the opening on the
  status line.
- Puzzle packs v2: packed FEN (`packFen`: occupancy + nibbles, ~23 bytes), moves one byte each,
  opening tags once, no RD/plays. 50k: 6.3 → 3.6 MB. `build-content.ps1 -Presets all`: all
  6.1 M Lichess puzzles in 427 MB (73 bytes each, ~6 min on the PC).
- Historical games: `scripts/fetch-historical.ps1` (PGN Mentor players + events, optional
  openings; Lumbra's Gigabase .7z placed by hand, CC BY-NC-SA, extracted with 7-Zip), then
  `build-content.ps1 -Historical [-FromYear -ToYear -MinElo]` → `historical.db` (PgnImporter
  folders, `--skip-online`). Not redistributable: stays in data/downloads and data/generated.
- Openings screen (Home → Openings): 150 families, 3,820 named lines (Lichess, CC0), search by name
  or ECO, a line opens in Analyze.

## 2026-09-30 — after the first M6 device review: rendering, color, navigation

- Rendering: buttons looked cropped at the top. Causes fixed: the analysis panel's engine lines ran
  into the button row (the headline now sits on the status row, extra MultiPV lines that do not fit
  are dropped: 2 of 3 on the reader); text leaked anti-aliased pixels past its area that later
  partial updates never cleared (each invalidation now repaints a small margin; board squares stay
  exact); e-ink regions are widened to an 8-px grid (Kaleido 3 color phase; to confirm on the
  reader). `tests/app/RedrawTests.cpp`: partial redraws must equal a full redraw.
- Color: Settings → Display → Color theme / Accent color / Piece colors open a grid of colored
  rectangles (`SwatchDialog`). 11 saturated hues; the accent drives primary buttons, the current
  move and the selected/last-move squares; piece colors tint white pieces pale and black pieces deep
  (`ui::Palette`, `PieceCache::setTint`). New palettes Harbor, Forest, Coral, Lavender, Sand. Mono
  mode ignores both.
- Navigation (ADR 0014): framed "‹ *parent*" button in a 7.5 mm top bar; bottom bar ◀ ▶ (= Prev/Next,
  captioned) at the left and Menu at the right on every screen; menus end with "‹ Back to …" and
  "Close"; Settings and Import turn pages with Prev/Next and choose through the Menu (no focus
  cursor). The board is now 131 px per square on the reader (was 134).

## 2026-09-30 — M6: themes, piece sets, import, polish

- Themes: `ui::ThemeManager` + `parseTheme` (design notes §19: `name`, `mode`, colors as quoted
  `"#rrggbb"` or gray 0..255, `*_gray` aliases, marker patterns, `tint_markers`; unknown keys and bad
  values rejected with the line). Built-ins stay in code; `data/themes/{walnut,newsprint}.yaml` ship
  in `assets/themes`. Settings lists every theme of the mode.
- Piece sets (ADR 0012): nanosvg vendored (`libs/nanosvg`, zlib) and compiled into both builds;
  `SvgPieceSet` rasterizes each piece at the board's square size on first use (cached).
  `data/pieces/`: Cburnett (default), Merida (`scale: 0.92`), Chessnut, Bold (Cburnett with 1.8x
  strokes via `base` + `stroke_scale`, the e-ink high-contrast set), licences per folder. DejaVu
  glyphs stay as fallback and in golden images. `--pieces <name>` for screenshots. No
  `AssetConverter`, no PNG decoder.
- Import (ADR 0013, Home → Import): scans `imports/` when opened or on Rescan; PGN (open in Analyze or
  make a game pack with the bundled `assets/openings`), FEN (one or a chooser), game/puzzle packs,
  themes, piece sets, engine folders (binary header must match the CPU). Installs move into
  `user/{content,themes,pieces,engines}`, which deploys never touch; the app scans bundle + user.
- Settings v2: Piece set, Page buttons (swap Prev/Next), Debug logging, Tactics → Skip theme (M4 leftover). The "engine binary path"
  item of the plan is covered by importing an engine folder.
- Polish: key-only navigation for the reader's Prev/Next/Menu (Back focus above the first row in
  Settings and Import, "Open a game…" in Games, "Back to the list" in the study menu, Menu leaves
  Diagnostics); long names ellipsized; Import names the folder as seen over USB. Touch audit: every
  button ≥ 106 px (9 mm at 300 ppi) except the board screens' button row (104 px, limited by the
  panel) and the status-bar Back area (5.4 × 22.7 mm, the board is right below it).
- Packaging: `scripts/zip-folder.ps1` writes '/' entries (Windows PowerShell 5.1 writes '\');
  the Windows zip now includes `SDL3.dll` and empty `user/ logs/ imports/ exports/`.
- Tests: themes, piece sets (coverage/contrast, bold vs thin), import scan/install/game pack/keys,
  page-button swap, key-only Settings/Games. Goldens: `home` (Import button), `settings`, `import`.

## 2026-09-30 — M5: historical games

- `data/openings/{a..e}.tsv`: Lichess chess-openings (CC0, pinned commit in the README).
  `core/games/Openings`: classification by Zobrist key of every position of the main line, the deepest
  named position wins (transpositions included).
- `core/games/GamePack`: `players` (normalized names for search) + `games` (year, players, ECO,
  opening, ply count, moves as a compact blob; the PGN text is kept only for annotated games).
  `GameFilter` (player / player-vs-opponent, years, event, ECO prefix, opening, result, favorite ids),
  `count` + `page` (newest first, LIMIT/OFFSET). Favorites per pack in `chess.db`.
- `PgnImporter` (desktop tool): streams `.pgn`/`.pgn.zst`, `--max`, `--name`.
  `build-content.ps1 -Games` builds `data/generated/games/lichess-2013-01.db` (20k games, 6.5 MB,
  27 s). The committed `data/samples/games/classics.db` (Opera, Immortal with a variation and
  comments, Evergreen) always ships; `stage-bundle.ps1` stages it next to generated packs.
- Games browser: filter chips (Player via the keyboard, "A - B" for a pairing; Year; Result;
  Opening; ☆ favorites), 10 two-line rows per page, Prev/Next = page, page turns invalidate with the
  new `RefreshHint::Page` (partial even though large; the periodic full still cleans up). The log has
  "games: count N ms" / "games: page N ms" for the < 150 ms acceptance.
- Study view: board + headline + comment + move list; Prev/Next = ply, tap a move to jump, Main,
  ☆, Menu (Variations…, Analyze from here, Flip, Game info). Session resume keeps pack/game/path.
- Tests: importer (moves, comments, variations), filters/pages, browser and study flows.
  Goldens: `games`, `game-study`. Long names are ellipsized (`ui::fitText`).

## 2026-09-30 — M4: tactics trainer, rating, review

- `libs/zstd`: zstd 1.5.7 decoder as one file (the release's `combine.sh`, ASM off), `Zstd.vcxproj`
  (desktop tools only). `scripts/fetch-lichess.ps1` downloads `lichess_db_puzzle.csv.zst` (290 MB,
  6.1 M puzzles) with a `.info` provenance file; `scripts/build-content.ps1` runs the importer.
- `PuzzleImporter` (desktop, `src/tools`): streams the `.zst`, validates every FEN and move,
  reservoir-samples `--count`/`--preset` deterministically per `--seed`, theme bitmask (the 64 most
  frequent), `VACUUM`. Large preset: 50,000 puzzles, 0 rejects, 6.0 MB, 10 min (unoptimized build).
- `core/puzzles`: `PuzzleImport` (CSV -> pack; the device self-test builds its fixture pack with it),
  `PuzzlePack` (read-only packs, bounded (rating, rnd) batches with paging), `PuzzleSelector`
  (±150 -> ±300 -> any, random start, skips seen puzzles), `PuzzleSession` (opponent/user states,
  retry, hint, solution playback, mate-in-1 exception), `Progress` (`user/progress.db`: rating K=24
  on first attempts only, history, review 1/3/7/21/60 days).
- Tactics screen (Train / Review): automatic opponent move, Hint (counts as a miss), Retry,
  Solution, Next (also hardware Next), Menu (Analyze position, Check my move with engine, Flip).
  Settings → Tactics (pack, theme, difficulty, opponent move, auto-next) and Advanced (review clock
  offset for testing the schedule on the device). Home: Tactics/Review enabled, "N due" on Review,
  "Puzzle rating · review due" summary. The app's random generator follows the injectable clock.
- Tests: `tests/fixtures/puzzles-sample.csv` (60 Lichess rows, CC0; staged to `selftest/fixtures/`),
  import/pack/selector/rating/schedule/session suites, Tactics screen flows with a fake clock.
  Golden: `tactics`. Not done: theme exclusion in Settings (the selector supports it).

## 2026-09-29 — M3 start: SQLite, storage, Settings

- SQLite 3.53.4 amalgamation committed in `libs/sqlite/` (SHA3-256 checked against sqlite.org);
  options in `pchess_sqlite_config.h`, shared by `Sqlite.vcxproj` and the CMake `pchess-sqlite`
  (`THREADSAFE=0`, no extensions, WAL with `synchronous=NORMAL`).
- `core/storage`: `Database`/`Statement` (Results, transactions, ReadWrite = WAL with a DELETE
  fallback, ReadOnly = immutable URI, Memory), `migrate()` by `user_version`, `ChessDb` v1
  (`settings`, `played_games`, `favorites`).
- Settings v1: `Settings::toMap/fromMap`, loaded at start from `user/chess.db` and saved on every
  change (`AppConfig::persistSettings`, off for tests and headless screenshots). Settings screen
  (Display, Input, Engine; paged rows, toggles and choice dialogs) from Home and from "Engine
  missing"; threads/hash/Deep limit reach the engine manager; "Tap to move" is honoured. The
  Diagnostics profile toggle is gone. Goldens: `home`, `analyze-engine-missing`, new `settings`.
- Not yet checked on the reader: whether WAL works on its vfat storage (the log says
  "chess.db open (journal wal- Play mode: Home → Play → side (White/Black/Random via `util::Random`) → strength (remembered as
  the default) → `PlayScreen`. Engine moves through `EngineManager::bestMove` at the chosen
  level; Undo takes back the last move pair (cancels a running search); Draw claims threefold/50
  moves; Resign asks first; checkmate/stalemate/insufficient material/fivefold/75 moves end the
  game automatically. The result dialog offers Analyze (the game opens in Analyze at its last
  move), Export PGN (`exports/pocketchess-YYYYMMDD-HHMMSS.pgn`) and New game; every finished game
  is saved in `played_games` with `SetUp`/`FEN` headers for custom starts. Leaving an unfinished
  game asks first (not saved until SessionState). Missing/failed engine: Retry / Settings / Home.
  `drawMoveList` is shared with Analyze; `util::civilFromUnix` formats dates. Goldens: `play`,
  `home` (Play enabled).
- SessionState (ADR 0011): `chess.db` table `session` (schema v2), saved on every move/screen change
  and on background/exit; Play and Analyze implement `saveSession`/`restore`; at launch "Resume?
  [Home] [Resume]". Unfinished games survive a kill (also while the engine was thinking).
- Analyze: MultiPV lines 2–3 under the main line (Settings → Analysis lines; with 3 lines the move
  list has no room), Menu → Move… (comment, back to main line, promote, delete) and Position…
  (flip, FEN, Load PGN from `imports/`, new game); `chooseFromList` pages long lists; buttons fall
  back to the small font when a label is too wide. `util::readTextFile` (size cap).
- Home: last game (result, side, strength, date) and totals from `played_games` (schema v3 adds
  `human`); the footer names the analysis engine. Goldens: `analyze-multipv`, `analyze-comment`.
|delete)").

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

- M2 remainder: the deferred device checks listed under the resume point (refresh rate, idle shutdown, profile
  switch, battery).
- `kMinTouchPx` is a constant 106 px (9 mm at 300 ppi): right for the PB634K3, too large on the
  lower-ppi `mono-758x1024` profile (rows look sparse). Make it `profile.mm(9)` when another device
  is targeted.
- Making a game pack from a large PGN on the reader blocks the UI until done (no progress view).
- InkView Fast mode uses PartialUpdate (ADR 0002); `post()` polls while background work runs
  (ADR 0003).
- `--replay` (recorded input logs) not implemented; `--script` covers scripted sessions.

## Resume point (next steps, in order)

1. Device, M3 + M4 acceptance (deploy with `deploy.ps1 -WithContent` so `content/puzzles/large.db`
   goes too): Settings persisted across restarts (log "chess.db open (journal wal|delete)"); a Play
   game per strength; Analyze + Export PGN; resume after sleep and after a kill; Load PGN from
   `imports/`; 30 puzzles in one session with the rating kept across restarts; the log's
   "tactics: selection N ms" lines (< 50 ms); Settings → Advanced → Review clock offset +1 day
   brings failed puzzles to Review.
2. Device, M5 acceptance (`build-content.ps1 -Presets @() -Games` first, then `deploy.ps1 -WithContent`):
   Games → pack "Lichess January 2013" (20k); every filter and page turn under 150 ms in the log
   ("games: count/page N ms"), page turns partial; study view on "Classics" (the Immortal's
   variation 18. Be3), Analyze from here, favorites filter.
3. Device, M6 acceptance (= MVP 0.1.0): `build-zig.ps1 -Configuration Release -Package`, deploy;
   in color and mono: each piece set (log "pieces: set X loaded in N ms"), Walnut/Newsprint, Import
   of a PGN (open + game pack), a FEN, a theme and a piece set copied over USB; key-only use with
   Prev/Next/Menu; is the status-bar "‹ Back" easy enough to hit (5.4 mm tall)?
4. Then the deferred tuning list below, `scripts/verify-all.ps1`, and tag 0.1.0.
5. Windows: F5 → Analyze → Engine (Quick/Deep) and Play with real Stockfish (quick visual check).

Deferred until the app is feature-complete (decided 2026-09-29): strength calibration (M3 item:
preset vs preset / fixed-Elo matches on the desktop; first decide whether presets use `nodes`
limits so desktop results hold on the reader, where Stockfish 19 is ~80x slower), and the device
fine-tuning checks —
analysis refresh ≤ 1/s (Timing log + Deep), Balanced idle shutdown (> 60 s idle on Analyze),
profile switch (Settings → Eco: Quick on stockfish11, Deep on stockfish), app launch
time, 30-minute Eco vs Balanced battery, touch offset (x ~15 px left), `llvm-readelf` check of the
app's NEEDED/GLIBC. Then `scripts/verify-all.ps1` closes M0 + M2.

## Verified on hardware

PB634K3, firmware U634k3.6.10.2544, 2026-09-29 (first device run: self-test, Diagnostics, Bench).

| Date | Firmware | Item | Result |
| --- | --- | --- | --- |
| — | 6.10 | App launch time to first screen | not measured yet |
| 2026-09-29 | 6.10.2544 | `GetCanvas()` depth / scanline / format | 1072×1448, depth 24, scanline 3216 (RGB), blit with `Stretch(IMAGE_RGB)` |
| 2026-09-29 | 6.10.2544 | PartialUpdate latency: 1 square / 12 squares / FullUpdate | the calls return at once (0.5–0.6 ms per square, one 318 ms outlier after a scheduled full), `FullUpdate` 48–67 ms: they are asynchronous, so the call time is not the panel time |
| 2026-09-29 | 6.10.2544 | Touch mapping (corners + center errors) | x −10…−30 px, y +1…+24 px on 4 targets (center not tapped); x consistently a bit left |
| 2026-09-29 | 6.10.2544 | Key codes (Prev, Next, Menu, Home, Back delivered?) | Menu 0x17, Prev 0x18, Next 0x19 delivered (match `IV_KEY_*`); Home/Back never reached the app (a background/foreground pair follows: the system takes them) |
| 2026-09-29 | 6.10.2544 | Lifecycle events on sleep/wake, library, exit | sleep: `EVT_BACKGROUND`; wake ~5 s later: `EVT_UPDATE` (95) then `EVT_FOREGROUND`; exit: `EVT_EXIT`; the engine stops on background (Analyze, logged); `EVT_MTSYNC` (39) with every touch (ignored) |
| 2026-09-29 | 6.10.2544 | post(): Poll vs SendEvent | poll every 100 ms while active works (probe, Bench) |
| 2026-09-29 | 6.10.2544 | glibc version, free RAM, exec allowed in applications/pocketchess | glibc 2.39, 987 MiB RAM (471 MiB free with the app running), 4 CPUs, exec ok, 10.9 GB free |
| 2026-09-29 | 6.10.2544 | Self-test result | 76 passed, 0 failed (incl. `[integration]` with both engines) |
| 2026-09-29 | 6.10.2544 | Stockfish 19: readyok ms, RSS, knps, CPU-s | ready 2.4–2.5 s (uciok 2.1 s: net load), RSS 156 MiB after ready, peak 221 MiB (net beside the binary), **~20 knps**, depth ~11 in 1 s |
| 2026-09-29 | 6.10.2544 | Stockfish 11: readyok ms, RSS, knps, CPU-s | ready 0.6–0.7 s, RSS 37 MiB, ~200 knps, depth ~13 in 0.5 s |
| 2026-09-29 | 6.10.2544 | Engine benchmark (`logs/engine-bench.yaml`) | two runs agree; SF19 17–30 knps, SF17.1 12–21 knps, SF11 156–219 knps; CPU-s per move = movetime for all (ADR 0010) |

| 2026-09-29 | 6.10.2544 | Analyze → Engine with Stockfish 19 | Quick: first result after 3.9 s (cold start 2.3 s), then 1.6 s per position; Deep: stops itself at 30 s (Balanced, 342k nodes); going to background turns the engine off and quits it |

Engine findings (details in ADR 0010): NNUE is ~10× slower per node than Stockfish 11's classical
evaluation on the Cortex-A7 (2× on x64), for 17.1 as much as for 19, so the threat features are
not the cause. At equal device time (desktop match at the device's node rates) Stockfish 19 still
wins +25 =7 −0 against Stockfish 11: Stockfish 19 is the main engine, Stockfish 11 the Eco engine,
17.1 is dropped.
