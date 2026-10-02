// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"
#include "core/engine/UciParser.h"

#include <optional>
#include <string>
#include <vector>

namespace pchess::puzzles {

// Personal mistakes (M7, design notes §27.3): the user's moves of a finished game that threw away
// a large part of the winning chance, as puzzles in the Lichess format (the opponent's previous move,
// then the engine's better move).

// Winning chance 0..100 for the side to move (the Lichess curve; mates count as ±1500 cp).
int winChance(const engine::Score& s);

// The engine's view of the position before main-line move `i` (side to move's view).
struct PlyEval {
    std::optional<engine::Score> score;
    chess::Move best;
};

struct Mistake {
    int ply = 0;            // the user's move (0 = White's first move)
    std::string fen;        // the position before the opponent's previous move
    std::vector<chess::Move> moves;  // that move, then the better move
    chess::Move played;     // what the user played
    int before = 0, after = 0;  // the user's winning chance before and after the move
};

// `evals[i]` for every i in [0, line.size()] that the finder reads: the user's positions and the
// ones right after them (a position without legal moves needs none). Missing scores skip the move.
// A move is a mistake when the chance drops by `threshold` points or more and differs from `best`.
std::vector<Mistake> findMistakes(const chess::Position& start, const std::vector<chess::Move>& line, chess::Color user,
                                  const std::vector<PlyEval>& evals, int threshold = 20);

} // namespace pchess::puzzles
