// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Rules.h"

#include "core/chess/MoveGen.h"

#include <algorithm>

namespace pchess::chess {

bool insufficientMaterial(const Position& pos) {
    const Bitboard heavy = pos.pieces(WPawn) | pos.pieces(BPawn) | pos.pieces(WRook) | pos.pieces(BRook) |
                           pos.pieces(WQueen) | pos.pieces(BQueen);
    if (heavy) return false;
    const Bitboard knights = pos.pieces(WKnight) | pos.pieces(BKnight);
    const Bitboard bishops = pos.pieces(WBishop) | pos.pieces(BBishop);
    const int minors = popcount(knights | bishops);
    if (minors <= 1) return true;
    if (!knights && ((bishops & LightSquares) == 0 || (bishops & DarkSquares) == 0)) return true;
    return false;
}

int repetitionCount(const Position& pos, std::span<const uint64_t> history) {
    int count = 1;
    const int window = std::min<int>(pos.halfmoveClock(), static_cast<int>(history.size()));
    // Same side to move only: step back two plies at a time.
    for (int i = 2; i <= window; i += 2)
        if (history[history.size() - static_cast<size_t>(i)] == pos.key()) ++count;
    return count;
}

bool canClaimThreefold(const Position& pos, std::span<const uint64_t> history) {
    return repetitionCount(pos, history) >= 3;
}

bool canClaimFiftyMoves(const Position& pos) { return pos.halfmoveClock() >= 100; }

Outcome gameOutcome(const Position& pos, std::span<const uint64_t> history) {
    MoveList list;
    generateLegalMoves(pos, list);
    if (list.empty()) {
        if (pos.inCheck())
            return {Termination::Checkmate, pos.sideToMove() == White ? "0-1" : "1-0"};
        return {Termination::Stalemate, "1/2-1/2"};
    }
    if (insufficientMaterial(pos)) return {Termination::InsufficientMaterial, "1/2-1/2"};
    if (pos.halfmoveClock() >= 150) return {Termination::SeventyFiveMoves, "1/2-1/2"};
    if (repetitionCount(pos, history) >= 5) return {Termination::FivefoldRepetition, "1/2-1/2"};
    return {};
}

const char* terminationText(Termination t) {
    switch (t) {
    case Termination::None: return "";
    case Termination::Checkmate: return "Checkmate";
    case Termination::Stalemate: return "Stalemate";
    case Termination::InsufficientMaterial: return "Insufficient material";
    case Termination::FivefoldRepetition: return "Fivefold repetition";
    case Termination::SeventyFiveMoves: return "75-move rule";
    }
    return "";
}

} // namespace pchess::chess
