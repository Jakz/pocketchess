// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/puzzles/MistakeFinder.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"

#include <algorithm>
#include <cmath>

namespace pchess::puzzles {

using namespace chess;

int winChance(const engine::Score& s) {
    int cp = s.value;
    if (s.kind == engine::Score::Kind::Mate) cp = s.value > 0 ? 1500 : -1500;
    cp = std::clamp(cp, -1500, 1500);
    const double w = 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0);
    return static_cast<int>(std::lround(w));
}

namespace {

// The user's chance after the move, from the opponent's position (no legal moves: mate or stalemate).
std::optional<int> chanceAfter(const Position& after, const std::vector<PlyEval>& evals, size_t i) {
    MoveList legal;
    generateLegalMoves(after, legal);
    if (legal.size() == 0) return after.inCheck() ? 100 : 50;
    if (i >= evals.size() || !evals[i].score) return std::nullopt;
    return 100 - winChance(*evals[i].score);
}

} // namespace

std::vector<Mistake> findMistakes(const Position& start, const std::vector<Move>& line, Color user,
                                  const std::vector<PlyEval>& evals, int threshold) {
    std::vector<Mistake> out;
    Position pos = start;
    Position prev = start;  // before the opponent's previous move
    for (size_t i = 0; i < line.size(); ++i) {
        const Move m = line[i];
        const Position before = pos;
        pos.makeMove(m);
        if (before.sideToMove() == user && i > 0 && i < evals.size() && evals[i].score) {
            const int chanceBefore = winChance(*evals[i].score);
            const auto chance = chanceAfter(pos, evals, i + 1);
            const Move best = evals[i].best;
            if (chance && chanceBefore - *chance >= threshold && !best.isNone() && best != m) {
                Mistake x;
                x.ply = static_cast<int>(i);
                x.fen = toFen(prev);
                x.moves = {line[i - 1], best};
                x.played = m;
                x.before = chanceBefore;
                x.after = *chance;
                out.push_back(std::move(x));
            }
        }
        prev = before;
    }
    return out;
}

} // namespace pchess::puzzles
