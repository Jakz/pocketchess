# 03 — Architecture

## 1. Layers and dependency rules

```text
 platform/sdl  platform/pocketbook  platform/win32  platform/posix      (backends)
        │               │                 │               │
        └──────┬────────┴─────────────────┴───────────────┘
               ▼   implements Host / Presenter / Process / TextInput / Power
             app/        Application, ScreenStack, controllers, SessionState
               ▼
             ui/         canvas, render (dirty/refresh), theme, widgets, views, input
               ▼
             core/       chess, engine (UCI over an abstract Process), puzzles, games,
                         storage (SQLite), settings, util
               ▼
             libs        sqlite, yaml-cpp, stb   (+ SDL3 only in platform/sdl)
```

- `core` knows nothing about pixels or input. `ui` knows nothing about SDL/InkView. `app` wires
  controllers to views and owns the screen stack.
- Tools (`src/tools`) depend on `core` only (plus zstd/nanosvg/stb).
- Threads: **one UI thread**. Engine I/O uses one reader thread per engine process. Results are marshalled
  to the UI thread through `Host::post()`. Nothing else runs in the background (design notes §31.2).

## 2. Platform interfaces (`src/platform/*.h`)

```cpp
namespace pchess::platform {

struct DeviceProfile { int width, height; int ppiMono, ppiColor; bool color; std::string name; };

class Presenter {                       // receives finished pixels
public:
    virtual ~Presenter() = default;
    virtual DeviceProfile profile() const = 0;
    virtual void present(const ui::Canvas& canvas, const ui::PresentRequest& req) = 0;
};

class Host {                            // event loop services
public:
    virtual ~Host() = default;
    virtual void post(std::function<void()> fn) = 0;              // thread-safe, wakes UI thread
    virtual TimerId startTimer(std::chrono::milliseconds, std::function<void()>) = 0;
    virtual void cancelTimer(TimerId) = 0;
    virtual void quit() = 0;
    virtual TextInput& textInput() = 0;                           // PB: OpenKeyboard; SDL: own dialog
    virtual Power& power() = 0;                                   // battery %, charging
};

class Process {                         // child process with line-based stdio
public:
    static std::unique_ptr<Process> spawn(const std::filesystem::path& exe,
                                          std::vector<std::string> args,
                                          std::function<void(std::string_view line)> onLine,
                                          std::function<void(int exitCode)> onExit);
    virtual ~Process() = default;                                 // terminates + reaps
    virtual void writeLine(std::string_view) = 0;
    virtual void terminate(std::chrono::milliseconds grace) = 0;  // "quit", then kill
    virtual void setLowPriority() = 0;                            // nice(10) / BELOW_NORMAL
};
}
```

Backends:

| Concern | SDL / Windows | InkView / PocketBook |
| --- | --- | --- |
| Event loop | `SDL_WaitEvent` (no polling); `post()` → `SDL_PushEvent(user)` | `InkViewMain(handler)`. For `post()`, M0 finds a thread-safe wake-up (candidates: `SendEvent`/`SendEventTo` to self with a custom event, or a one-shot `SetHardTimer` armed from the UI thread when an engine is active). The mechanism is recorded in an ADR. |
| Present | Canvas → streaming texture (dirty rects only); the e-ink simulator (§4) post-processes | Blit dirty rects into the InkView canvas (`GetCanvas()`: depth/format recorded in M0), then `PartialUpdate` / `FullUpdate` / fast mode per `RefreshMode`. Fallback: `Stretch(IMAGE_RGB, …)` per rect, as in Enigmistica. |
| Input | Mouse → pointer down/up/move; keys (§5) | `EVT_POINTERDOWN/UP/MOVE/LONG`, `EVT_KEYPRESS/REPEAT/RELEASE` |
| Lifecycle | Window close → save → quit | `EVT_SHOW`, `EVT_FOREGROUND`, `EVT_BACKGROUND`, `EVT_EXIT`, sleep/wake, orientation. Engine stop + session save on background/exit. Exact names are confirmed against `inkview.h` 6.10. |
| Text input | Modal on-canvas keyboard widget (the same one used on device if needed) | `OpenKeyboard(...)` native keyboard, with the widget as fallback |
| Process | `CreateProcessW` + anonymous pipes + job object (kill on parent exit) | `pipe`/`fork`/`execv`, `setpriority`, `waitpid`, `SIGKILL` after grace |
| Power | Fake battery (settable from a simulator menu) | `GetBatteryPower()`, `IsCharging()` |
| Logging | `logs/pocketchess.log` + OutputDebugString | `logs/pocketchess.log` (rotated at 1 MB) + stderr |

## 3. Rendering: software canvas + dirty regions + refresh policy (P2)

- `ui::Canvas`: 24-bit RGB, device resolution (1072×1448 for Verse Pro Color), with a clip stack.
  Primitives: `fill`, `line`, `rect`, `blit(Bitmap, alpha)`, `text(font, px, utf8, color)`, and
  patterns for monochrome markers (hatch, dots, double border; design notes §17.3).
- `ui::Bitmap`: RGBA pieces decoded once with stb_image from **pre-sized PNGs** (design notes §18.1).
  A `PieceCache` keys on (set, px, mono/color), and a missing size falls back to a one-off high-quality
  downscale that is then cached.
- `FontRasterizer`: stb_truetype + glyph cache per pixel size. Bundled Noto Sans Regular/Bold.
  A figurine font for SAN is optional (backlog).
- `DirtyRegions` (ported from Enigmistica: clip, coalesce when cheaper, fall back to full over N rects)
  → `PresentRequest{rects, RefreshMode}`.
- `RefreshPolicy` decides the mode (design notes §22):
  - `Fast` (monochrome, fast waveform) for selection markers and tap feedback;
  - `Partial` for moves, move text, and eval updates;
  - `Full` on screen changes, theme changes, dialogs covering > 50 % of the screen, and every *N*
    partial updates on the board (default 12 moves, configurable);
  - color mode vs monochrome mode selects different InkView calls. The M0 spike measures what each
    call does on Kaleido 3 (latency, ghosting, color fidelity).
- Views never call `present()`. They mark dirty rects via `ViewContext::invalidate(rect, hint)`, and the
  `Application` flushes once per event.

### Board rendering (`BoardView`)

- Geometry for 1072×1448 portrait: board 1072 px wide → **134 px squares** with inside-square coordinates
  (no margins). A top status bar of about 64 px, a board area of 1072 px, and about 312 px for the panel
  (move list/eval/buttons). Layout is computed from `DeviceProfile`, so other PocketBook screens work
  (for example `--profile mono-758x1024`).
- A move invalidates only the from/to squares (plus the rook squares for castling, the captured pawn
  square for en passant, the previous last-move squares, and the check square). This is design notes §7.3
  implemented as a pure function `affectedSquares(prevState, newState)` that is unit-tested.
- Markers: selected square, legal targets (dot / ring on capture), last move, check. Each has a color
  style and a mono pattern. Color is never the only carrier of information (§39.9).

## 4. Windows e-ink simulator (`platform/sdl/EinkSimulator`)

The desktop build is a **faithful preview**, not just a window:

- A logical canvas at device resolution, shown scaled (`--scale`, and Ctrl+wheel to change it).
- Display modes (F2): **Exact RGB** / **Mono 16-gray** (quantized like the panel) / **Kaleido 3
  approximation** (luminance at 300 ppi, chroma averaged over 2×2 blocks for 150 ppi, desaturated and
  darkened with a tweakable matrix, values in `data/simulator/kaleido3.yaml`).
- Refresh visualization (F3): outlines each presented rect for 300 ms, with a color per `RefreshMode`.
  The title bar shows counters (`partial 17 / fast 42 / full 3`). A full refresh optionally shows a
  brief black flash (F4) to make them noticeable.
- An optional **latency emulation** (F6) delays presentation per mode (for example
  fast 120 ms, partial 450 ms, full 900 ms, tuned from device measurements) so that UX decisions are
  realistic.
- Hardware-key emulation (§5), a fake battery level, and sleep/background simulation (F9) that sends the
  same lifecycle events as the device.
- `--screenshot` writes the **canvas** (pre-simulation) PNG. Tests and docs use this output.

## 5. Input model

`InputEvent = PointerDown/Up/Move/LongPress{pt} | Key{KeyCode, repeat}`.
`KeyCode = Prev, Next, Menu, Home, Back, Up, Down, Left, Right, Ok`.
The Verse Pro Color has page-turn, home and menu buttons (M0 records the actual codes, and whether the
firmware lets the app receive Home).

| Semantic | Device (initial guess, verified in M0) | Desktop |
| --- | --- | --- |
| Prev / Next | `IV_KEY_PREV` / `IV_KEY_NEXT` (page buttons) | PgUp/PgDn, ←/→ |
| Menu | `IV_KEY_MENU` | M, F10 |
| Back | `IV_KEY_BACK` (if present) / long-press Menu | Esc, Backspace |
| Home | `IV_KEY_HOME` (if delivered) | Home |
| Tap / drag | touch | mouse |

- The `GestureRecognizer` turns pointer streams into tap / long-press / drag-to-move (touch down = source,
  touch up = destination, with no redraw while dragging; design notes §21.1).
- `KeyMap` is remappable (stored in settings). Each screen defines what Prev/Next do (§21.2), and the
  "hardware buttons always do something useful" rule (§39.4) is a checklist item per screen.

## 6. Application structure

- `Application` owns `Settings`, `Databases`, `EngineManager`, `ThemeManager`, `ScreenStack`, and
  `SessionState`.
- Screens (each is a controller plus a view): `Home`, `PlaySetup`, `Play`, `Analysis`, `TacticsSession`,
  `Review` (the same controller in review mode), `GameBrowser` (filters/list), `GameStudy`, `Settings`,
  `Import`, and the dialogs `Promotion`, `Confirm`, `Error`, `Resume`, `Keyboard`.
- `SessionState` (design notes §30) is serialized to `user/session.yaml` on every meaningful change
  (move, screen change) with an atomic write (temp file + rename), and on background/exit. At launch,
  when a session exists, a **Resume? [Yes] [Home]** dialog appears.
- Error handling (§32): every failure surfaces as a typed `pchess::Error{code, context}`. Screens show a
  dialog with a remedy button (for example "Engine not found. Expected: …/engines/stockfish/stockfish
  [Settings] [Continue without engine]"). The app never exits on content errors.

## 7. Chess core (D10)

- Board representation: 12 piece bitboards + occupancy, side to move, castling rights, ep square,
  halfmove/fullmove counters, Zobrist key. `Position::makeMove` returns an `Undo` record.
- Move: 16-bit `from(6) | to(6) | flags(4)` (promotion piece, castle, ep). **`MoveCodec` stores moves
  as 16-bit little-endian values** in database blobs (puzzles, games). This is the same format as the
  in-memory type, so decoding is a copy plus a legality check.
- Legal move generation: pseudo-legal generation plus a king-safety filter (pin and check masks).
  Validated with **perft** (the standard positions, including Kiwipete and the tricky ep/promotion
  suites) to depth 5–6 on desktop, with a shorter depth on the device self-test.
- `Fen` (parse/format with strict errors), `San` (generate with disambiguation, check `+` and mate `#`;
  parse tolerant variants like `0-0` and `e8Q`), `UciMove`, and `Pgn`:
  - streaming tokenizer → headers, movetext, comments `{}`, NAGs, RAVs (variations), result;
  - `GameTree` (nodes with move, comment, NAGs, children), used by study/annotations (§13.5) and
    analysis "try move / return to main line" (§8.3).
- Draw detection: threefold repetition (Zobrist history), 50-move rule, insufficient material, and
  stalemate.
- 32-bit safety: `uint64_t` bitboards, no `long` assumptions, unsigned `char` safe, and popcount/ctz
  through `std::popcount`/`std::countr_zero`.

## 8. Engine subsystem (D4, D8, P10, P11)

The app never talks to "Stockfish". It talks to **an engine selected by role and power profile**. Engines
are plug-ins described by data, so a lighter engine can be added, swapped or removed later without
touching screens or controllers.

```text
 Screens/controllers ──> EngineManager ──> EngineRegistry (engines/*/engine.yaml)
   (only Jobs)              │ roles: play / analysis / hint / validate
                            │ PowerProfile: Performance / Balanced / Eco
                            │ job queue (one active job, one live process max)
                            ▼
                         IEngine ◄── UciProcessEngine ──> platform::Process (engines/<id>/<binary>)
                            ▲                  │ reader thread → Host::post(line)
                            │                  ▼
                            │            UciParser: id, uciok, readyok, option, info (depth, seldepth,
                            │            multipv, score cp/mate, nodes, nps, time, pv), bestmove
                            └── (future) InProcessEngine: a tiny built-in engine, same interface
```

### 8.1 Interfaces

```cpp
namespace pchess::engine {
struct SearchLimits { std::optional<int> movetimeMs, depth, nodes; bool infinite = false; };
struct SearchInfo   { int depth; Score score; std::vector<chess::Move> pv; uint64_t nodes; int multipv; };
struct EngineStats  { std::chrono::milliseconds startup; uint64_t rssKb; double cpuSeconds; };

class IEngine {                                    // one running engine instance
public:
    virtual ~IEngine() = default;
    virtual const EngineDescriptor& descriptor() const = 0;
    virtual const Capabilities& capabilities() const = 0;   // parsed from UCI "option" lines
    virtual void configure(const OptionSet&) = 0;           // Threads, Hash, MultiPV, strength…
    virtual void search(const chess::Position&, std::span<const chess::Move> history,
                        const SearchLimits&, InfoCallback, BestMoveCallback) = 0;
    virtual void stop() = 0;
    virtual EngineStats stats() const = 0;                  // for energy/RAM accounting
};
}
```

Jobs (`BestMoveJob`, `AnalysisJob`, `HintJob`, `ValidateAlternativeJob`) only use `IEngine`. Tests use a
`FakeEngine` and a `FakeProcess`.

### 8.2 Engine descriptor (`data/engines/<id>/engine.yaml`, staged to `engines/<id>/`)

```yaml
id: stockfish
name: "Stockfish 19"
protocol: uci                      # only 'uci' at first; 'builtin' reserved for InProcessEngine
binary: { arm: stockfish, windows-x64: stockfish.exe }
files: []                          # e.g. [nn-xxxx.nnue] when the net is not embedded
license: GPL-3.0-or-later
roles: [play, analysis, hint, validate]
resources:                         # filled in from device measurements (M0.6/M2); shown in Settings
  disk_mb: null
  rss_mb: null                     # after readyok, excluding Hash
  startup_ms: null
  energy_class: high               # low | medium | high (relative CPU-seconds per job)
defaults: { Threads: 1, Hash: 16 }
strength:                          # label -> options + limits; validated against Capabilities
  beginner: { options: { "Skill Level": 0 },  limits: { movetime: 80 } }
  casual:   { options: { "Skill Level": 5 },  limits: { movetime: 200 } }
  club:     { options: { UCI_LimitStrength: true, UCI_Elo: 1600 } }
  strong:   { options: { UCI_LimitStrength: true, UCI_Elo: 2000 } }
  maximum:  { options: {} }
```

At engine start, the `EngineRegistry` checks the descriptor against the engine's reported `option`s.
Unknown options are dropped with a log warning. If an engine lacks a strength option, strength falls back
to node/movetime limits only. A missing binary or a malformed descriptor disables that engine with an
error dialog (design notes §32). It never crashes the app.

### 8.3 Roles and power profiles

- **Roles**: Settings → Engine lets the user pick an engine per role. Defaults come from the power profile.
  Only **one engine process is alive at a time** (RAM). A job for a different engine stops the current one
  first.
- **Power profile** (Settings → Power; design notes §20 "Power"), one table in `PowerProfile.cpp`:

  | Setting | Performance | Balanced (default) | Eco |
  | --- | --- | --- | --- |
  | Engine for play/hint | Stockfish | Stockfish | lightest installed engine (`energy_class`) |
  | Engine for analysis | Stockfish | Stockfish | Stockfish (explicit Deep only) or lightest |
  | Threads / Hash | 2 / 64 MB | 1 / 16 MB | 1 / 8 MB |
  | Analysis auto-stop | none (manual) | 30 s or depth 22 | 10 s or depth 16 |
  | Hint budget | 2 s | 1 s | 300 ms or 50k nodes |
  | Engine idle timeout | 5 min | 60 s | 0 (quit right after the job) |
  | Engine priority | normal | nice 10 | nice 19 |

  The numbers are placeholders until M2's measurements. A user-set limit in Settings always wins
  over the profile.
- **Energy model.** Energy is roughly engine CPU-seconds plus the one-off cost of loading the engine. An
  idle engine blocked on stdin costs about zero CPU but holds RAM. Restarting costs load time again. So:
  (a) every job has a budget (time, nodes or depth) and nothing searches unbounded except an explicit Deep
  analysis; (b) the idle timeout trades RAM against restart cost; (c) a lighter engine only saves energy
  if it reaches the needed strength in fewer CPU-seconds, or loads faster. That is measured, not assumed
  (§8.4).
- **Accounting.** Every job logs engine CPU-seconds (`/proc/<pid>/stat` utime+stime on the device,
  `GetProcessTimes` on Windows), wall time, nodes and peak RSS. Diagnostics shows the totals per session
  and the battery % delta. This is the data used to tune profiles later.

### 8.4 Engine candidates and selection

| Candidate | Why | Evaluate |
| --- | --- | --- |
| **Stockfish 19** (NNUE embedded) | Strongest; required for Analysis | Startup, RSS, nps, CPU-s per move at each strength label |
| **Stockfish 11** (classical eval, no net) | Same UCI behavior and Skill Level, a small binary with nothing to load, and faster per node on ARMv7 without NNUE | Same metrics |
| Other small UCI engines (Fruit-class, licence must be GPL-3 compatible) | A possible minimal footprint for play/hints | Only if SF 11 is not light enough |
| (Backlog) built-in `InProcessEngine` | Zero extra binary for Beginner/Casual and hint-light, using our own core | Only if the measurements justify it |

The M2 **engine benchmark** (Diagnostics → Engines) runs a fixed position set for each installed engine
and strength label. It records startup ms, RSS, CPU-s per move, and agreement with Stockfish's best move
(a quality proxy). It writes `logs/engine-bench.yaml`, and the results go into each descriptor's
`resources` and into an ADR choosing the defaults. `projects/engines.txt` decides which engines are
bundled. Extra engines can be installed later through Import (M6), by dropping an `engines/<id>/`
folder.

### 8.5 Protocol handling

- The state machine is `Off → Starting (uci/uciok, setoption…, isready/readyok) → Idle → Searching →
  Stopping → Idle`, then Off after the idle timeout or on lifecycle events. Every transition has a
  timeout. A missing `readyok` within 10 s kills and restarts the engine once, then reports an error.
- Jobs are cancellable. Starting a new job sends `stop`, waits for `bestmove`, and then starts. UI
  updates for analysis are throttled (at most one partial refresh per second, and only when the
  depth/PV/eval text changed) to avoid e-ink churn.
- Modes as in design notes §8.2: `Off`, `Quick` (hint), `Deep` (until stopped, or the profile's
  auto-stop). MultiPV defaults to 1.
- Strength labels (Beginner ~800 … Maximum, design notes §7.2) are mapped **per engine** through the
  descriptor's `strength` table (§8.2). For Stockfish, `UCI_Elo` has a floor (≈1320), so Beginner/Casual
  use `Skill Level` + a short move time (P10). The exact values are calibrated in M3 with a desktop
  harness that plays engine vs engine.
- Tests run against a **fake engine** (`tests/fixtures/uci/*.txt` scripted transcripts, via a
  `FakeProcess`), so the state machine is tested without any real engine. Descriptor parsing/validation
  and the profile tables are unit-tested. One optional integration test runs every engine built for
  `windows-x64`.

## 9. Storage (P4, P5)

- `storage::Database`: RAII wrapper (open flags, `busy_timeout`, prepared statements with typed
  binding, transactions). Content DBs open with `SQLITE_OPEN_READONLY` and `immutable=1`. User DBs use
  `journal_mode=WAL`, falling back to `DELETE` if WAL misbehaves on the device's vfat (tested in M4), and
  `synchronous=NORMAL`.
- Migrations: `Migrations.cpp` holds an ordered list of SQL scripts per DB and applies them by
  `user_version`, inside a transaction.
- Puzzle packs `content/puzzles/<pack>.db` (generated, read-only; several packs can be installed and are
  selected in Settings → Tactics):

  ```sql
  CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);          -- source, date, count, filters
  CREATE TABLE themes(bit INTEGER PRIMARY KEY, name TEXT UNIQUE);
  CREATE TABLE puzzles(
      id TEXT PRIMARY KEY,                -- Lichess PuzzleId
      fen TEXT NOT NULL,                  -- before opponent's first move (P12)
      moves BLOB NOT NULL,                -- 16-bit MoveCodec, first move = opponent
      rating INTEGER NOT NULL,
      rd INTEGER, popularity INTEGER, plays INTEGER,
      themes INTEGER NOT NULL,            -- bitmask into themes(bit)
      opening TEXT,
      rnd INTEGER NOT NULL                -- random 31-bit, fixed at import
  ) WITHOUT ROWID;
  CREATE INDEX idx_puzzles_rating_rnd ON puzzles(rating, rnd);
  ```

- `progress.db` (user): `puzzle_progress` as in design notes §25, plus `rating(current, updated)`,
  `rating_history(ts, rating, puzzle_id, result)`, and `review_queue` (a view over `puzzle_progress
  WHERE next_review <= now`, with an index on `next_review`).
- `games.db` (generated): the schema in design notes §26, plus `moves BLOB` (MoveCodec), `pgn TEXT` (kept
  only when the game has comments/variations), `ply_count`, `year INTEGER` (for the filter), a
  `players(id, name, normalized)` table with indexes, and `favorites` in `chess.db` referencing
  `(pack, game_id)`.
- `chess.db` (user): `settings(key, value)`, `played_games(id, date, pgn, result, mode, strength)`,
  `favorites`.
- Puzzle selection query (bounded, index-only, design notes §9.3):
  `SELECT … FROM puzzles WHERE rating BETWEEN ?lo AND ?hi AND rnd >= ?r AND (themes & ?inc) = ?inc AND (themes & ?exc) = 0 ORDER BY rating, rnd LIMIT 32`.
  The batch is filtered against `progress.db` (attached) for "unseen". If fewer than one candidate
  remains, the window widens (±150 → ±300 → any).

## 10. Puzzle trainer, rating, review

- `PuzzleSession` state machine: `ShowPosition → OpponentMove(auto, highlighted) → AwaitUser →
  (Correct → OpponentReply → AwaitUser | Solved) | (Wrong → RetryAllowed / Failed)`.
  - Mate-in-1 exception: any mating move is accepted (Lichess semantics).
  - Alternative moves: when the user's move differs from the stored move, it is accepted only if it
    mates. Otherwise the result is "wrong", with an optional **Check with engine** button
    (`ValidateAlternativeJob`, Quick mode, design notes §9.4).
- `RatingService`: Elo, K = 24, binary score (design notes §10). A first failure is final for rating
  purposes (retries do not change the rating). The history is stored.
- `ReviewScheduler`: intervals 1 → 3 → 7 → 21 → 60 days. A failure resets to 1 day. It uses an
  injectable `Clock`.
- The Home screen shows the rating and the due-review count. These come from cheap indexed queries run at
  screen entry.

## 11. Games browser and study

- `GameQuery` builds parameterized SQL from filters (player, opponent, year range, event, ECO,
  opening, result, favorites). Results are paged (20 rows per e-ink page); Prev/Next move between pages.
- `StudyCursor` walks the `GameTree` (from the `moves` blob, or from `pgn` when annotations exist).
  Prev/Next step through the main line, a tap on a move in the list jumps to it, and "Main" returns from a
  variation. Comments are shown under the board (design notes §13.5).
- "Analyze from here" opens the Analysis screen on the current position.

## 12. Desktop tools

| Tool | Input | Output | Key behavior |
| --- | --- | --- | --- |
| `PuzzleImporter` | `lichess_db_puzzle.csv.zst` (streamed with zstd, no full decompression) or `.csv` | `puzzles.db` | Filters: rating range, theme include/exclude, min popularity/plays, `--count N` (Starter/Standard/Large/Huge presets from design notes §9.2), deterministic `--seed`. Validates every FEN and move with core. Reports rejects. Runs `VACUUM` + `ANALYZE` at the end. |
| `PgnImporter` | `*.pgn` (files or folders) | `games.db` pack | Strict parse with core, per-game error log (continue on error), player-name normalization table, ECO/opening lookup from `data/openings` by position, and `--pack-name`. |
| `AssetConverter` | `data/pieces/<set>/*.svg` | `assets/pieces/<set>/<px>/*.png` | Rasterizes with nanosvg at the device square sizes (134, plus a list), producing color and mono (thresholded or dithered) variants. Validates that all 12 pieces exist (design notes §32). |

All three tools are also run by `scripts/build-content.ps1`, which produces `data/generated/`. The two
build scripts stage that folder when it exists.

## 13. Resource budgets and pluggable components

The device has **1 GB RAM, 16 GB storage, a quad-core 1.8 GHz CPU** (Verse Pro Color spec) and runs on a
battery. The plan starts **flexible**: every heavy component is a replaceable plug-in with a manifest, so
it can be swapped or shrunk later without code changes.

| Component | Plug-in unit | Manifest | Swappable at |
| --- | --- | --- | --- |
| Engine | `engines/<id>/` | `engine.yaml` (§8.2) | build (`engines.txt`) or Import |
| Puzzle pack | `content/puzzles/<pack>.db` | `meta` table (source, filters, count, format version) | build-content or Import |
| Game pack | `content/games/<pack>.db` | `meta` table | build-content or Import |
| Piece set | `assets/pieces/<set>/` | `pieceset.yaml` (name, licence, sizes, mono/color) | Import |
| Board theme | `assets/themes/<name>.yaml` | itself | Import |

Initial budgets (targets to measure against, revised with device data):

| Resource | Budget | Notes |
| --- | --- | --- |
| App RSS (no engine) | < 60 MB | Canvas ≈ 4.7 MB; piece cache ≈ 12 × 134² × 4 B ≈ 0.9 MB per set/size; SQLite page cache capped (`PRAGMA cache_size=-2048`, i.e. 2 MB per DB) |
| Engine RSS | < 250 MB incl. Hash (Stockfish gate, M0.6); target < 30 MB for an Eco engine | One live engine process at a time |
| Total disk (default bundle) | < 150 MB | App + engines + default packs; the exact Stockfish size is measured in M0.6 |
| Puzzle pack, 50k | ≈ 5–8 MB | FEN text + 16-bit moves + bitmask themes; `VACUUM`ed |
| Game pack, 10k games | ≈ 5 MB (≈ 50 MB per 100k) | Move blobs; PGN text only for annotated games |
| Energy | Engine CPU-seconds per job logged (§8.3) | Profiles tuned from these logs |

`deploy.ps1` and the Diagnostics screen print actual sizes against this table. The M6 polish pass
revisits the defaults: which engines are bundled, the default pack sizes, and the profile numbers.
