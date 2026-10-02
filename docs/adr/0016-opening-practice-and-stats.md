# ADR 0016 — Opening practice from the packs' book, and a statistics screen

- Status: Accepted (2026-10-03). M7 backlog item 3 (opening study, design notes §15), plus the
  statistics screen that reads what ADR 0015 records.

## Context

Every game pack already has a `book` table holding the moves played in the first 20 plies, with
counts and results (M5, packs v2). That is enough for a repertoire drill without a separate
`OpeningTreeBuilder` or `openings.db`. The user asked for every result to stay in the user
database and for menus to stay tidy.

## Decision

1. **Opening practice** (`core/games/OpeningDrill`, `app/DrillScreen`).
   - **Book source:** the book is the **largest installed game pack** (by file size), whatever
     pack Games is showing (`Application::bookPack()`). With the historical pack installed, that
     is master practice.
   - **Moves that count:** a position is "in the book" when its games number at least 5, and a
     move counts when at least 3% of those games (and at least 2) chose it.
   - **The opponent** picks among those moves, weighted by games, so the user meets the lines
     players actually choose.
   - **The user's move** scores as *main* (the most played move), *book* (another counted move),
     or *missed* (two tries outside the book, or ▶; the most played move is then shown).
     ◀ highlights the main move's piece.
   - **End:** the drill ends where the book does. Menu offers Analyze from here, Practice
     again, Flip, Back and Close.
   - **Entry points:** in Openings, tapping a line asks *Study in Analyze* / *Practice as White*
     / *Practice as Black*. Openings → Menu → *Practice from the start…* starts from move one.
   - **Records:** `chess.db` v7. `drill_runs` holds one row per drill (pack, side, start moves,
     opening name, counts) and is updated after each move of the user. `drill_moves` records each
     of the user's moves (FEN, move, outcome, share, first wrong try, hint). The FEN makes a later
     "positions you keep missing" review possible. Resumable through the session.
2. **Stats** (`app/StatsScreen`, Home tools row: Stats · Settings · Import · Diagnostics). One
   page per mode, read on opening:
   - **Puzzles:** rating, highest, tried, solved and never failed, attempts, in review, due, and
     a column chart of the last 120 rated attempts. Columns rather than lines, so the chart is
     crisp on e-ink.
   - **Play:** won/drawn/lost, score, and the same per strength.
   - **Guess the move:** first, second and shown counts, score, and the last games.
   - **Opening practice:** main, book and missed counts, and the last openings.
   - **My mistakes:** games checked, mistakes found, tried, solved, in review, and to solve now.

   ◀ ▶ turn pages; Menu lists them, then "← Back to Home" and "Close".
   The queries live in `ChessDb` (`playStatsByStrength`, `recentGuessRuns`, `recentDrillRuns`,
   `mistakeStats`) and `Progress::totals(pack, only)`.

## Consequences

- No new content files. A pack built from few games gives a shallow book: the drill then ends
  after a few moves (the classics alone are not enough; the historical or Lichess packs are).
- Opening practice reads the book once per position (an indexed lookup, as Analyze already does).
- The statistics screen only reads, so it can grow pages without schema changes.
