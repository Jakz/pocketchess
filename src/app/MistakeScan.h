// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Position.h"
#include "core/engine/EngineManager.h"
#include "core/puzzles/MistakeFinder.h"
#include "core/storage/ChessDb.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pchess::app {

class Application;

// Checks the finished Play games that no scan has seen (M7, My mistakes): one Hint job per needed
// position, in order, then findMistakes() and storage::saveMistakeScan(). Cancelling (or another
// screen's engine job, which supersedes ours) stops it; a game is marked only once fully checked,
// so the next scan starts again with the first unchecked game.
class MistakeScan {
public:
    struct Status {
        int game = 0, games = 0;  // 1-based game being checked, of how many
        int position = 0, positions = 0;
        int found = 0;            // mistakes stored so far in this scan
    };
    using ProgressFn = std::function<void(const Status&)>;
    using DoneFn = std::function<void(const Status&, bool completed)>;

    explicit MistakeScan(Application& app);
    ~MistakeScan();
    MistakeScan(const MistakeScan&) = delete;
    MistakeScan& operator=(const MistakeScan&) = delete;

    void start(ProgressFn onProgress, DoneFn onDone);
    void cancel();
    bool running() const { return running_; }

private:
    void nextGame();
    void nextPosition();
    void finishGame();
    void stop(bool completed);

    Application& app_;
    std::vector<storage::PlayedGame> games_;
    size_t gameIndex_ = 0;
    chess::Position start_;
    std::vector<chess::Move> line_;
    chess::Color user_ = chess::White;
    std::vector<puzzles::PlyEval> evals_;
    std::vector<size_t> todo_;  // plies whose position needs an evaluation
    size_t todoIndex_ = 0;
    std::string engineId_;
    engine::JobId job_ = 0;
    bool running_ = false;
    Status status_;
    ProgressFn onProgress_;
    DoneFn onDone_;
    std::shared_ptr<char> alive_ = std::make_shared<char>('\0');
};

} // namespace pchess::app
