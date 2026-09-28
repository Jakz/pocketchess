# 04 — Milestones

Every milestone ends with the same **definition of done**:

1. `scripts/verify-all.ps1` is green: MSVC Debug + Release, all tests, PB Release bundle, ELF checks,
   source-list and include-boundary checks.
2. The milestone's **device checklist** (see `05-testing-and-verification.md`) has been run on the
   PB634K3, with the results recorded in `docs/project-status.md`.
3. `docs/project-status.md` has an updated "Working now / Not working yet / Resume point", and ADRs are
   added for any decision taken.

Sizes: S ≈ ≤ 1 day, M ≈ 2–4 days, L ≈ 1–2 weeks of focused work (rough guidance for ordering, not
commitments).

---

## M0 — Harness and platform spike (L)

Goal: both builds exist and are wired together, and the five risks in design notes §41 are answered on the
real device.

### M0.1 Repository skeleton (S)
- Create the layout from `02-repository-and-build.md` §1: `.gitignore` (`/out/`, `.vs/`, `*.user`,
  `/data/downloads/`, `/data/generated/`), `.gitattributes` (LF for `.sh`/`.cmake`, CRLF for
  `.sln`/`.vcxproj`/`.ps1`), `.editorconfig`, `.clang-format`, `LICENSE`, `README.md`.
- `AGENTS.md`: goals, the parity rules, layering rules, naming (CamelCase files, lowercase dirs,
  `pchess::` namespaces mirroring directories), error-handling style, "no platform headers in core/ui",
  "update both source lists", "never overwrite `user/`", and the doc-update duty.
- `git init`, then a first commit.

### M0.1b Private GitHub repository and optional CI (S)
- Create a private GitHub repository (for example `pocketchess`) and push `main`. Protect `main` from
  force-pushes.
- The repo holds only first-party code, build files, docs, small data, and the small vendored libraries.
  **Engine sources and networks are not committed** (`libs/stockfish*/` is ignored; see build doc §2).
  Only their pins, build files and descriptors are committed, and `scripts/fetch-engines.ps1` restores them.
  Generated content (`data/generated/`, puzzle and game DBs) and downloads stay ignored, and are rebuilt
  by `scripts/build-content.ps1`. There is no Git LFS.
- Optional CI (GitHub Actions, Windows only): build `Desktop|x64` and run `PocketChessTests`. This requires a
  runner with VS 2026 (toolset v145). If GitHub's hosted images don't have it yet, keep CI off, or use a
  self-hosted runner on this PC. The PocketBook build stays local, because the Zig/InkView toolchain lives
  outside the repo (D3).

### M0.2 Vendor libraries (S)
- Copy `sdl3/`, `stb/`, `yaml/` from Enigmistica's `libs/` (same pins). Add `sqlite/` (amalgamation),
  `doctest/`, and `stb_image_write.h`. Add fonts to `data/fonts/`.
- Write `libs/README.md` (the Enigmistica table format).
- Defer `stockfish/` to M0.6 (other engines to M2), and `zstd/` + `nanosvg/` to M4/M6, when their tools start.

### M0.3 MSVC solution (M)
- `Common.props`, `Sqlite`, `YamlCpp`, `SDL3` (copied project), `PchessCore`, `PchessUi`,
  `PchessPlatformDesktop`, `PocketChess`, and `PocketChessTests`, with the Debug/Release/Desktop configurations.
- A minimal `Canvas`, `DirtyRegions` (ported), `Presenter`, and `SdlHost` with an event-driven loop.
- `PocketChess.exe` opens a 1072×1448 logical window scaled to fit, draws a static 8×8 board from a
  hard-coded position with placeholder pieces (letters in circles), and highlights the tapped square with
  a partial present.
- The post-build step stages the `pocketchess/` tree. F5 works from a clean checkout.

### M0.4 PocketBook build (M)
- `projects/pocketbook/CMakeLists.txt`, `sources.cmake`, `build-zig.ps1`, `check-elf.ps1`,
  `deploy.ps1`, `README.md`, and `PocketBook.vcxproj` (a Makefile project in the solution).
- `src/platform/pocketbook/Main.cpp` + `InkViewHost` + `InkViewPresenter` render the **same** board
  scene from the same `app` code.
- `scripts/check-sources.ps1` and `scripts/verify-all.ps1`.

### M0.5 Device spike app (M)
Build a diagnostics screen into the app (hidden behind Menu → "Diagnostics", kept permanently) that
measures and logs:
- screen size, canvas depth/stride/format from `GetCanvas()`, and orientation;
- per-square partial update, a 12-square sequence, and full update: **wall-clock latency** of each InkView
  update call in color and mono content (logged). Visual ghosting is judged by eye;
- the touch coordinate mapping (tap the 4 corners and the center; log the reported points);
- key codes for every physical button, including whether Home and Back are delivered;
- lifecycle events received on sleep/wake, on switching to the library and back, and on exit;
- the thread-safe wake-up mechanism for `Host::post()` (at least two candidates, keep the one that works);
- glibc version (`gnu_get_libc_version()`), free RAM, CPU count, and whether `applications/pocketchess/`
  is writable and allows exec (the engines live there).

### M0.6 Stockfish cross-build and engine plug-in skeleton (M)
- Put Stockfish 19 `src/` + its `.nnue` into `libs/stockfish/` (local only, ignored by Git). Commit
  `engine.lock`, `BUILDING.md` (the replicated Makefile flags) and `CMakeLists.txt` under
  `projects/pocketbook/engines/stockfish/`, and write `scripts/fetch-engines.ps1`.
- `build-engine.ps1` + `projects/engines.txt` for `arm` and `windows-x64`, and a first
  `data/engines/stockfish/engine.yaml`. The Windows smoke test uses `bench`.
- Diagnostics screen: spawn `engines/stockfish/stockfish` with `PosixProcess`, run `uci`/`isready`, and
  measure the **time to `readyok`** (NNUE load), RSS after load (`/proc/<pid>/status`), `go movetime 1000`
  nps, `bench` time, and the engine's CPU-seconds (`/proc/<pid>/stat`). Kill it cleanly on exit.
- **Checkpoint for D4**: Stockfish stays the default for everything if time to readyok < 3 s and RSS
  < 250 MB. Otherwise it stays for explicit Deep analysis only, and the Eco engine work in M2 moves up.
  The plug-in design (D8) makes this a data change, not a rewrite.

**M0 acceptance**: one VS build produces `PocketChess.exe` **and** `pocketchess.app`. The device shows the
board, reacts to taps with partial updates, logs all buttons, and runs Stockfish and gets `bestmove`. The
measurements are in `docs/project-status.md` and ADRs 0001–0004 (toolchain, canvas/present path, post
mechanism, engine choice) are written.

---

## M1 — Chess core and board UI (L)

### Core (M–L)
- `Square/Bitboard/Move/Position/MoveGen/Zobrist/Fen/San/UciMove/MoveCodec/GameTree/Pgn`.
- Tests: perft suite (`tests/fixtures/perft.epd`) to depth 5 in Release, FEN round-trip, SAN
  generation/parse with disambiguation/promotion/castling/ep/check/mate, a PGN corpus (small CC0/own
  sample with comments, NAGs and nested variations), draw rules, and MoveCodec round-trip.
- A micro-benchmark (`--bench-movegen`) runs on desktop and in the device self-test, so the ARM speed is
  known.

### UI (M)
- `Canvas` primitives, `FontRasterizer`, and `PieceCache` using temporary PNGs (a single CC0/GPL set
  rasterized by hand until M6's converter exists).
- `BoardView`: coordinates, orientation/flip, selection, legal-target markers, last move, check
  indicator (color and mono styles), and `affectedSquares()` for minimal invalidation.
- Tap-to-move plus drag-to-move without continuous redraw. A promotion dialog.
- `RefreshPolicy` v1: Fast for selection, Partial for moves, Full every N moves and on screen change.
- Golden-image tests: the board in the start position, after 1.e4, with a selection, in check, flipped,
  and in mono. PNGs are stored in `tests/golden/`.

### App (S)
- An "Analyze board" sandbox screen: make moves for both sides, undo/redo, flip, FEN import/export
  (keyboard dialog), and hardware Prev/Next = undo/redo.

**M1 acceptance**: all legal-move rules work on both platforms. The device self-test app passes on
the PB634K3. Moving a piece on the device refreshes only the affected squares (verified visually and in
the log).

---

## M2 — Engine integration, plug-ins and power profiles (L)

- `IEngine`, `UciParser`, `UciProcessEngine` (state machine, timeouts, restart-once), `EngineRegistry`
  (loads descriptors and validates them against the reported options), `EngineManager` (roles, one live
  process, lazy start, per-profile idle shutdown, lifecycle stop), the jobs (`BestMove`, `Analysis`,
  `Hint`, `ValidateAlternative`), `PowerProfile`, and per-engine strength mapping (architecture doc §8).
- Per-job accounting: CPU-seconds, wall time, nodes and peak RSS are logged and summed in Diagnostics.
- **Eco engine candidate**: Stockfish 11 in `libs/stockfish11/` (local only, with an `engine.lock` pin),
  with its own descriptor and CMake project, built by the same `build-engine.ps1`.
- **Engine benchmark** (Diagnostics → Engines): a fixed position set × strength labels × installed
  engines, run on desktop and device → `logs/engine-bench.yaml`. An ADR records the chosen defaults per
  profile and whether the Eco engine is bundled (`engines.txt`).
- `Win32Process` (job object so the engine dies with the app) and `PosixProcess` (`setpriority`, SIGKILL
  fallback, reap).
- Analysis panel v1 on the sandbox screen: eval (cp/mate, from White's view with a toggle), depth, best
  move, and PV in SAN (converted via core), with a throttled refresh. Engine Off/Quick/Deep toggle.
- "Engine missing" handling (design notes §32): a dialog with Settings / Continue without engine. All
  features degrade gracefully.
- Tests: `FakeProcess` transcripts (normal search, mate scores, multipv, crash mid-search, no readyok,
  and output with extra whitespace and unknown tokens). Integration test on desktop with the real binary
  (tagged `[integration]`).

**M2 acceptance**: on the device, analysis starts within the M0-measured time and updates at most once
per second. Leaving the screen, sleep, and exit all stop the engine (no stray process in
`/proc`). The profile's idle timeout shuts the engine down. Switching the power profile or engine in
Settings takes effect on the next job without a restart. Removing an engine folder disables it gracefully.
Benchmark results are recorded for both engines.

---

## M3 — Play mode, Analysis screen, Home, session (L)

- **Home** screen (design notes §6.1): Play / Tactics / Games / Analyze / Review, the last-session summary,
  and a battery + engine footer that updates only on screen entry.
- **Play**: setup dialog (White/Black/Random, strength presets), then the game. Engine replies through
  `BestMoveJob`. Undo (takes back the move pair), resign, flip, draw claims, and a result dialog → save
  the PGN to `chess.db.played_games`, plus an optional export to `exports/*.pgn`, and
  "Analyze this game".
- **Analysis** screen: from the current game, a FEN, or a PGN position. MultiPV 1–3, depth/time limit,
  try move → a variation in the `GameTree`, return to main line, save annotation (a comment on the node).
  Hardware Prev/Next navigate plies.
- **Settings** v1: power profile, engine per role, engine strength defaults, threads/hash, analysis limits, full-refresh interval,
  color/mono, coordinates, orientation, tap vs drag. Stored in `chess.db`.
- **SessionState** + Resume dialog (design notes §30). Covers the current screen, game, move index,
  orientation, and analysis state (never a running engine).
- Strength calibration: a desktop test harness plays preset vs preset and preset vs fixed-Elo engine, to
  tune the Beginner/Casual values for each installed engine. Results go into the engine descriptors
  (`strength:`) + an ADR.

**M3 acceptance**: a full game vs each preset on the device, then save, analyze, and resume after a forced
sleep and after killing the app. The desktop golden images for the Play/Analysis screens are stable.

---

## M4 — Tactics trainer, rating, review (L)

- Vendor `zstd/` (decoder). Build `ToolsCommon` + `PuzzleImporter` (§12 of the architecture doc). Add
  `scripts/fetch-lichess.ps1`: this downloads the puzzle DB into `data/downloads/` and is the only script
  that touches the network. It is never run by builds.
- `PuzzleRepository`, `PuzzleSelector` (the bounded query + widening), `PuzzleSession`, `RatingService`,
  `ReviewScheduler`, and progress DB migrations.
- **Tactics** screen: auto opponent move with a highlight, retry/fail flow, solved/failed banner,
  Next (hardware Next after completion), hint (engine Quick, counted as failed for rating), "Check with
  engine" for alternatives, and "Analyze position".
- **Review** screen: the due queue (count on Home), the same session UI, and the scheduler updates.
- Theme filters (include/exclude/random), a rating-limited session, and "auto-next" / "show opponent
  move automatically" settings (design notes §20 Tactics).
- Tests: selector determinism with a seeded fixture DB built by the importer from
  `tests/fixtures/puzzles-sample.csv`. Rating math, scheduler intervals with a fake clock, and session
  state transitions including the mate-in-1 exception.
- Device: measure selection query time on the **50k Standard pack** (target < 50 ms), DB size, and
  WAL behavior on the user partition.

**M4 acceptance**: 30 puzzles on the device in one session with the rating persisting across restarts.
Failed puzzles reappear in Review on the scheduled day (tested with the fake clock on desktop and a
manual clock offset setting in Diagnostics on device).

---

## M5 — Historical games (L)

- `PgnImporter` + `data/openings` (ECO TSV) + player normalization.
- `GameRepository`, `GameQuery` (filters from design notes §13.3/§29), and favorites.
- **Games** screens: a browser (filter chips; player search using the keyboard; paged list with Prev/Next =
  page), a study view (board + move list + comment panel under the board, Prev/Next = ply, tap a move to
  jump, variations entry/exit, "Main"), and "Analyze from here".
- A small bundled sample pack from public-domain/own PGNs in `data/samples/`. The user's own packs go
  in through the importer on desktop, or through Import (M6) on device.
- Tests: importer round-trip (PGN → DB → GameTree → PGN equality on moves), filter SQL, and
  pagination.

**M5 acceptance**: browsing a pack of ≥ 10k games on the device, with every filter < 150 ms and page
turns doing only partial refreshes.

---

## M6 — Themes, piece sets, import, polish (M–L)

- Vendor `nanosvg/`. Build `AssetConverter`. Choose 2–4 piece sets with licences compatible with
  GPL-3 and write them into `data/pieces/<set>/LICENSE`. Lichess's piece-set list documents per-set
  licences and is the starting point. Also make a high-contrast e-ink set (own design).
- `ThemeManager` + YAML descriptors (design notes §19), with built-ins: Paper (color), Slate (color),
  ClassicMono, and HighContrastMono. Validate the schema with clear errors, and fall back to the
  default theme on malformed files (§32).
- **Settings** v2: board theme, piece set, color/mono, coordinates, full-refresh interval, key remapping,
  engine binary path, debug logging.
- **Import** screen (design notes §34): scan `imports/` **only on request**. PGN → the Analysis/Study
  viewer; FEN → Analysis; `.db` packs → validate the schema/meta and move them into `content/`; piece
  set/theme folders → validate and install.
- Refresh tuning using the M0–M5 device logs: default N, fast-mode use, and color vs mono calls.
- Resource tuning (architecture doc §13): revisit the power-profile numbers from the CPU-second logs,
  choose the bundled engines and default pack sizes, and check the bundle against the disk budget.
- Import also accepts `engines/<id>/` folders (descriptor validated, binary checked as ARM ELF).
- Polish pass: touch-target audit (≥ 9 mm), and a check of every screen's hardware-button behavior, mono
  legibility, and error dialogs.

**M6 acceptance (= MVP release 0.1.0, design notes §38)**: every MVP bullet works on the device in color
and mono. A release bundle zip can be produced by `build-zig.ps1 -Configuration Release -Package`, and a
Windows zip by `projects/msvc/build.ps1 -Package`.

---

## M7+ — Backlog (not detailed; ordered by expected value)

1. **Personal mistakes** (design notes §27.3, §28): a desktop `MistakeMiner` tool (PGN → desktop Stockfish
   analysis → `personal_puzzles.db` with the source-game metadata). Reuses the M4 trainer with a "My
   Mistakes" pack filter.
2. **Endgame study** (§16): YAML position packs (§35) → an `endgames.db` or a YAML folder, a
   goal-driven play-out vs the engine, and success detection (mate, win of material, or draw held for N
   moves).
3. **Opening study** (§15): a desktop-built opening tree (`OpeningTreeBuilder` from PGN) → `openings.db`
   with move frequencies, and a repertoire drill mode.
4. **Lessons / learn mode** (§36): a lesson YAML format (text/diagram/question/interactive/explanation).
5. Glicko-2 rating option (§10 option B).
6. Other targets: Linux desktop (SDL, trivially from the existing code), other PocketBook devices
   (`--profile`), Android/Kobo frontends.
7. Figurine notation font, opening-book moves in Play, PGN export to USB.
