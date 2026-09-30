// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Move.h"
#include "core/chess/Position.h"
#include "core/puzzles/Puzzle.h"

#include <vector>

namespace pchess::puzzles {

// One puzzle attempt (architecture doc §10, design notes §9.4), without UI or engine:
//   OpponentToMove -> AwaitUser -> (correct: OpponentToMove ... | Solved) | (wrong: Wrong -> retry)
// The stored line is the solution; the engine is never needed to judge a move. The one exception
// (Lichess semantics): any move that checkmates is accepted, not only the stored one.
class PuzzleSession {
public:
    enum class State { OpponentToMove, AwaitUser, Wrong, Solved, Failed };
    enum class Verdict { Correct, Solved, Wrong, NotYourTurn };

    explicit PuzzleSession(Puzzle p);

    bool valid() const { return valid_; }  // the FEN parses and the line is legal
    const Puzzle& puzzle() const { return puzzle_; }
    State state() const { return state_; }
    const chess::Position& position() const { return pos_; }
    const std::vector<chess::Move>& played() const { return played_; }  // from the puzzle FEN
    chess::Color userColor() const { return userColor_; }

    chess::Move pendingOpponentMove() const;  // none unless OpponentToMove
    void playOpponentMove();

    Verdict tryMove(chess::Move m);            // AwaitUser only; a wrong move is not played
    void retry();                              // Wrong -> AwaitUser
    chess::Move hint();                        // the expected move; marks the hint as used
    void giveUp();                             // -> Failed, the rest of the line is played
    bool nextSolutionMove();                   // Failed/Solved: nothing left -> false

    bool failedOnce() const { return failedOnce_; }
    bool hintUsed() const { return hintUsed_; }
    // For rating (architecture doc §10): solved at the first try without a hint.
    bool solvedClean() const { return state_ == State::Solved && !failedOnce_ && !hintUsed_; }
    bool finished() const { return state_ == State::Solved || state_ == State::Failed; }
    int userMovesTotal() const;
    int userMovesDone() const;

private:
    void apply(chess::Move m);

    Puzzle puzzle_;
    chess::Position pos_;
    std::vector<chess::Move> played_;
    size_t index_ = 0;  // next move of the solution line
    State state_ = State::OpponentToMove;
    chess::Color userColor_ = chess::White;
    bool valid_ = false, failedOnce_ = false, hintUsed_ = false;
};

} // namespace pchess::puzzles
