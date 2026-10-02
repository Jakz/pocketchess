// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/MistakeScan.h"

#include "app/Application.h"
#include "core/chess/MoveGen.h"
#include "core/chess/Pgn.h"
#include "core/chess/San.h"
#include "core/util/Log.h"

namespace pchess::app {

using namespace chess;

MistakeScan::MistakeScan(Application& app) : app_(app) {}

MistakeScan::~MistakeScan() {
    alive_.reset();
    if (job_) app_.engines().cancel(job_);
}

void MistakeScan::start(ProgressFn onProgress, DoneFn onDone) {
    onProgress_ = std::move(onProgress);
    onDone_ = std::move(onDone);
    status_ = {};
    games_.clear();
    gameIndex_ = 0;
    if (storage::Database* db = app_.userDb())
        if (auto g = storage::uncheckedPlayedGames(*db); g) games_ = std::move(*g);
    status_.games = static_cast<int>(games_.size());
    running_ = true;
    util::Log::info("mistakes", "scan: " + std::to_string(games_.size()) + " games to check");
    nextGame();
}

void MistakeScan::cancel() {
    if (!running_) return;
    if (job_) app_.engines().cancel(job_);  // the callback reports cancelled and stops the scan
    else stop(false);
}

void MistakeScan::stop(bool completed) {
    running_ = false;
    job_ = 0;
    util::Log::info("mistakes", std::string("scan ") + (completed ? "done" : "stopped") + ": " +
                                    std::to_string(status_.found) + " mistakes found");
    if (auto done = std::move(onDone_)) done(status_, completed);
}

void MistakeScan::nextGame() {
    while (gameIndex_ < games_.size()) {
        const storage::PlayedGame& g = games_[gameIndex_];
        status_.game = static_cast<int>(gameIndex_) + 1;
        auto parsed = parsePgnGame(g.pgn);
        if (!parsed) {
            util::Log::warn("mistakes", "game " + std::to_string(g.id) + ": " + parsed.error().message());
            if (storage::Database* db = app_.userDb()) (void)storage::saveMistakeScan(*db, g.id, {}, "", app_.now());
            ++gameIndex_;
            continue;
        }
        start_ = parsed->tree.positionAt(parsed->tree.root());
        line_.clear();
        for (const GameNode* n = parsed->tree.root()->mainChild(); n; n = n->mainChild()) line_.push_back(n->move);
        user_ = g.human == "black" ? Black : White;
        // The user's positions (after the opponent's first move) and the positions right after them.
        evals_.assign(line_.size() + 1, {});
        todo_.clear();
        std::vector<bool> need(line_.size() + 1, false);
        Position pos = start_;
        for (size_t i = 0; i < line_.size(); ++i) {
            if (pos.sideToMove() == user_ && i > 0) need[i] = need[i + 1] = true;
            pos.makeMove(line_[i]);
        }
        pos = start_;
        for (size_t i = 0; i <= line_.size(); ++i) {
            MoveList legal;
            generateLegalMoves(pos, legal);
            if (need[i] && !legal.empty()) todo_.push_back(i);
            if (i < line_.size()) pos.makeMove(line_[i]);
        }
        todoIndex_ = 0;
        status_.position = 0;
        status_.positions = static_cast<int>(todo_.size());
        nextPosition();
        return;
    }
    stop(true);
}

void MistakeScan::nextPosition() {
    if (todoIndex_ >= todo_.size()) return finishGame();
    status_.position = static_cast<int>(todoIndex_) + 1;
    if (onProgress_) onProgress_(status_);
    const size_t ply = todo_[todoIndex_];
    std::vector<Move> moves(line_.begin(), line_.begin() + static_cast<std::ptrdiff_t>(ply));
    std::weak_ptr<char> alive = alive_;
    job_ = app_.engines().hint(start_, std::move(moves), [this, alive, ply](const engine::JobResult& r) {
        if (alive.expired()) return;
        job_ = 0;
        if (r.cancelled || !r.ok) {
            if (!r.ok && !r.cancelled) util::Log::warn("mistakes", "engine: " + r.error);
            return stop(false);
        }
        evals_[ply].score = r.score;
        evals_[ply].best = r.best;
        engineId_ = r.engineId;
        ++todoIndex_;
        nextPosition();
    });
}

void MistakeScan::finishGame() {
    const storage::PlayedGame& g = games_[gameIndex_];
    std::vector<storage::StoredMistake> stored;
    for (const puzzles::Mistake& m : puzzles::findMistakes(start_, line_, user_, evals_)) {
        storage::StoredMistake s;
        s.gameId = g.id;
        s.ply = m.ply;
        s.fen = m.fen;
        for (Move mv : m.moves) s.moves += (s.moves.empty() ? "" : " ") + toUci(mv);
        s.played = toUci(m.played);
        s.before = m.before;
        s.after = m.after;
        stored.push_back(std::move(s));
    }
    if (storage::Database* db = app_.userDb())
        if (auto r = storage::saveMistakeScan(*db, g.id, stored, engineId_, app_.now()); !r)
            util::Log::error("mistakes", "save: " + r.error().message());
    status_.found += static_cast<int>(stored.size());
    util::Log::info("mistakes", "game " + std::to_string(g.id) + ": " + std::to_string(stored.size()) + " mistakes in " +
                                    std::to_string(todo_.size()) + " positions");
    ++gameIndex_;
    nextGame();
}

} // namespace pchess::app
