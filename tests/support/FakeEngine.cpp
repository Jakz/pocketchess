// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/FakeEngine.h"

#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "support/Test.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace pchess::test {

// --- FakeUci -----------------------------------------------------------------------------------

std::vector<std::string> FakeUci::infoAndBest(bool withBest) {
    std::vector<std::string> out;
    const chess::MoveList legal = chess::legalMoves(pos_);
    std::vector<std::string> ucis;
    for (const chess::Move m : legal) ucis.push_back(chess::toUci(m));
    std::sort(ucis.begin(), ucis.end());
    if (ucis.empty()) {
        const bool mated = pos_.inCheck();
        out.push_back(mated ? "info depth 0 score mate 0" : "info depth 0 score cp 0");
        if (withBest) out.emplace_back("bestmove (none)");
        return out;
    }
    out.push_back("info depth 1 score cp 10 nodes 20 pv " + ucis[0]);
    const int lines = std::min<int>(multipv_, static_cast<int>(ucis.size()));
    for (int k = 1; k <= lines; ++k) {
        std::string pv = ucis[static_cast<size_t>(k - 1)];
        chess::Position after = pos_;
        after.makeMove(chess::parseUci(pos_, pv).value());
        std::vector<std::string> replies;
        for (const chess::Move m : chess::legalMoves(after)) replies.push_back(chess::toUci(m));
        std::sort(replies.begin(), replies.end());
        if (!replies.empty()) pv += " " + replies[0];
        std::ostringstream os;
        const int cp = k == 1 && b_.evalCp ? b_.evalCp(pos_) : 30 - 10 * k;
        os << "info depth " << b_.depth << " seldepth " << b_.depth + 1 << " multipv " << k << " score cp " << cp
           << " nodes " << b_.nodes << " nps 84000 time 50 pv " << pv;
        out.push_back(os.str());
    }
    if (withBest) {
        std::string best = "bestmove " + ucis[0];
        chess::Position after = pos_;
        after.makeMove(chess::parseUci(pos_, ucis[0]).value());
        std::vector<std::string> replies;
        for (const chess::Move m : chess::legalMoves(after)) replies.push_back(chess::toUci(m));
        std::sort(replies.begin(), replies.end());
        if (!replies.empty()) best += " ponder " + replies[0];
        out.push_back(best);
    }
    return out;
}

std::vector<std::string> FakeUci::handle(const std::string& raw, int& exitCode) {
    exitCode = -1;
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    std::vector<std::string> out;
    if (line == "uci") {
        if (!b_.answerUci) return out;
        out.push_back("id name " + b_.name);
        out.emplace_back("id author PocketChess tests");
        out.emplace_back("option name Hash type spin default 16 min 1 max 1024");
        out.emplace_back("option name Skill Level type spin default 20 min 0 max 20");
        for (const auto& o : b_.extraOptions) out.push_back(o);
        out.emplace_back("uciok");
    } else if (line == "isready") {
        if (b_.answerReady) out.emplace_back("readyok");
    } else if (line.rfind("setoption name MultiPV value ", 0) == 0) {
        multipv_ = std::max(1, std::atoi(line.c_str() + 29));
    } else if (line.rfind("position ", 0) == 0) {
        std::istringstream is(line.substr(9));
        std::string tok;
        is >> tok;
        if (tok == "startpos") {
            pos_ = chess::Position::startPosition();
        } else if (tok == "fen") {
            std::string fen, part;
            for (int i = 0; i < 6 && is >> part; ++i) fen += (i ? " " : "") + part;
            auto p = chess::parseFen(fen);
            if (p) pos_ = *p;
        }
        if (is >> tok && tok == "moves") {
            std::string mv;
            while (is >> mv) {
                auto m = chess::parseUci(pos_, mv);
                if (!m) break;
                pos_.makeMove(*m);
            }
        }
    } else if (line.rfind("go", 0) == 0) {
        if (b_.crashOnGo) {
            exitCode = 3;
            return out;
        }
        const bool wait = line.find("infinite") != std::string::npos || !b_.bestmoveOnMovetime;
        searching_ = wait;
        out = infoAndBest(!wait);
        out.insert(wait ? out.end() : out.end() - 1, b_.extraGoLines.begin(), b_.extraGoLines.end());
        return out;
    } else if (line == "stop") {
        if (searching_ && b_.answerStop) {
            searching_ = false;
            const auto lines = infoAndBest(true);
            out.push_back(lines.back());
        }
    } else if (line == "quit") {
        exitCode = 0;
    }
    return out;
}

// --- ManualScheduler ---------------------------------------------------------------------------

void ManualScheduler::post(std::function<void()> fn) {
    std::lock_guard lock(mutex_);
    posted_.push_back(std::move(fn));
}

util::Scheduler::TimerId ManualScheduler::startTimer(std::chrono::milliseconds delay, std::function<void()> fn) {
    const TimerId id = next_++;
    timers_[id] = Entry{now_ + delay, std::move(fn)};
    return id;
}

void ManualScheduler::cancelTimer(TimerId id) {
    timers_.erase(id);
}

int ManualScheduler::runPending() {
    int ran = 0;
    for (;;) {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard lock(mutex_);
            batch.swap(posted_);
        }
        if (batch.empty()) return ran;
        for (auto& f : batch) {
            f();
            ++ran;
        }
    }
}

void ManualScheduler::advance(std::chrono::milliseconds ms) {
    const auto end = now_ + ms;
    runPending();
    for (;;) {
        auto it = std::min_element(timers_.begin(), timers_.end(),
                                   [](const auto& a, const auto& b) { return a.second.due < b.second.due; });
        if (it == timers_.end() || it->second.due > end) break;
        now_ = std::max(now_, it->second.due);
        auto fn = std::move(it->second.fn);
        timers_.erase(it);
        fn();
        runPending();
    }
    now_ = end;
    runPending();
}

// --- FakeProcess -------------------------------------------------------------------------------

FakeProcess::FakeProcess(FakeUci::Behaviour b, LineCallback onLine, ExitCallback onExit, std::vector<std::string>* log,
                         FakeProcess** current, int* lastNice)
    : uci_(std::move(b)),
      onLine_(std::move(onLine)),
      onExit_(std::move(onExit)),
      log_(log),
      current_(current),
      lastNice_(lastNice) {}

FakeProcess::~FakeProcess() {
    terminate(std::chrono::milliseconds(0));
    if (current_ && *current_ == this) *current_ = nullptr;
}

void FakeProcess::writeLine(std::string_view line) {
    if (!running_) return;
    if (log_) log_->emplace_back(line);
    int code = -1;
    const std::string cmd(line);
    if (cmd.rfind("go", 0) == 0) cpuSeconds += 0.25;
    for (const auto& out : uci_.handle(cmd, code))
        if (onLine_) onLine_(out);
    if (code >= 0) exit(code);
}

void FakeProcess::terminate(std::chrono::milliseconds) {
    if (running_) exit(137);
}

void FakeProcess::exit(int code) {
    running_ = false;
    if (onExit_) onExit_(code);
}

engine::ProcessStats FakeProcess::stats() const {
    engine::ProcessStats s;
    s.valid = true;
    s.cpuSeconds = cpuSeconds;
    s.rssKb = 90 * 1024;
    s.peakRssKb = 95 * 1024;
    return s;
}

// --- FakeEngineFactory -------------------------------------------------------------------------

engine::ProcessFactory FakeEngineFactory::factory() {
    return [this](const std::filesystem::path&, const std::vector<std::string>&, engine::Process::LineCallback onLine,
                  engine::Process::ExitCallback onExit, std::string*) -> std::unique_ptr<engine::Process> {
        ++spawned;
        FakeUci::Behaviour b = behaviour;
        if (readyFailuresLeft > 0) {
            --readyFailuresLeft;
            b.answerReady = false;
        }
        auto p = std::make_unique<FakeProcess>(b, std::move(onLine), std::move(onExit), &received, &current, &lastNice);
        current = p.get();
        return p;
    };
}

int FakeEngineFactory::count(const std::string& command) const {
    return static_cast<int>(std::count_if(received.begin(), received.end(),
                                          [&](const std::string& l) { return l.rfind(command, 0) == 0; }));
}

engine::EngineDescriptor fakeDescriptor(const std::string& id, engine::EnergyClass energy) {
    engine::EngineDescriptor d;
    d.id = id;
    d.name = "Fake " + id;
    d.binaries[engine::currentPlatformKey()] = selfExe();
    d.args = {"--fake-engine"};
    d.roles = {engine::Role::Play, engine::Role::Analysis, engine::Role::Hint, engine::Role::Validate};
    d.resources.energy = energy;
    d.defaults = {{"Threads", "1"}, {"Hash", "16"}};
    engine::StrengthLevel beginner;
    beginner.label = "beginner";
    beginner.options = {{"Skill Level", "0"}};
    beginner.limits.movetimeMs = 80;
    d.strength.push_back(beginner);
    engine::StrengthLevel maximum;
    maximum.label = "maximum";
    d.strength.push_back(maximum);
    return d;
}

} // namespace pchess::test
