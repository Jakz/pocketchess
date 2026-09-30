// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Move.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pchess::puzzles {

// One tactics puzzle (design notes §9.1). `fen` is the position *before* the opponent's first
// move; `moves[0]` is that opponent move, then user and opponent moves alternate (Lichess, P12).
struct Puzzle {
    std::string id;  // Lichess PuzzleId
    std::string fen;
    std::vector<chess::Move> moves;
    int rating = 1500;
    int ratingDeviation = 0;
    int popularity = 0;
    int plays = 0;
    uint64_t themes = 0;  // bitmask into the pack's themes(bit, name)
    std::string opening;  // Lichess OpeningTags (space separated), may be empty
    uint32_t rnd = 0;     // random 31-bit, fixed at import (selection order)
};

} // namespace pchess::puzzles
