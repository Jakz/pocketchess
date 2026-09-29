# ADR 0008 — Engine subsystem: UciEngine, EngineManager and jobs

- Status: Accepted (2026-09-29). Implements architecture doc §8 (M2, first part); details below
  refine it.

## Pieces

| Piece | File | Role |
| --- | --- | --- |
| `util::Scheduler` | `core/util/Scheduler.h` | post / one-shot timers / now on the UI thread. The app adapts `platform::Host` (`app::HostScheduler`); tests use a manual scheduler with fake time. Keeps core free of `platform/Host.h`. |
| `EngineDescriptor`, `EngineRegistry` | `core/engine/` | `engines/<id>/engine.yaml` (ADR 0007). Broken descriptors and missing binaries are listed with a reason and disabled. Platform keys: `windows-x64`, `arm`, `linux-x64`. |
| `IEngine`, `UciEngine` | `core/engine/Engine.h`, `UciEngine.*` | One engine process over UCI (state machine below). |
| `PowerProfile` | `core/engine/PowerProfile.*` | The Performance / Balanced / Eco table (§8.3), placeholders until measured. |
| `EngineManager` | `core/engine/EngineManager.*` | Roles, one live process, lazy start, idle timeout, lifecycle stop, jobs, accounting. |

## UciEngine

`Off → Starting (uci → uciok, setoption…, isready → readyok) → Idle → Searching → Stopping → Idle`,
plus `Failed`.

- Timeouts: uciok 10 s, readyok 10 s, stop → bestmove 3 s (then kill), movetime searches get a
  watchdog of movetime + 5 s (then `stop`).
- A failing start (timeout or early exit) kills the process and **retries once**, then fails the
  request and enters `Failed`; the next search tries again.
- Options are validated against the engine's `option` lines (unknown dropped, spin clamped,
  check/combo checked) and only changes are sent, confirmed by `isready`. An option that is no
  longer wanted goes back to the engine's default (a play job's `Skill Level` does not leak into
  the next analysis).
- `position startpos|fen … moves …` keeps the game history (repetitions) for the engine.
- PVs are converted to `chess::Move` in the searched position and cut at the first illegal move;
  `bestmove (none)` is a valid result (mate/stalemate).
- A search while another runs stops it first; every request's done callback fires exactly once.
- `setBackgroundActivity(true)` is held while a process exists (InkView polls `post()` only then,
  ADR 0003).

## EngineManager

- **One active job.** A new job cancels the running one; the cancelled job still reports
  (`cancelled = true`, with the best move so far). Screens therefore never queue behind each other.
- **Results are delivered from the event loop** (posted), never inside the call that submitted the
  job, so callers always have the job id first.
- Engine choice per role: the user's choice (Settings) if installed, else the highest energy class
  (Stockfish-class) — except Eco, which uses the lightest engine for play/hint/validate.
- **One process at a time**: switching engines quits the current one first. Old engine objects are
  destroyed from a posted task, never inside their own callbacks.
- Idle timeout from the profile (Balanced 60 s, Performance 5 min, Eco 0 = quit right after the
  job). Background / sleep / exit stop the engine at once (P11); wake/foreground do nothing (lazy).
- Every job logs wall time, engine CPU-seconds (delta of `/proc/<pid>/stat` or `GetProcessTimes`,
  including a cold start), nodes and peak RSS; totals are shown in Diagnostics.
- Jobs: `analyze` (Quick/Deep, MultiPV), `bestMove` (strength label → descriptor options + limits,
  profile budget when the level sets none), `hint`, `validate` (searches after the candidate move and
  returns the score from the candidate mover's view).

## Analyze screen

Engine button (and Menu entry) cycles Off → Quick → Deep. Quick re-analyses each position with the
profile's short budget; Deep runs to the profile's auto-stop (Performance: until stopped). The panel
shows evaluation (White's view by default), best move, depth and the PV in SAN, redrawn at most once
per second and only when the text changed. Covering the screen (dialogs) or leaving it cancels the
job; uncovering resumes it. Sleep/background switch the panel Off. No engine → "Engine missing"
dialog (Diagnostics / Continue); the Settings button of design notes §32 arrives with Settings (M3).

## Not yet (rest of M2)

Engine benchmark (Diagnostics → Engines, `logs/engine-bench.yaml`), the Eco engine candidate
(Stockfish 11), `[integration]` test against the real Stockfish binary, per-profile numbers from
device measurements, and the M2 device checklist.
