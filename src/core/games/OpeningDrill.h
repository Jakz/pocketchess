// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"
#include "core/games/GamePack.h"
#include "core/util/Random.h"

#include <functional>
#include <vector>

namespace pchess::games {

// Opening practice (M7, design notes §15) from a game pack's opening statistics (the `book` table):
// the opponent answers with the moves the pack's players chose, weighted by how often; the user's
// move must be one of them. The drill ends where the book thins out (or after its 20 plies).
class OpeningDrill {
public:
    enum class Verdict : uint8_t { Main, Book, Wrong, Shown, NotYourTurn };
    enum class Outcome : uint8_t { Main, Book, Missed };  // stored as 0, 1, 2
    using BookFn = std::function<std::vector<BookMove>(const chess::Position&)>;
    static constexpr int kTries = 2;
    static constexpr int kMinGames = 5;     // fewer games at a position: out of the book
    static constexpr int kMinSharePct = 3;  // a reply played in fewer games is ignored

    struct Score {
        int main = 0, book = 0, missed = 0;
        int moves() const { return main + book + missed; }
    };

    OpeningDrill(chess::Position start, chess::Color user, BookFn book, util::Random& random);

    const chess::Position& position() const { return pos_; }
    const std::vector<chess::Move>& played() const { return played_; }  // since `start`
    chess::Color user() const { return user_; }
    bool userToMove() const { return !finished() && pos_.sideToMove() == user_; }
    bool finished() const { return candidates_.empty(); }
    // The book's moves here worth knowing (most played first) and the games behind them.
    const std::vector<BookMove>& candidates() const { return candidates_; }
    int games() const { return games_; }

    // The opponent's book move (weighted by games); false at the user's turn or the end.
    bool opponentMove();
    Verdict guess(chess::Move m);
    chess::Square hint();  // the most played move's piece; a later right guess still counts
    void reveal();         // plays the most played move: Missed
    int misses() const { return misses_; }
    bool hinted() const { return hinted_; }
    Outcome lastOutcome() const { return lastOutcome_; }
    int lastSharePct() const { return lastShare_; }  // the share of the user's last book move
    const Score& score() const { return score_; }

    // Resuming a saved drill: replays a move (legal, not scored) and brings back the totals.
    void follow(chess::Move m) { play(m); }
    void restoreScore(const Score& s) { score_ = s; }

private:
    void play(chess::Move m);
    void lookUp();

    chess::Position pos_;
    chess::Color user_;
    BookFn book_;
    util::Random& random_;
    std::vector<chess::Move> played_;
    std::vector<BookMove> candidates_;
    int games_ = 0;
    int misses_ = 0;
    bool hinted_ = false;
    Outcome lastOutcome_ = Outcome::Main;
    int lastShare_ = 0;
    Score score_;
};

} // namespace pchess::games
