# ADR 0018 — Endgame practice against the engine

- Status: Accepted (2026-10-03). M7 backlog item 2 (endgame study, design notes §16).

## Context

Endgame technique is learned by playing known positions out against perfect defence (or perfect
attack). The design notes planned YAML position packs and an `endgames.db`; ten classic positions
do not need a database. The user wants tidy menus and every result recorded.

## Decision

1. **Content**: `data/endgames/endgames.tsv` → `assets/endgames/endgames.tsv`. Each task is one
   tab-separated line: id, category, title, goal (`mate` | `win` | `draw`), the user's side, moves
   to hold (draw tasks), FEN and advice. There are ten tasks:
   - **Checkmates:** queen, rook, two bishops, bishop and knight.
   - **Pawn endings:** king on the sixth, opposition with a spare tempo, defending king and pawn.
   - **Rook endings:** Lucena, Philidor (defend).
   - **Queen endings:** queen against a pawn on the seventh.

   Every position was checked with Stockfish 19 on the desktop (3–4 s each): the win tasks show
   mate or more than +20, the draw tasks show 0.00. Two "opposition" candidates turned out to be
   draws and were replaced. More tasks are lines in the file; bad lines are skipped and logged.
2. **Judge** (`core/games/EndgameJudge`, engine-free and unit-tested), after every move:
   - **mate:** solved on checkmate. Failed on a draw by the rules (stalemate, insufficient
     material, repetition, fifty moves), or when the engine's evaluation after its reply is back
     above −0.5 ("the win slipped away").
   - **win:** as mate, and also solved once the user has promoted and the engine is −5 or worse.
   - **draw:** solved on any draw by the rules, or after the task's number of moves.
     Failed on checkmate or when the engine reaches +3.
3. **Play**: the engine plays at "maximum" with the power profile's limits (the side to move
   first may be the engine, e.g. queen against pawn).
   - **Bottom bar:** `←` · `◀` take back the last move pair · `▶` hint (the engine's move, shown
     and highlighted) · `Menu` (Restart, Advice, Analyze from here, Flip, Back, Close).
   - **Help is recorded:** hints and take-backs are counted, and "solved on your own" means
     neither was used. Restarting an unfinished attempt records it as not solved.
4. **Entry**: Home → Puzzles → Endgames. The list shows each task's category, goal and record
   ("✓" in front when solved on your own).
5. **Records**: `chess.db` v8, `endgame_attempts` (task, times, result −1/0/1, moves, hints, take-backs,
   how it ended), saved when an attempt starts and after every move; resumable through the session.
   Stats gains an "Endgames" page (positions solved, on your own, attempts, and each position's record).

## Consequences

- On the reader the engine's limits decide how hard the defence is; Eco (Stockfish 11, 50k nodes)
  still defends these endings well. To confirm when playing on the reader.
- The "slipped away" rule relies on the engine's score after its reply, so a wrong evaluation at
  low depth could end an attempt early. The thresholds are generous; revisit with device logs.
