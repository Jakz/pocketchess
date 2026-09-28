// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Move.h"

#include <cstdint>

namespace pchess::chess {

class Position;

// Fully legal move generation: pseudo-legal generation filtered with check and pin masks
// (architecture doc §7). En passant is verified by an occupancy test.
void generateLegalMoves(const Position& pos, MoveList& out);

inline MoveList legalMoves(const Position& pos) {
    MoveList list;
    generateLegalMoves(pos, list);
    return list;
}

// Number of leaf nodes at `depth` (validation and the --bench-movegen micro-benchmark).
uint64_t perft(Position& pos, int depth);

} // namespace pchess::chess
