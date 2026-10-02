// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/games/Endgames.h"

#include "core/chess/Fen.h"
#include "core/chess/Rules.h"

#include <charconv>

namespace pchess::games {

using namespace chess;

namespace {
constexpr int kLostCp = 300;     // Draw tasks: the engine reaching this is winning
constexpr int kDrawnCp = 50;     // Win/mate tasks: the engine back to this is out of danger
constexpr int kWonCp = 500;      // Win tasks: after a promotion, the engine this far behind has lost
} // namespace

std::vector<EndgameTask> parseEndgames(std::string_view tsv, int* rejected) {
    std::vector<EndgameTask> out;
    int bad = 0;
    size_t i = 0;
    while (i < tsv.size()) {
        size_t j = tsv.find('\n', i);
        if (j == std::string_view::npos) j = tsv.size();
        std::string_view line = tsv.substr(i, j - i);
        i = j + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string_view> f;
        size_t a = 0;
        for (;;) {
            const size_t b = line.find('\t', a);
            f.push_back(line.substr(a, b == std::string_view::npos ? std::string_view::npos : b - a));
            if (b == std::string_view::npos) break;
            a = b + 1;
        }
        EndgameTask t;
        bool ok = f.size() == 8;
        if (ok) {
            t.id = f[0];
            t.category = f[1];
            t.title = f[2];
            if (f[3] == "mate") t.goal = EndgameTask::Goal::Mate;
            else if (f[3] == "win") t.goal = EndgameTask::Goal::Win;
            else if (f[3] == "draw") t.goal = EndgameTask::Goal::Draw;
            else ok = false;
            if (f[4] == "white") t.side = White;
            else if (f[4] == "black") t.side = Black;
            else ok = false;
            ok = ok && std::from_chars(f[5].data(), f[5].data() + f[5].size(), t.holdMoves).ec == std::errc();
            t.fen = f[6];
            t.advice = f[7];
            ok = ok && !t.id.empty() && parseFen(t.fen).ok();
        }
        if (ok) out.push_back(std::move(t));
        else ++bad;
    }
    if (rejected) *rejected = bad;
    return out;
}

EndgameJudge::Result EndgameJudge::judge(const Position& pos, std::span<const uint64_t> history,
                                         std::optional<int> engineScoreCp, bool userPromoted, int userMoves) const {
    const Outcome o = gameOutcome(pos, history);
    const bool draw = o.over() ? o.termination != Termination::Checkmate
                               : canClaimThreefold(pos, history) || canClaimFiftyMoves(pos);
    const bool mated = o.termination == Termination::Checkmate;
    const bool userMated = mated && pos.sideToMove() == task_.side;
    const char* why = o.over() ? terminationText(o.termination)
                               : canClaimThreefold(pos, history) ? "threefold repetition" : "the fifty-move rule";
    if (task_.goal == EndgameTask::Goal::Draw) {
        if (userMated) return {Verdict::Failed, "Checkmate: the position was lost."};
        if (draw) return {Verdict::Solved, std::string("Draw by ") + why + "."};
        if (engineScoreCp && *engineScoreCp >= kLostCp) return {Verdict::Failed, "The engine is winning now."};
        if (task_.holdMoves > 0 && userMoves >= task_.holdMoves)
            return {Verdict::Solved, "Held for " + std::to_string(task_.holdMoves) + " moves: a draw."};
        return {};
    }
    if (mated && !userMated) return {Verdict::Solved, "Checkmate."};
    if (userMated) return {Verdict::Failed, "Checkmate against you."};
    if (draw) return {Verdict::Failed, std::string("Draw by ") + why + ": the win slipped away."};
    if (engineScoreCp && *engineScoreCp > -kDrawnCp) return {Verdict::Failed, "The win slipped away (the engine holds now)."};
    if (task_.goal == EndgameTask::Goal::Win && userPromoted && engineScoreCp && *engineScoreCp <= -kWonCp)
        return {Verdict::Solved, "Promoted, and the win is clear."};
    return {};
}

} // namespace pchess::games
