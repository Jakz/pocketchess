// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"

#include <cstdint>
#include <span>
#include <string>

namespace pchess::chess {

enum class Termination {
    None,
    Checkmate,
    Stalemate,
    InsufficientMaterial,
    FivefoldRepetition,   // automatic (FIDE 9.6.1)
    SeventyFiveMoves,     // automatic (FIDE 9.6.2)
};

struct Outcome {
    Termination termination = Termination::None;
    // "1-0", "0-1", "1/2-1/2" or "*" (game continues)
    std::string result = "*";
    bool over() const { return termination != Termination::None; }
};

// `history` = Zobrist keys of the positions *before* the current one, oldest first.
Outcome gameOutcome(const Position& pos, std::span<const uint64_t> history);

// Claimable draws (threefold repetition, fifty-move rule); the Play screen offers a claim.
bool canClaimThreefold(const Position& pos, std::span<const uint64_t> history);
bool canClaimFiftyMoves(const Position& pos);

// Dead positions: K v K, K+minor v K, and bishops only, all on one square color.
bool insufficientMaterial(const Position& pos);

// Occurrences of the current position, including itself, within the reversible window.
int repetitionCount(const Position& pos, std::span<const uint64_t> history);

const char* terminationText(Termination t);

} // namespace pchess::chess
