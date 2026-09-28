// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/Process.h"
#include "core/engine/UciParser.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pchess::engine {

// M0.6 engine spike (Diagnostics -> Engine test): start the engine, `uci` / `isready`, time to
// readyok (NNUE load), RSS after load, `go movetime N` nodes/s and best move, CPU-seconds,
// then quit. Runs asynchronously; all callbacks arrive on the thread that `post` targets.
class UciProbe {
public:
    struct Result {
        bool ok = false;
        std::string error;
        std::string engineName;
        int optionCount = 0;
        double spawnToUciOkMs = 0;
        double readyOkMs = 0;      // isready -> readyok after setoption
        uint64_t rssAfterLoadKb = 0;
        uint64_t nodes = 0;
        uint64_t nps = 0;
        int depth = 0;
        std::string bestMove;
        double searchWallMs = 0;
        double cpuSeconds = 0;
        uint64_t peakRssKb = 0;
    };
    using Post = std::function<void(std::function<void()>)>;
    using Done = std::function<void(const Result&)>;
    using Progress = std::function<void(const std::string& line)>;

    UciProbe(Post post, Done done, Progress progress = {});
    ~UciProbe();

    void start(const std::filesystem::path& exe, int movetimeMs = 1000, int threads = 1, int hashMb = 16,
               const std::vector<std::string>& args = {});
    void abort(const std::string& reason);
    bool running() const { return state_ != State::Idle && state_ != State::Finished; }

private:
    enum class State { Idle, WaitUciOk, WaitReadyOk, Searching, Finished };
    void onLine(const std::string& line);
    void finish();

    Post post_;
    Done done_;
    Progress progress_;
    std::unique_ptr<Process> process_;
    std::shared_ptr<bool> alive_;
    State state_ = State::Idle;
    Result result_;
    int movetimeMs_ = 1000, threads_ = 1, hashMb_ = 16;
    std::chrono::steady_clock::time_point t0_, tReady_, tGo_;
};

} // namespace pchess::engine
