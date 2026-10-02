// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"
#include "core/util/Error.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pchess::games {

// Endgame practice (M7, design notes §16, ADR 0018): known positions played out against the engine.
struct EndgameTask {
    enum class Goal { Mate, Win, Draw };
    std::string id, category, title;
    Goal goal = Goal::Mate;
    chess::Color side = chess::White;  // the user's side (the engine moves first when it is not to move)
    int holdMoves = 0;                 // Draw: moves of the user to hold
    std::string fen;
    std::string advice;
};

// data/endgames/endgames.tsv: '#' comments, then id, category, title, goal, side, moves, fen, advice
// separated by tabs. Bad lines are skipped and counted in `rejected`.
std::vector<EndgameTask> parseEndgames(std::string_view tsv, int* rejected = nullptr);

// The rules of one attempt, apart from the engine: after each move it says whether the task is
// still on, solved or lost.
class EndgameJudge {
public:
    enum class Verdict { Continue, Solved, Failed };
    struct Result {
        Verdict verdict = Verdict::Continue;
        std::string reason;  // "Checkmate.", "Draw by stalemate: the win slipped away." …
    };

    explicit EndgameJudge(const EndgameTask& task) : task_(task) {}

    // After any move. `history`: Zobrist keys before the current position (repetitions);
    // `engineScoreCp`: the engine's evaluation from its own side after its reply (mate = ±100000),
    // when known; `userPromoted`: the user has promoted a pawn in this attempt; `userMoves`: moves made by the user.
    Result judge(const chess::Position& pos, std::span<const uint64_t> history, std::optional<int> engineScoreCp,
                 bool userPromoted, int userMoves) const;

private:
    const EndgameTask& task_;
};

} // namespace pchess::games
