// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/EngineManager.h"

#include "core/chess/MoveGen.h"
#include "core/util/Log.h"

#include <algorithm>
#include <cstdio>

namespace pchess::engine {

using namespace std::chrono;

const char* toString(JobKind k) {
    switch (k) {
        case JobKind::Analysis: return "analysis";
        case JobKind::BestMove: return "bestmove";
        case JobKind::Hint: return "hint";
        case JobKind::Validate: return "validate";
    }
    return "?";
}

EngineManager::EngineManager(util::Scheduler& scheduler, EngineRegistry& registry, EngineFactory factory)
    : scheduler_(scheduler),
      registry_(registry),
      factory_(std::move(factory)),
      idle_(&scheduler),
      token_(std::make_shared<char>('\0')) {
    if (!factory_)
        factory_ = [](const EngineDescriptor& d, util::Scheduler& s) -> std::unique_ptr<IEngine> {
            return std::make_unique<UciEngine>(d, s);
        };
}

EngineManager::~EngineManager() {
    token_.reset();  // results of jobs cut short by the destruction are dropped
    idle_.cancel();
    if (engine_) engine_->shutdown();
    engine_.reset();
    retired_.clear();
}

void EngineManager::setSettings(const EngineSettings& s) {
    const bool engineChange = s.engineForRole != settings_.engineForRole || s.profile != settings_.profile;
    settings_ = s;
    // Takes effect on the next job without a restart; an idle process with the wrong engine goes.
    if (engineChange && engine_ && !busy()) {
        bool stillUsed = false;
        for (Role r : {Role::Play, Role::Analysis, Role::Hint, Role::Validate})
            if (const EngineDescriptor* d = engineFor(r); d && d->id == engine_->descriptor().id) stillUsed = true;
        if (!stillUsed) shutdownEngine();
    }
    if (!busy()) armIdle();
}

const EngineDescriptor* EngineManager::engineFor(Role role) const {
    const std::string& chosen = settings_.engineForRole[static_cast<size_t>(role)];
    if (!chosen.empty()) {
        if (const EngineDescriptor* d = registry_.available(chosen); d && d->supports(role)) return d;
    }
    const bool light = powerProfile(settings_.profile).lightEngineForPlay && role != Role::Analysis;
    if (light) return registry_.lightestFor(role);
    const EngineDescriptor* best = nullptr;
    for (const EngineDescriptor* d : registry_.availableFor(role))
        if (!best || d->resources.energy > best->resources.energy) best = d;
    return best;
}

OptionSet EngineManager::baseOptions() const {
    const PowerProfile& p = profile();
    OptionSet o;
    setOption(o, "Threads", std::to_string(settings_.threads.value_or(p.threads)));
    setOption(o, "Hash", std::to_string(settings_.hashMb.value_or(p.hashMb)));
    return o;
}

// --- Jobs --------------------------------------------------------------------------------------

JobId EngineManager::analyze(const chess::Position& start, std::vector<chess::Move> moves, AnalysisMode mode,
                             int multipv, InfoCallback onInfo, DoneCallback onDone) {
    Request r{JobKind::Analysis, Role::Analysis,   start, std::move(moves), {}, {}, false,
              std::move(onInfo), std::move(onDone)};
    setOption(r.options, "MultiPV", std::to_string(std::clamp(multipv, 1, 8)));
    if (mode == AnalysisMode::Quick)
        r.limits = profile().quickAnalysis;
    else
        r.limits = settings_.analysisLimit.value_or(profile().deepAnalysis);
    return submit(std::move(r));
}

JobId EngineManager::bestMove(const chess::Position& start, std::vector<chess::Move> moves, const std::string& strength,
                              DoneCallback onDone) {
    Request r{JobKind::BestMove, Role::Play, start,   std::move(moves), {},
              profile().play,    false,      nullptr, std::move(onDone)};
    if (const EngineDescriptor* d = engineFor(Role::Play)) {
        const StrengthLevel* level = d->strengthFor(strength);
        if (!level && !strength.empty())
            util::Log::warn("engine", d->id + ": no strength level '" + strength + "', playing at full strength");
        if (level) {
            r.options = level->options;
            if (level->limits.bounded()) r.limits = level->limits;
        }
    }
    return submit(std::move(r));
}

JobId EngineManager::hint(const chess::Position& start, std::vector<chess::Move> moves, DoneCallback onDone) {
    Request r{JobKind::Hint,  Role::Hint, start,   std::move(moves), {},
              profile().hint, false,      nullptr, std::move(onDone)};
    return submit(std::move(r));
}

JobId EngineManager::validate(const chess::Position& start, std::vector<chess::Move> moves, chess::Move candidate,
                              DoneCallback onDone) {
    moves.push_back(candidate);
    Request r{JobKind::Validate,  Role::Validate, start,   std::move(moves), {},
              profile().validate, true,           nullptr, std::move(onDone)};
    return submit(std::move(r));
}

JobId EngineManager::submit(Request r) {
    const JobId id = nextId_++;
    idle_.cancel();
    // One active job: the running one is cancelled (it reports when its search ends).
    if (current_) {
        if (auto it = running_.find(current_); it != running_.end()) it->second.cancelRequested = true;
        current_ = 0;
    }

    const EngineDescriptor* d = engineFor(r.role);
    if (!d) {
        if (engine_ && engine_->state() == EngineState::Searching) engine_->stop();
        JobResult res;
        res.id = id;
        res.kind = r.kind;
        res.errorCode = ErrorCode::EngineMissing;
        res.error = std::string("No engine installed for ") + toString(r.role);
        record(res);
        deliver(std::move(r.onDone), std::move(res));
        return id;
    }
    IEngine* e = ensureEngine(*d);

    OptionSet opts = baseOptions();
    for (const auto& [k, v] : r.options) setOption(opts, k, v);
    e->configure(opts);
    e->setPriority(profile().nice);

    Running run;
    run.kind = r.kind;
    run.negateScore = r.negateScore;
    run.engine = e;
    run.before = e->stats();
    run.coldStart = !e->processRunning();
    run.started = scheduler_.now();
    run.onDone = std::move(r.onDone);
    running_[id] = std::move(run);
    current_ = id;

    std::weak_ptr<char> alive = token_;
    InfoCallback onInfo = std::move(r.onInfo);
    e->search(
        r.start, r.moves, r.limits,
        [this, alive, id, onInfo](const SearchInfo& info) {
            if (alive.expired() || id != current_ || !onInfo) return;
            onInfo(id, info);
        },
        [this, alive, id](const SearchResult& res) {
            if (alive.expired()) return;
            onSearchDone(id, res);
        });
    return id;
}

IEngine* EngineManager::ensureEngine(const EngineDescriptor& d) {
    if (engine_ && engine_->descriptor().id == d.id) return engine_.get();
    if (engine_) {
        util::Log::info("engine", "switching engine " + engine_->descriptor().id + " -> " + d.id);
        engine_->shutdown();  // one process at a time (RAM)
        retired_.push_back(std::move(engine_));
        std::weak_ptr<char> alive = token_;
        scheduler_.post([this, alive] {
            if (!alive.expired()) retired_.clear();
        });
    }
    engine_ = factory_(d, scheduler_);
    return engine_.get();
}

void EngineManager::onSearchDone(JobId id, const SearchResult& r) {
    auto it = running_.find(id);
    if (it == running_.end()) return;
    Running run = std::move(it->second);
    running_.erase(it);

    JobResult res;
    res.id = id;
    res.kind = run.kind;
    res.ok = r.ok;
    res.cancelled = r.stopped || run.cancelRequested;
    res.error = r.error;
    res.errorCode = r.error.rfind("Engine not found", 0) == 0 ? ErrorCode::EngineMissing : ErrorCode::EngineFailed;
    res.engineId = run.engine ? run.engine->descriptor().id : std::string();
    res.best = r.best;
    res.lines = r.lines;
    if (!r.lines.empty() && r.lines[0].score) {
        Score s = *r.lines[0].score;
        if (run.negateScore) {
            s.value = -s.value;
            std::swap(s.lowerbound, s.upperbound);
        }
        res.score = s;
    }
    const bool engineAlive =
        run.engine && (run.engine == engine_.get() || std::any_of(retired_.begin(), retired_.end(), [&](const auto& e) {
                           return e.get() == run.engine;
                       }));
    const EngineStats after = engineAlive ? run.engine->stats() : run.before;
    res.stats.cpuSeconds = std::max(0.0, after.cpuSeconds - run.before.cpuSeconds);
    res.stats.wallMs = duration<double, std::milli>(scheduler_.now() - run.started).count();
    for (const auto& l : r.lines) res.stats.nodes = std::max(res.stats.nodes, l.nodes);
    res.stats.peakRssKb = after.peakRssKb;
    res.stats.coldStart = run.coldStart;
    if (run.coldStart && after.starts > run.before.starts) totals_.starts += after.starts - run.before.starts;
    record(res);

    if (current_ == id) {
        current_ = 0;
        armIdle();
    }
    deliver(std::move(run.onDone), std::move(res));
}

void EngineManager::deliver(DoneCallback onDone, JobResult r) {
    if (!onDone) return;
    std::weak_ptr<char> alive = token_;
    scheduler_.post([alive, onDone = std::move(onDone), r = std::move(r)] {
        if (!alive.expired()) onDone(r);
    });
}

void EngineManager::record(const JobResult& r) {
    ++totals_.jobs;
    if (!r.ok && !r.cancelled) ++totals_.failures;
    totals_.cpuSeconds += r.stats.cpuSeconds;
    totals_.wallMs += r.stats.wallMs;
    totals_.nodes += r.stats.nodes;
    totals_.peakRssKb = std::max(totals_.peakRssKb, r.stats.peakRssKb);
    history_.push_back(r);
    if (history_.size() > 20) history_.erase(history_.begin());

    char buf[256];
    std::snprintf(buf, sizeof buf, "job %s %s: %s%s, %.0f ms wall, %.2f CPU-s, %llu nodes, peak RSS %llu MiB%s",
                  toString(r.kind), r.engineId.empty() ? "-" : r.engineId.c_str(),
                  r.ok ? "ok" : (r.cancelled ? "cancelled" : "failed"), r.cancelled && r.ok ? " (stopped)" : "",
                  r.stats.wallMs, r.stats.cpuSeconds, static_cast<unsigned long long>(r.stats.nodes),
                  static_cast<unsigned long long>(r.stats.peakRssKb / 1024), r.stats.coldStart ? ", cold start" : "");
    util::Log::info("engine", buf + (r.error.empty() ? std::string() : " - " + r.error));
}

void EngineManager::armIdle() {
    if (!engine_ || !engine_->processRunning()) return;
    const milliseconds t = profile().idleTimeout;
    auto quitIfIdle = [this] {
        if (!busy() && engine_ && engine_->processRunning()) {
            util::Log::info("engine", engine_->descriptor().id + ": idle timeout");
            engine_->shutdown();
        }
    };
    if (t.count() <= 0) {
        std::weak_ptr<char> alive = token_;
        scheduler_.post([alive, quitIfIdle] {
            if (!alive.expired()) quitIfIdle();
        });
    } else {
        idle_.start(t, quitIfIdle);
    }
}

// --- Control -----------------------------------------------------------------------------------

void EngineManager::cancel(JobId id) {
    auto it = running_.find(id);
    if (it == running_.end()) return;
    it->second.cancelRequested = true;
    if (id == current_ && engine_) engine_->stop();
}

void EngineManager::cancelAll() {
    for (auto& [id, run] : running_) run.cancelRequested = true;
    if (current_ && engine_) engine_->stop();
}

void EngineManager::shutdownEngine() {
    for (auto& [id, run] : running_) run.cancelRequested = true;
    if (engine_) engine_->shutdown();
    idle_.cancel();  // after: the cut-short job's completion may have armed it
}

void EngineManager::onLifecycle(Lifecycle l) {
    if (l == Lifecycle::Background || l == Lifecycle::Sleep || l == Lifecycle::Exit) {
        if (engine_ && engine_->processRunning())
            util::Log::info("engine", std::string("lifecycle: stopping the engine (") +
                                          (l == Lifecycle::Exit    ? "exit"
                                           : l == Lifecycle::Sleep ? "sleep"
                                                                   : "background") +
                                          ")");
        shutdownEngine();
    }
}

} // namespace pchess::engine
