// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/UciEngine.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/util/Log.h"

#include <algorithm>
#include <cstddef>
#include <system_error>

namespace pchess::engine {

using namespace std::chrono;

const char* toString(EngineState s) {
    switch (s) {
        case EngineState::Off: return "off";
        case EngineState::Starting: return "starting";
        case EngineState::Idle: return "idle";
        case EngineState::Searching: return "searching";
        case EngineState::Stopping: return "stopping";
        case EngineState::Failed: return "failed";
    }
    return "?";
}

UciEngine::UciEngine(EngineDescriptor descriptor, util::Scheduler& scheduler, std::filesystem::path exe,
                     ProcessFactory factory, Timeouts timeouts)
    : descriptor_(std::move(descriptor)),
      scheduler_(scheduler),
      exe_(std::move(exe)),
      factory_(std::move(factory)),
      timeouts_(timeouts),
      token_(std::make_shared<char>('\0')),
      timer_(&scheduler) {
    if (exe_.empty()) exe_ = descriptor_.binaryPath(currentPlatformKey());
    if (!factory_) factory_ = &Process::spawn;
    wanted_ = descriptor_.defaults;
}

UciEngine::~UciEngine() {
    token_.reset();
    timer_.cancel();
    killProcess(true);
}

void UciEngine::configure(const OptionSet& options) {
    wanted_ = descriptor_.defaults;
    for (const auto& [k, v] : options) setOption(wanted_, k, v);
}

void UciEngine::setPriority(int niceLevel) {
    nice_ = niceLevel;
    if (process_ && nice_ > 0) process_->setLowPriority(nice_);
}

EngineState UciEngine::state() const {
    switch (phase_) {
        case Phase::Off: return EngineState::Off;
        case Phase::WaitUciOk: return EngineState::Starting;
        case Phase::WaitReadyOk: return starting_ ? EngineState::Starting : EngineState::Idle;
        case Phase::Idle: return EngineState::Idle;
        case Phase::Searching: return EngineState::Searching;
        case Phase::Stopping: return EngineState::Stopping;
        case Phase::Failed: return EngineState::Failed;
    }
    return EngineState::Off;
}

bool UciEngine::processRunning() const {
    return process_ && process_->running();
}

EngineStats UciEngine::stats() const {
    EngineStats s = stats_;
    s.cpuSeconds = cpuFinished_;
    if (process_) {
        const ProcessStats p = process_->stats();
        if (p.valid) {
            s.cpuSeconds += p.cpuSeconds;
            s.rssKb = p.rssKb;
            s.peakRssKb = std::max(s.peakRssKb, p.peakRssKb);
        }
    } else {
        s.rssKb = 0;
    }
    return s;
}

void UciEngine::write(const std::string& line) {
    if (process_) process_->writeLine(line);
}

// --- Process start / stop ----------------------------------------------------------------------

void UciEngine::startProcess() {
    killProcess(false);
    std::error_code ec;
    if (exe_.empty() || !std::filesystem::exists(exe_, ec)) {
        lastError_ = "Engine not found. Expected: " + (exe_.empty() ? descriptor_.id : exe_.string());
        phase_ = Phase::Failed;
        util::Log::warn("engine", descriptor_.id + ": " + lastError_);
        cancelPending(lastError_);
        return;
    }
    ++startAttempts_;
    const uint64_t gen = generation_;
    std::weak_ptr<char> alive = token_;
    util::Scheduler* sched = &scheduler_;
    std::string err;
    spawnTime_ = scheduler_.now();
    process_ = factory_(
        exe_, descriptor_.args,
        [this, alive, sched, gen](std::string_view line) {
            sched->post([this, alive, gen, copy = std::string(line)] {
                if (alive.expired() || gen != generation_) return;
                onLine(copy);
            });
        },
        [this, alive, sched, gen](int code) {
            sched->post([this, alive, gen, code] {
                if (alive.expired() || gen != generation_) return;
                onExit(code);
            });
        },
        &err);
    if (!process_) {
        failStart("cannot start " + exe_.filename().string() + ": " + err);
        return;
    }
    ++stats_.starts;
    if (!background_) {
        scheduler_.setBackgroundActivity(true);
        background_ = true;
    }
    if (nice_ > 0) process_->setLowPriority(nice_);
    util::Log::info("engine",
                    descriptor_.id + ": started " + exe_.string() + " pid " + std::to_string(process_->pid()));
    caps_ = {};
    sent_.clear();
    newGame_ = true;
    starting_ = true;
    phase_ = Phase::WaitUciOk;
    write("uci");
    timer_.start(timeouts_.uciOk,
                 [this] { failStart("no 'uciok' within " + std::to_string(timeouts_.uciOk.count()) + " ms"); });
}

void UciEngine::killProcess(bool graceful) {
    timer_.cancel();
    if (process_) {
        if (graceful) write("quit");
        process_->terminate(graceful ? timeouts_.quitGrace : milliseconds(0));
        const ProcessStats s = process_->stats();
        if (s.valid) {
            cpuFinished_ += s.cpuSeconds;
            stats_.peakRssKb = std::max(stats_.peakRssKb, s.peakRssKb);
        }
        process_.reset();
    }
    ++generation_;
    if (background_) {
        scheduler_.setBackgroundActivity(false);
        background_ = false;
    }
    starting_ = false;
    sent_.clear();
    if (phase_ != Phase::Failed) phase_ = Phase::Off;
}

void UciEngine::failStart(const std::string& why) {
    util::Log::warn("engine", descriptor_.id + ": " + why);
    lastError_ = why;
    // A search in progress cannot survive a dead process.
    if (active_) {
        std::weak_ptr<char> alive = token_;
        finishActive(false, why);
        if (alive.expired()) return;
    }
    killProcess(false);
    if (startAttempts_ < 2 && pending_) {
        util::Log::info("engine", descriptor_.id + ": restarting once");
        startProcess();
        return;
    }
    phase_ = Phase::Failed;
    startAttempts_ = 0;
    cancelPending(why);
}

void UciEngine::shutdown() {
    std::weak_ptr<char> alive = token_;
    if (active_) {
        if (phase_ == Phase::Searching) write("stop");
        finishActive(true, {});
        if (alive.expired()) return;
    }
    cancelPending({});
    if (alive.expired()) return;
    if (process_) util::Log::info("engine", descriptor_.id + ": shut down");
    killProcess(true);
    phase_ = Phase::Off;
    startAttempts_ = 0;
}

// --- Searching ---------------------------------------------------------------------------------

void UciEngine::search(const chess::Position& start, std::span<const chess::Move> moves, const SearchLimits& limits,
                       InfoCallback onInfo, DoneCallback onDone) {
    std::weak_ptr<char> alive = token_;
    cancelPending({});  // a queued, not yet started request is superseded
    if (alive.expired()) return;
    pending_ = Request{start, std::vector<chess::Move>(moves.begin(), moves.end()), limits, std::move(onInfo),
                       std::move(onDone)};
    switch (phase_) {
        case Phase::Off:
        case Phase::Failed:
            startAttempts_ = 0;
            startProcess();
            break;
        case Phase::Idle: launchPending(); break;
        case Phase::Searching:
            if (active_) active_->stopRequested = true;
            sendStop();
            break;
        case Phase::WaitUciOk:
        case Phase::WaitReadyOk:
        case Phase::Stopping: break;  // runs when the engine is ready / the old search ended
    }
}

void UciEngine::stop() {
    std::weak_ptr<char> alive = token_;
    cancelPending({});
    if (alive.expired()) return;
    if (phase_ == Phase::Searching && active_) {
        active_->stopRequested = true;
        sendStop();
    }
}

void UciEngine::sendStop() {
    write("stop");
    phase_ = Phase::Stopping;
    timer_.start(timeouts_.stop, [this] {
        const std::string why = "engine did not stop within " + std::to_string(timeouts_.stop.count()) + " ms";
        util::Log::warn("engine", descriptor_.id + ": " + why);
        lastError_ = why;
        std::weak_ptr<char> alive = token_;
        finishActive(false, why);
        if (alive.expired()) return;
        killProcess(false);
        if (pending_) startProcess();
    });
}

void UciEngine::launchPending() {
    if (!pending_ || phase_ != Phase::Idle) return;
    // Options and ucinewgame first, confirmed by isready.
    std::vector<std::string> warnings;
    const OptionSet valid = validateOptions(wanted_, caps_, &warnings);
    std::vector<std::string> commands;
    for (const auto& [k, v] : valid) {
        const std::string* old = findOption(sent_, k);
        if (old && *old == v) continue;
        commands.push_back(v.empty() ? "setoption name " + k : "setoption name " + k + " value " + v);
        setOption(sent_, k, v);
    }
    for (const auto& w : warnings) util::Log::debug("engine", descriptor_.id + ": " + w);
    // Options sent earlier but no longer wanted go back to the engine's default (e.g. the
    // Skill Level of a play job before an analysis job).
    for (size_t i = 0; i < sent_.size();) {
        const std::string name = sent_[i].first;
        if (findOption(valid, name)) {
            ++i;
            continue;
        }
        const UciOption* o = caps_.find(name);
        if (o && o->type != "button" && o->defaultValue != sent_[i].second)
            commands.push_back("setoption name " + o->name + " value " + o->defaultValue);
        sent_.erase(sent_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (newGame_) {
        commands.emplace_back("ucinewgame");
        newGame_ = false;
    }
    if (!commands.empty()) {
        for (const auto& c : commands) write(c);
        write("isready");
        phase_ = Phase::WaitReadyOk;
        starting_ = false;
        timer_.start(timeouts_.readyOk,
                     [this] { failStart("no 'readyok' within " + std::to_string(timeouts_.readyOk.count()) + " ms"); });
        return;
    }

    Active a;
    a.req = std::move(*pending_);
    pending_.reset();
    a.searched = a.req.start;
    for (const chess::Move m : a.req.moves) {
        if (!chess::legalMoves(a.searched).contains(m)) {
            std::weak_ptr<char> alive = token_;
            SearchResult r;
            r.error = "illegal move in the search request";
            if (a.req.onDone) a.req.onDone(r);
            if (alive.expired()) return;
            launchPending();
            return;
        }
        a.searched.makeMove(m);
    }
    std::string pos;
    const std::string fen = chess::toFen(a.req.start);
    pos = fen == chess::toFen(chess::Position::startPosition()) ? "position startpos" : "position fen " + fen;
    if (!a.req.moves.empty()) {
        pos += " moves";
        for (const chess::Move m : a.req.moves) pos += " " + chess::toUci(m);
    }
    write(pos);
    write(a.req.limits.goCommand());
    a.go = scheduler_.now();
    const std::optional<int> movetime = a.req.limits.movetimeMs;
    active_ = std::move(a);
    phase_ = Phase::Searching;
    if (movetime) {
        timer_.start(milliseconds(*movetime) + timeouts_.movetimeGrace, [this] {
            util::Log::warn("engine", descriptor_.id + ": no bestmove after movetime, sending stop");
            sendStop();
        });
    } else {
        timer_.cancel();
    }
}

void UciEngine::finishActive(bool stopped, const std::string& error) {
    if (!active_) return;
    Active a = std::move(*active_);
    active_.reset();
    a.result.ok = error.empty() && !(stopped && a.result.best.isNone() && a.result.lines.empty());
    a.result.stopped = stopped || a.stopRequested;
    a.result.error = error;
    a.result.wallMs = duration<double, std::milli>(scheduler_.now() - a.go).count();
    if (phase_ == Phase::Searching || phase_ == Phase::Stopping) phase_ = Phase::Idle;
    if (a.req.onDone) a.req.onDone(a.result);
}

void UciEngine::cancelPending(const std::string& error) {
    if (!pending_) return;
    Request r = std::move(*pending_);
    pending_.reset();
    SearchResult res;
    res.stopped = error.empty();
    res.error = error;
    if (r.onDone) r.onDone(res);
}

// --- Engine output -----------------------------------------------------------------------------

SearchInfo UciEngine::convertInfo(const UciInfo& info, const chess::Position& pos) const {
    SearchInfo s;
    s.depth = info.depth.value_or(0);
    s.seldepth = info.seldepth.value_or(0);
    s.multipv = std::max(1, info.multipv.value_or(1));
    s.score = info.score;
    s.nodes = info.nodes.value_or(0);
    s.nps = info.nps.value_or(0);
    s.timeMs = static_cast<int>(info.timeMs.value_or(0));
    s.hashfull = info.hashfull.value_or(0);
    chess::Position p = pos;
    for (const auto& uci : info.pv) {
        auto m = chess::parseUci(p, uci);
        if (!m) break;  // stop at the first move that is not legal here
        s.pv.push_back(*m);
        p.makeMove(*m);
    }
    return s;
}

void UciEngine::onLine(const std::string& line) {
    const UciMessage m = parseUciLine(line);
    switch (phase_) {
        case Phase::WaitUciOk:
            if (m.type == UciMessage::Type::IdName)
                caps_.name = m.text;
            else if (m.type == UciMessage::Type::IdAuthor)
                caps_.author = m.text;
            else if (m.type == UciMessage::Type::Option)
                caps_.options.push_back(m.option);
            else if (m.type == UciMessage::Type::UciOk) {
                timer_.cancel();
                std::vector<std::string> warnings;
                const OptionSet valid = validateOptions(wanted_, caps_, &warnings);
                for (const auto& w : warnings) util::Log::warn("engine", descriptor_.id + ": " + w);
                for (const auto& [k, v] : valid) {
                    write(v.empty() ? "setoption name " + k : "setoption name " + k + " value " + v);
                    setOption(sent_, k, v);
                }
                write("isready");
                phase_ = Phase::WaitReadyOk;
                timer_.start(timeouts_.readyOk, [this] {
                    failStart("no 'readyok' within " + std::to_string(timeouts_.readyOk.count()) + " ms");
                });
            }
            break;
        case Phase::WaitReadyOk:
            if (m.type == UciMessage::Type::ReadyOk) {
                timer_.cancel();
                if (starting_) {
                    stats_.startup = duration_cast<milliseconds>(scheduler_.now() - spawnTime_);
                    const ProcessStats ps = process_ ? process_->stats() : ProcessStats{};
                    util::Log::info("engine", descriptor_.id + ": ready (" + caps_.name + ") in " +
                                                  std::to_string(stats_.startup.count()) + " ms, " +
                                                  std::to_string(caps_.options.size()) + " options, RSS " +
                                                  std::to_string(ps.rssKb / 1024) + " MiB");
                    starting_ = false;
                    startAttempts_ = 0;
                    lastError_.clear();
                }
                phase_ = Phase::Idle;
                launchPending();
            }
            break;
        case Phase::Searching:
        case Phase::Stopping:
            if (!active_) break;
            if (m.type == UciMessage::Type::Info) {
                if (!m.info.score && m.info.pv.empty()) break;  // currmove, string, hashfull-only lines
                SearchInfo si = convertInfo(m.info, active_->searched);
                const size_t idx = static_cast<size_t>(si.multipv - 1);
                if (idx < 64) {
                    if (active_->result.lines.size() <= idx) active_->result.lines.resize(idx + 1);
                    active_->result.lines[idx] = si;
                }
                if (phase_ == Phase::Searching && active_->req.onInfo) active_->req.onInfo(si);
            } else if (m.type == UciMessage::Type::BestMove) {
                timer_.cancel();
                if (m.text != "(none)" && m.text != "0000") {
                    auto bm = chess::parseUci(active_->searched, m.text);
                    if (bm) {
                        active_->result.best = *bm;
                        chess::Position after = active_->searched;
                        after.makeMove(*bm);
                        if (!m.ponder.empty())
                            if (auto pm = chess::parseUci(after, m.ponder)) active_->result.ponder = *pm;
                    } else {
                        util::Log::warn("engine", descriptor_.id + ": illegal bestmove '" + m.text + "'");
                    }
                }
                std::weak_ptr<char> alive = token_;
                finishActive(active_->stopRequested, {});
                if (alive.expired()) return;
                launchPending();
            }
            break;
        case Phase::Off:
        case Phase::Idle:
        case Phase::Failed: break;  // stray output (late bestmove after a kill, info string ...)
    }
}

void UciEngine::onExit(int code) {
    const std::string why = "engine exited unexpectedly (code " + std::to_string(code) + ")";
    if (phase_ == Phase::WaitUciOk || (phase_ == Phase::WaitReadyOk && starting_)) {
        failStart(why);
        return;
    }
    util::Log::warn("engine", descriptor_.id + ": " + why);
    lastError_ = why;
    std::weak_ptr<char> alive = token_;
    finishActive(false, why);
    if (alive.expired()) return;
    killProcess(false);
    if (pending_) startProcess();  // the next request restarts the engine
}

} // namespace pchess::engine
