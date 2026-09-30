// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/puzzles/PuzzleSession.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"

#include <algorithm>

namespace pchess::puzzles {

using namespace chess;

namespace {

bool isLegal(const Position& pos, Move m) {
    MoveList legal;
    generateLegalMoves(pos, legal);
    return std::find(legal.begin(), legal.end(), m) != legal.end();
}

bool checkmates(const Position& pos, Move m) {
    Position after = pos;
    after.makeMove(m);
    MoveList replies;
    generateLegalMoves(after, replies);
    return replies.empty() && after.inCheck();
}

} // namespace

PuzzleSession::PuzzleSession(Puzzle p) : puzzle_(std::move(p)) {
    auto pos = parseFen(puzzle_.fen);
    if (!pos || puzzle_.moves.size() < 2) return;
    // The whole line must be legal (packs are validated at import; be safe on the device).
    Position check = pos.value();
    for (Move m : puzzle_.moves) {
        if (!isLegal(check, m)) return;
        check.makeMove(m);
    }
    pos_ = pos.value();
    userColor_ = pos_.sideToMove() == White ? Black : White;  // the opponent moves first
    valid_ = true;
}

void PuzzleSession::apply(Move m) {
    pos_.makeMove(m);
    played_.push_back(m);
}

Move PuzzleSession::pendingOpponentMove() const {
    return state_ == State::OpponentToMove && index_ < puzzle_.moves.size() ? puzzle_.moves[index_] : Move::none();
}

void PuzzleSession::playOpponentMove() {
    if (state_ != State::OpponentToMove || index_ >= puzzle_.moves.size()) return;
    apply(puzzle_.moves[index_++]);
    state_ = State::AwaitUser;
}

PuzzleSession::Verdict PuzzleSession::tryMove(Move m) {
    if (state_ != State::AwaitUser) return Verdict::NotYourTurn;
    const Move expected = puzzle_.moves[index_];
    const bool mates = isLegal(pos_, m) && checkmates(pos_, m);
    if (m != expected && !mates) {
        failedOnce_ = true;
        state_ = State::Wrong;
        return Verdict::Wrong;
    }
    apply(m);
    ++index_;
    if (mates || index_ >= puzzle_.moves.size()) {
        state_ = State::Solved;
        return Verdict::Solved;
    }
    state_ = State::OpponentToMove;
    return Verdict::Correct;
}

void PuzzleSession::retry() {
    if (state_ == State::Wrong) state_ = State::AwaitUser;
}

Move PuzzleSession::hint() {
    if (state_ != State::AwaitUser && state_ != State::Wrong) return Move::none();
    hintUsed_ = true;
    return puzzle_.moves[index_];
}

void PuzzleSession::giveUp() {
    if (finished()) return;
    failedOnce_ = true;
    state_ = State::Failed;
}

bool PuzzleSession::nextSolutionMove() {
    if (!finished() || index_ >= puzzle_.moves.size()) return false;
    // After a checkmate by an alternative move the stored line no longer applies.
    if (!isLegal(pos_, puzzle_.moves[index_])) return false;
    apply(puzzle_.moves[index_++]);
    return true;
}

int PuzzleSession::userMovesTotal() const { return static_cast<int>(puzzle_.moves.size() / 2); }

int PuzzleSession::userMovesDone() const { return static_cast<int>(std::min(index_, puzzle_.moves.size()) / 2); }

} // namespace pchess::puzzles
