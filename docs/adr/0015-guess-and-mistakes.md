# ADR 0015 — Guess the move and My mistakes, with every result in the user database

- Status: Accepted (2026-10-03). M7 backlog items 1 (personal mistakes, design notes §27.3, §28)
  and a new "guess the move" mode for the historical packs.

## Context

Before tuning the app on the device, the user picked the next features: learning from master games
by guessing their moves, and turning their own Play games into puzzles. Two requirements came
with them: keep the menus as tidy as they are (ADR 0014), and record every statistic in the existing
user databases under `user/`, so that a later statistics screen can read it.

## Decision

1. **Guess the move** (`core/games/GuessSession`, `app/GuessScreen`). The user plays White,
   Black or both sides of a game's main line. Each move gets two tries. Right at once counts as
   *first* (2 points); right after a miss or the hint counts as *second* (1 point); otherwise the
   move is *shown*. The other side replies after 0.5 s. Bottom bar: `←` `◀` hint `▶` show move
   `Menu`. The menu offers Study this game, Guess another game, Flip, Back, Close.
   - Entry points: the study menu ("Guess the moves…", from the main-line position at or before
     the current move) and the Games menu ("Guess the moves of a random game…", picked among the
     games the filters show; games under 20 plies are avoided).
   - Records: `chess.db` v5. `guess_runs` holds one row per run (pack, game, side, plies, scores,
     finished) and is updated after every guessed move. `guess_moves` holds one row per guessed
     move (outcome, first wrong try, hint used). A run is resumed through the session (key `guess.run`).
2. **My mistakes** (`core/puzzles/MistakeFinder`, `app/MistakeScan`, `TacticsScreen::Mode::Mistakes`).
   - Detection: the user's moves in finished Play games whose winning chance (the Lichess curve
     on the engine's score) drops by **20 points or more** and that differ from the engine's move.
     Each becomes a puzzle in the Lichess format: the opponent's previous move, then the better move.
   - On demand, not in the background: Home → Puzzles → My mistakes checks the games that no
     scan has seen. It runs one Hint job per needed position (the user's positions and the ones
     right after), so it uses the power profile's hint limits (Eco: Stockfish 11, 50k nodes) and
     shows a progress dialog with Stop. A game is marked as checked only once it is complete, so
     a stopped scan, or an engine job from another screen superseding it, simply resumes with
     that game next time.
   - Records: `chess.db` v6. `mistakes` stores each mistake (game, ply, FEN, moves, the move
     played, chance before/after). `mistake_scans` records which games were checked, by which
     engine, and how many mistakes each had. Solving goes to `progress.db` like every puzzle:
     pack "My mistakes", id `m<id>`, never rated, so the review schedule is the same.
   - The queue lists mistakes never tried first, then those due for review.
     Puzzles → Review stays per puzzle pack and does not include them.
3. **Menus**: Puzzles gains a third entry (Train · Review · My mistakes, with "N to solve · M new
   games"); the study and Games menus gain one entry each. No new Home button, no top-bar controls.

## Consequences

- Both modes work without new content; Guess the move works on every pack, "My games" included.
- The engine's hint quality decides which mistakes are found. On the reader under Eco, a game
  takes about 40 hint jobs (~15–30 s); still to measure there (deferred with the other tuning).
- Statistics are complete for a future stats screen: guess runs/moves, mistakes per game, and
  every puzzle attempt.
