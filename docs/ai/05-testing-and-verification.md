# 05 — Testing and verification

## 1. Test pyramid

| Level | Where | Runs on | Examples |
| --- | --- | --- | --- |
| Unit (doctest) | `tests/core`, `tests/ui`, `tests/app` | Windows (every build), device via `pocketchess-selftest.app` | perft, FEN/SAN/PGN, MoveCodec, UCI parser, rating, scheduler, selector, DirtyRegions, `affectedSquares`, layout math |
| Component | `tests/app` | Windows | Engine state machine with `FakeProcess` transcripts; PuzzleSession flows; SessionState save/restore; DB migrations on temp files |
| Golden image | `tests/golden` | Windows + device self-test (checksum compare) | Board states, each screen with fixture data, mono and color. Exact PNG compare (the software canvas is deterministic, P2). Update with `PocketChessTests --update-golden`. |
| Tool | `tests/tools` | Windows | PuzzleImporter on `puzzles-sample.csv` → selector queries; PgnImporter round-trip; AssetConverter completeness |
| Integration `[integration]` | `tests/integration` | Windows, opt-in | Real Stockfish: `uci`→`bestmove`, analysis, and kill on exit |
| Scripted UI replay | `--replay input.log` | Windows | Recorded input sessions (for example "play 10 moves, resign, analyze") replayed with golden screenshots at checkpoints |
| Device checklist | Manual, on the PB634K3 | Device | See §3 |

Determinism rules: every random source goes through `util::Random` (seedable), every time source goes
through `util::Clock` (fakeable), and no test depends on the wall clock or the filesystem outside a temp
directory.

## 2. Windows as the verification harness

The purpose of the parallel desktop build is to catch as much as possible before copying to the device:

- The **same pixels**: the canvas is identical, so layout, fonts, markers and dirty rects are verified on
  Windows. Only panel behavior (latency, ghosting, color rendering) needs the device.
- **E-ink simulator** (architecture doc §4): mono/Kaleido approximation, refresh visualization with
  counters, and latency emulation. Rule: *if a screen needs a full refresh to look right in the
  simulator's refresh view, it will on the device too.* Minimize these.
- **Same file tree** (`pocketchess/` beside the exe) and the same engine protocol (Stockfish built from the same
  source).
- **Profiles**: `--profile` runs other resolutions to keep the layout honest.
- **Hardware-key parity**: only the semantic keys are used, and the desktop mapping exposes exactly the
  buttons the device has. Debug-only shortcuts are grouped under F-keys and compiled out of the device
  build.

## 3. Device smoke checklists (recorded in `docs/project-status.md`)

Run from a fresh `deploy.ps1`. Each item gets ✅ or ❌ with the firmware version and date.

**Every milestone (base checklist)**
1. The app launches from the Applications list in < 2 s to its first screen.
2. The first screen renders correctly in color mode and mono mode (setting toggle).
3. Every physical button does something sensible on every screen visited.
4. Sleep → wake returns to the same screen, redraws fully once, and no engine is running
   (`logs/pocketchess.log` shows the stop).
5. Home/library → back to the app (if the firmware allows it), then exit via Back/menu: the app closes
   cleanly and the reader UI is intact.
6. The log has no errors or warnings that are not understood.
7. The self-test app passes (all suites plus the golden checksums).

**M0 additions**: all spike measurements in M0.5/M0.6 recorded, and the Stockfish `bestmove` received.
**M1**: partial updates limited to affected squares (visual), promotion dialog, 20-move game without a
full refresh except the scheduled one.
**M2**: analysis updates ≤ 1/s, engine stop on leave/sleep/exit, idle shutdown per profile, profile and
engine switching, engine benchmark run and recorded, and battery % over a 30-minute Eco vs Balanced
session.
**M3**: full game per strength preset, resume after sleep and after a forced kill.
**M4**: 30-puzzle session, rating persists, Review counts, 50k-pack query timing.
**M5**: 10k-game pack filters and paging timing.
**M6**: every theme and piece set in color and mono, import flows, and the full MVP pass.

## 4. Performance budgets (device)

| Action | Budget |
| --- | --- |
| App start to first screen | < 2 s |
| Tap → selection visible | < 250 ms (fast mode) |
| Move → board updated | < 600 ms (partial) |
| Puzzle selection query | < 50 ms |
| Game filter query | < 150 ms |
| Engine readyok (cold) | < 3 s (D4 checkpoint) |
| Resident memory (app, no engine) | < 60 MB |
| Engine RSS incl. Hash | < 250 MB (Stockfish), < 30 MB (Eco target) |

The full resource table (disk, packs, energy) is in architecture doc §13. Measurements are logged with `util::ScopedTimer` at `info` level when **Diagnostics → timing log** is
enabled.
