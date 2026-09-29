// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test doubles for the engine subsystem (architecture doc §8.5): a scripted UCI engine (FakeUci)
// used both in-process (FakeProcess) and as a real child process (`--fake-engine`), and a manual
// scheduler with fake time.

#include "core/chess/Position.h"
#include "core/engine/Process.h"
#include "core/engine/UciEngine.h"
#include "core/util/Scheduler.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pchess::test {

// Scripted UCI engine: tracks `position`, answers `go` with deterministic legal moves (the
// legal moves sorted by UCI text; multipv k plays the k-th one).
class FakeUci {
public:
    struct Behaviour {
        std::string name = "FakeFish 1.0";
        bool answerUci = true;
        bool answerReady = true;
        bool answerStop = true;                 // false: ignores "stop" (stop timeout tests)
        bool crashOnGo = false;                 // exits with code 3 on "go"
        bool bestmoveOnMovetime = true;         // false: never answers a bounded go (watchdog tests)
        int depth = 3;                          // depth of the final info line
        uint64_t nodes = 4242;                  // nodes of the final info line
        std::vector<std::string> extraOptions;  // raw "option name ..." lines
        std::vector<std::string> extraGoLines;  // raw lines sent after the info lines of every go
    };

    FakeUci() = default;
    explicit FakeUci(Behaviour b) : b_(std::move(b)) {}

    // Output lines for one input line. `exitCode` >= 0 when the engine exits after them.
    std::vector<std::string> handle(const std::string& line, int& exitCode);
    Behaviour& behaviour() { return b_; }
    const chess::Position& position() const { return pos_; }
    int multipv() const { return multipv_; }
    bool searching() const { return searching_; }

private:
    std::vector<std::string> infoAndBest(bool withBest);

    Behaviour b_;
    chess::Position pos_ = chess::Position::startPosition();
    int multipv_ = 1;
    bool searching_ = false;
};

// Manual event loop: posts run in order, timers fire when advance() passes their deadline.
class ManualScheduler final : public util::Scheduler {
public:
    void post(std::function<void()> fn) override;
    TimerId startTimer(std::chrono::milliseconds delay, std::function<void()> fn) override;
    void cancelTimer(TimerId id) override;
    std::chrono::steady_clock::time_point now() const override { return now_; }
    void setBackgroundActivity(bool active) override { backgroundRefs += active ? 1 : -1; }

    int runPending();                            // runs posted functions until none are left; returns how many ran
    void advance(std::chrono::milliseconds ms);  // fires due timers in deadline order, running posts
    size_t timerCount() const { return timers_.size(); }

    int backgroundRefs = 0;

private:
    std::mutex mutex_;
    std::vector<std::function<void()>> posted_;
    struct Entry {
        std::chrono::steady_clock::time_point due;
        std::function<void()> fn;
    };
    std::map<TimerId, Entry> timers_;
    TimerId next_ = 1;
    std::chrono::steady_clock::time_point now_{std::chrono::seconds(1000)};
};

// In-process "child process" running a FakeUci; replies are delivered synchronously from
// writeLine() through the line callback, like a reader thread would.
class FakeProcess final : public engine::Process {
public:
    FakeProcess(FakeUci::Behaviour b, LineCallback onLine, ExitCallback onExit, std::vector<std::string>* log,
                FakeProcess** current = nullptr, int* lastNice = nullptr);
    ~FakeProcess() override;
    void writeLine(std::string_view line) override;
    void terminate(std::chrono::milliseconds grace) override;
    void setLowPriority(int niceLevel) override {
        nice = niceLevel;
        if (lastNice_) *lastNice_ = niceLevel;
    }
    engine::ProcessStats stats() const override;
    bool running() const override { return running_; }
    int pid() const override { return 4242; }

    int nice = 0;
    double cpuSeconds = 0;

private:
    void exit(int code);
    FakeUci uci_;
    LineCallback onLine_;
    ExitCallback onExit_;
    std::vector<std::string>* log_;
    FakeProcess** current_;
    int* lastNice_;
    bool running_ = true;
};

// Factory for UciEngine: every spawn gets a FakeProcess with `behaviour` (tests may change it
// between spawns). All commands of all processes are recorded in `received`.
struct FakeEngineFactory {
    FakeUci::Behaviour behaviour;
    std::vector<std::string> received;
    int spawned = 0;
    int readyFailuresLeft = 0;       // the first N spawns do not answer isready
    FakeProcess* current = nullptr;  // the live fake process (nullptr once it is destroyed)
    int lastNice = 0;                // last priority set on any process

    engine::ProcessFactory factory();
    int count(const std::string& command) const;  // received lines equal to / starting with `command`
};

// A descriptor whose binary is this test executable (exists on disk) for the current platform.
engine::EngineDescriptor fakeDescriptor(const std::string& id = "fakefish",
                                        engine::EnergyClass energy = engine::EnergyClass::High);

} // namespace pchess::test
