// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"

#include <cstdint>
#include <vector>

namespace pchess::games {

// "Guess the move" (M7): the user plays one side (or both) of a game's main line. Each of the user's
// moves has kTries tries: right at once = First, right after a miss or a hint = Second, otherwise the
// game move is shown (Missed). The other side's moves play themselves (advance()).
class GuessSession {
public:
    enum class Side : uint8_t { White, Black, Both };
    enum class Outcome : uint8_t { First, Second, Missed };
    enum class Verdict : uint8_t { Match, Wrong, Revealed, NotYourTurn };
    static constexpr int kTries = 2;

    struct Score {
        int first = 0, second = 0, missed = 0;
        int guessed() const { return first + second + missed; }
        // First try = 2 points, second = 1; 0..100 (0 when nothing was guessed yet).
        int percent() const { return guessed() ? (200 * first + 100 * second) / (2 * guessed()) : 0; }
    };

    // `line`: the game's main-line moves from `start`; play begins after `fromPly` of them. `score`
    // carries a resumed run's totals.
    GuessSession(chess::Position start, std::vector<chess::Move> line, Side side, int fromPly = 0, Score score = {});

    const chess::Position& position() const { return pos_; }
    int ply() const { return ply_; }  // moves played since `start`
    int plies() const { return static_cast<int>(line_.size()); }
    Side side() const { return side_; }
    bool finished() const { return ply_ >= plies(); }
    bool userToMove() const;  // the next game move is one the user guesses
    chess::Move lastMove() const { return ply_ > 0 ? line_[static_cast<size_t>(ply_ - 1)] : chess::Move::none(); }
    chess::Move expected() const { return finished() ? chess::Move::none() : line_[static_cast<size_t>(ply_)]; }

    // Plays the next move when it is not the user's; false when it is the user's turn or the end.
    bool advance();
    Verdict guess(chess::Move m);
    // The game move's piece (from-square); a later right guess counts as Second.
    chess::Square hint();
    // Gives up the current move: Missed, and the move is played.
    void reveal();

    int misses() const { return misses_; }          // wrong tries on the current move
    bool hinted() const { return hinted_; }
    Outcome lastOutcome() const { return lastOutcome_; }  // of the last move the user finished
    const Score& score() const { return score_; }

private:
    void play(Outcome o);

    chess::Position pos_;
    std::vector<chess::Move> line_;
    Side side_;
    int ply_ = 0;
    int misses_ = 0;
    bool hinted_ = false;
    Outcome lastOutcome_ = Outcome::First;
    Score score_;
};

} // namespace pchess::games
