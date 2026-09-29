// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/Engine.h"
#include "core/engine/Process.h"
#include "core/util/Scheduler.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pchess::engine {

using ProcessFactory = std::function<std::unique_ptr<Process>(
    const std::filesystem::path& exe, const std::vector<std::string>& args, Process::LineCallback onLine,
    Process::ExitCallback onExit, std::string* error)>;

// Every waiting step of UciEngine has a timeout (architecture doc §8.5).
struct UciTimeouts {
    std::chrono::milliseconds uciOk{10000};
    std::chrono::milliseconds readyOk{10000};
    std::chrono::milliseconds stop{3000};           // stop -> bestmove
    std::chrono::milliseconds movetimeGrace{5000};  // movetime searches: go -> bestmove watchdog
    std::chrono::milliseconds quitGrace{500};       // quit -> exit before a kill
};

// A UCI engine in a child process (architecture doc §8.5):
//   Off -> Starting (uci/uciok, setoption..., isready/readyok) -> Idle -> Searching -> Stopping -> Idle
// Every waiting step has a timeout. A start that fails (no uciok/readyok, early exit) kills the
// process and retries once, then reports the error (state Failed; the next search tries again).
class UciEngine final : public IEngine {
public:
    using Timeouts = UciTimeouts;

    // `exe` defaults to descriptor.binaryPath(currentPlatformKey()); `factory` to Process::spawn.
    UciEngine(EngineDescriptor descriptor, util::Scheduler& scheduler, std::filesystem::path exe = {},
              ProcessFactory factory = {}, Timeouts timeouts = {});
    ~UciEngine() override;
    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    const EngineDescriptor& descriptor() const override { return descriptor_; }
    const Capabilities& capabilities() const override { return caps_; }
    void configure(const OptionSet& options) override;
    void setPriority(int niceLevel) override;
    void search(const chess::Position& start, std::span<const chess::Move> moves, const SearchLimits& limits,
                InfoCallback onInfo, DoneCallback onDone) override;
    void stop() override;
    void newGame() override { newGame_ = true; }
    void shutdown() override;
    EngineState state() const override;
    bool processRunning() const override;
    EngineStats stats() const override;
    std::string lastError() const override { return lastError_; }

    const std::filesystem::path& executable() const { return exe_; }
    // Options actually sent to the current process (after validation), for Diagnostics/tests.
    const OptionSet& sentOptions() const { return sent_; }

private:
    enum class Phase { Off, WaitUciOk, WaitReadyOk, Idle, Searching, Stopping, Failed };
    struct Request {
        chess::Position start;
        std::vector<chess::Move> moves;
        SearchLimits limits;
        InfoCallback onInfo;
        DoneCallback onDone;
    };
    struct Active {
        Request req;
        chess::Position searched;
        SearchResult result;
        std::chrono::steady_clock::time_point go;
        bool stopRequested = false;
    };

    void startProcess();
    void onLine(const std::string& line);
    void onExit(int code);
    void failStart(const std::string& why);
    void launchPending();
    void sendStop();
    void finishActive(bool stopped, const std::string& error);
    void cancelPending(const std::string& error);
    void killProcess(bool graceful);
    void write(const std::string& line);
    SearchInfo convertInfo(const UciInfo& info, const chess::Position& pos) const;

    EngineDescriptor descriptor_;
    util::Scheduler& scheduler_;
    std::filesystem::path exe_;
    ProcessFactory factory_;
    Timeouts timeouts_;

    std::unique_ptr<Process> process_;
    uint64_t generation_ = 0;      // bumps on every kill: stale lines/exits are ignored
    std::shared_ptr<char> token_;  // lifetime guard for posted callbacks
    Phase phase_ = Phase::Off;
    bool starting_ = false;  // WaitUciOk/WaitReadyOk belong to a process start
    int startAttempts_ = 0;
    bool newGame_ = true;
    int nice_ = 0;
    Capabilities caps_;
    OptionSet wanted_;
    OptionSet sent_;
    std::optional<Request> pending_;
    std::optional<Active> active_;
    util::Timer timer_;
    std::string lastError_;

    std::chrono::steady_clock::time_point spawnTime_{};
    EngineStats stats_;
    double cpuFinished_ = 0;  // CPU-seconds of processes that already exited
    bool background_ = false;
};

} // namespace pchess::engine
