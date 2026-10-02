// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/games/GuessSession.h"

#include <algorithm>

namespace pchess::games {

using namespace chess;

GuessSession::GuessSession(Position start, std::vector<Move> line, Side side, int fromPly, Score score)
    : pos_(std::move(start)), line_(std::move(line)), side_(side), score_(score) {
    const int n = std::clamp(fromPly, 0, plies());
    while (ply_ < n) pos_.makeMove(line_[static_cast<size_t>(ply_++)]);
}

bool GuessSession::userToMove() const {
    if (finished()) return false;
    if (side_ == Side::Both) return true;
    return pos_.sideToMove() == (side_ == Side::White ? White : Black);
}

bool GuessSession::advance() {
    if (finished() || userToMove()) return false;
    pos_.makeMove(line_[static_cast<size_t>(ply_++)]);
    return true;
}

void GuessSession::play(Outcome o) {
    switch (o) {
    case Outcome::First: ++score_.first; break;
    case Outcome::Second: ++score_.second; break;
    case Outcome::Missed: ++score_.missed; break;
    }
    lastOutcome_ = o;
    pos_.makeMove(line_[static_cast<size_t>(ply_++)]);
    misses_ = 0;
    hinted_ = false;
}

GuessSession::Verdict GuessSession::guess(Move m) {
    if (!userToMove()) return Verdict::NotYourTurn;
    if (m == expected()) {
        play(misses_ == 0 && !hinted_ ? Outcome::First : Outcome::Second);
        return Verdict::Match;
    }
    if (++misses_ >= kTries) {
        play(Outcome::Missed);
        return Verdict::Revealed;
    }
    return Verdict::Wrong;
}

Square GuessSession::hint() {
    if (!userToMove()) return NoSquare;
    hinted_ = true;
    return expected().from();
}

void GuessSession::reveal() {
    if (userToMove()) play(Outcome::Missed);
}

} // namespace pchess::games
