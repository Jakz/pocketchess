// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/Engine.h"
#include "core/engine/EngineRegistry.h"
#include "core/engine/PowerProfile.h"
#include "core/engine/UciEngine.h"
#include "core/util/Scheduler.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pchess::engine {

// User choices from Settings -> Engine / Power (persisted from M3). Empty = profile default.
struct EngineSettings {
    PowerProfileId profile = PowerProfileId::Balanced;
    std::array<std::string, RoleCount> engineForRole{};  // engine id per Role
    std::optional<int> threads, hashMb;
    std::optional<SearchLimits> analysisLimit;  // replaces the profile's Deep auto-stop
};

enum class JobKind : uint8_t { Analysis, BestMove, Hint, Validate };
const char* toString(JobKind k);
enum class AnalysisMode : uint8_t { Quick, Deep };

using JobId = uint64_t;

// Resource use of one job (architecture doc §8.3 "Accounting").
struct JobStats {
    double wallMs = 0;
    double cpuSeconds = 0;  // engine CPU during the job (including a cold start)
    uint64_t nodes = 0;
    uint64_t peakRssKb = 0;
    bool coldStart = false;  // the process was started for this job
};

struct JobResult {
    JobId id = 0;
    JobKind kind = JobKind::Analysis;
    bool ok = false;
    bool cancelled = false;  // cancel(), a newer job, or a lifecycle stop
    ErrorCode errorCode = ErrorCode::EngineFailed;
    std::string error;
    std::string engineId;
    chess::Move best;               // BestMove/Hint/Analysis: the engine's move (none if no legal move)
    std::optional<Score> score;     // side to move's view (Validate: the mover of the candidate)
    std::vector<SearchInfo> lines;  // last info per multipv line
    JobStats stats;
};

struct EngineTotals {
    int jobs = 0;
    int failures = 0;
    int starts = 0;
    double cpuSeconds = 0;
    double wallMs = 0;
    uint64_t nodes = 0;
    uint64_t peakRssKb = 0;
};

// Owns the engine processes (architecture doc §8): picks the engine by role and power profile,
// keeps at most one process alive, starts it lazily, shuts it down after the profile's idle
// timeout and on lifecycle events, and accounts every job. Screens only submit jobs.
// One active job: a new job cancels the running one (its callback gets cancelled = true).
class EngineManager {
public:
    using EngineFactory = std::function<std::unique_ptr<IEngine>(const EngineDescriptor&, util::Scheduler&)>;
    using InfoCallback = std::function<void(JobId, const SearchInfo&)>;
    using DoneCallback = std::function<void(const JobResult&)>;

    EngineManager(util::Scheduler& scheduler, EngineRegistry& registry, EngineFactory factory = {});
    ~EngineManager();
    EngineManager(const EngineManager&) = delete;
    EngineManager& operator=(const EngineManager&) = delete;

    void setSettings(const EngineSettings& s);
    const EngineSettings& settings() const { return settings_; }
    const PowerProfile& profile() const { return powerProfile(settings_.profile); }
    EngineRegistry& registry() { return registry_; }

    // The engine a job of this role would use: the user's choice, else the profile's default
    // (Stockfish-class = the highest energy class; Eco: the lightest one, except for Deep analysis,
    // which the user starts explicitly: architecture doc §8.3).
    const EngineDescriptor* engineFor(Role role, AnalysisMode mode = AnalysisMode::Deep) const;
    bool available(Role role) const { return engineFor(role) != nullptr; }

    // --- Jobs (callbacks on the UI thread; `onDone` fires exactly once per job) ---------------
    JobId analyze(const chess::Position& start, std::vector<chess::Move> moves, AnalysisMode mode, int multipv,
                  InfoCallback onInfo, DoneCallback onDone);
    JobId bestMove(const chess::Position& start, std::vector<chess::Move> moves, const std::string& strength,
                   DoneCallback onDone);
    JobId hint(const chess::Position& start, std::vector<chess::Move> moves, DoneCallback onDone);
    // Evaluates `candidate` (legal in start+moves): the score is from the candidate mover's view.
    JobId validate(const chess::Position& start, std::vector<chess::Move> moves, chess::Move candidate,
                   DoneCallback onDone);

    void cancel(JobId id);  // stop; the job still reports (cancelled = true, best move so far)
    void cancelAll();
    bool busy() const { return current_ != 0; }
    JobId activeJob() const { return current_; }

    // Background / Sleep / Exit: cancel and quit the engine now (P11). Others: nothing (lazy).
    enum class Lifecycle { Foreground, Background, Sleep, Wake, Exit };
    void onLifecycle(Lifecycle l);
    void shutdownEngine();  // quits the live process (Settings changes, tests)

    // Diagnostics.
    const IEngine* liveEngine() const { return engine_.get(); }
    bool processRunning() const { return engine_ && engine_->processRunning(); }
    const EngineTotals& totals() const { return totals_; }
    const std::vector<JobResult>& history() const { return history_; }  // last 20 jobs

private:
    struct Running {
        JobKind kind = JobKind::Analysis;
        bool cancelRequested = false;
        bool negateScore = false;
        IEngine* engine = nullptr;
        EngineStats before;
        bool coldStart = false;
        std::chrono::steady_clock::time_point started;
        DoneCallback onDone;
    };
    struct Request {
        JobKind kind;
        Role role;
        chess::Position start;
        std::vector<chess::Move> moves;
        OptionSet options;
        SearchLimits limits;
        bool negateScore = false;
        InfoCallback onInfo;
        DoneCallback onDone;
        AnalysisMode mode = AnalysisMode::Deep;  // Analysis jobs: picks the engine in light profiles
    };

    JobId submit(Request r);
    IEngine* ensureEngine(const EngineDescriptor& d);
    void onSearchDone(JobId id, const SearchResult& r);
    void deliver(DoneCallback onDone, JobResult r);
    void armIdle();
    void record(const JobResult& r);
    OptionSet baseOptions() const;

    util::Scheduler& scheduler_;
    EngineRegistry& registry_;
    EngineFactory factory_;
    EngineSettings settings_;
    std::unique_ptr<IEngine> engine_;
    std::vector<std::unique_ptr<IEngine>> retired_;  // destroyed from a posted task, never inside callbacks
    std::map<JobId, Running> running_;               // the current job, plus superseded ones until they report
    JobId current_ = 0;
    JobId nextId_ = 1;
    util::Timer idle_;
    EngineTotals totals_;
    std::vector<JobResult> history_;
    std::shared_ptr<char> token_;
};

} // namespace pchess::engine
