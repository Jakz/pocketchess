// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/UciProbe.h"

#include "core/util/Log.h"

namespace pchess::engine {

using Clock = std::chrono::steady_clock;

namespace {
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
} // namespace

UciProbe::UciProbe(Post post, Done done, Progress progress)
    : post_(std::move(post)), done_(std::move(done)), progress_(std::move(progress)), alive_(std::make_shared<bool>(true)) {}

UciProbe::~UciProbe() {
    *alive_ = false;
    if (process_) process_->terminate(std::chrono::milliseconds(500));
}

void UciProbe::start(const std::filesystem::path& exe, int movetimeMs, int threads, int hashMb,
                     const std::vector<std::string>& args) {
    movetimeMs_ = movetimeMs;
    threads_ = threads;
    hashMb_ = hashMb;
    result_ = {};
    std::error_code ec;
    if (!std::filesystem::exists(exe, ec)) {
        result_.error = "Engine not found. Expected: " + exe.string();
        state_ = State::Finished;
        if (done_) done_(result_);
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    auto post = post_;
    std::string err;
    t0_ = Clock::now();
    process_ = Process::spawn(
        exe, args,
        [this, alive, post](std::string_view line) {
            std::string copy(line);
            post([this, alive, copy] {
                if (auto a = alive.lock(); a && *a) onLine(copy);
            });
        },
        [this, alive, post](int code) {
            post([this, alive, code] {
                if (auto a = alive.lock(); a && *a && running()) abort("engine exited with code " + std::to_string(code));
            });
        },
        &err);
    if (!process_) {
        result_.error = "Cannot start engine: " + err;
        state_ = State::Finished;
        if (done_) done_(result_);
        return;
    }
    util::Log::info("probe", "started " + exe.string() + " pid " + std::to_string(process_->pid()));
    state_ = State::WaitUciOk;
    process_->writeLine("uci");
}

void UciProbe::abort(const std::string& reason) {
    if (!running()) return;
    result_.ok = false;
    result_.error = reason;
    finish();
}

void UciProbe::onLine(const std::string& line) {
    if (progress_ && line.rfind("info", 0) != 0) progress_(line);
    const UciMessage m = parseUciLine(line);
    switch (state_) {
    case State::WaitUciOk:
        if (m.type == UciMessage::Type::IdName) result_.engineName = m.text;
        else if (m.type == UciMessage::Type::Option) ++result_.optionCount;
        else if (m.type == UciMessage::Type::UciOk) {
            result_.spawnToUciOkMs = msSince(t0_);
            process_->writeLine("setoption name Threads value " + std::to_string(threads_));
            process_->writeLine("setoption name Hash value " + std::to_string(hashMb_));
            tReady_ = Clock::now();
            process_->writeLine("isready");
            state_ = State::WaitReadyOk;
        }
        break;
    case State::WaitReadyOk:
        if (m.type == UciMessage::Type::ReadyOk) {
            result_.readyOkMs = msSince(tReady_);
            result_.rssAfterLoadKb = process_->stats().rssKb;
            process_->writeLine("ucinewgame");
            process_->writeLine("position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5");
            tGo_ = Clock::now();
            process_->writeLine("go movetime " + std::to_string(movetimeMs_));
            state_ = State::Searching;
        }
        break;
    case State::Searching:
        if (m.type == UciMessage::Type::Info) {
            if (m.info.nodes) result_.nodes = *m.info.nodes;
            if (m.info.nps) result_.nps = *m.info.nps;
            if (m.info.depth) result_.depth = *m.info.depth;
        } else if (m.type == UciMessage::Type::BestMove) {
            result_.searchWallMs = msSince(tGo_);
            result_.bestMove = m.text;
            result_.ok = true;
            finish();
        }
        break;
    default: break;
    }
}

void UciProbe::finish() {
    state_ = State::Finished;
    if (process_) {
        const ProcessStats s = process_->stats();
        result_.cpuSeconds = s.cpuSeconds;
        result_.peakRssKb = s.peakRssKb;
        process_->writeLine("quit");
        process_->terminate(std::chrono::milliseconds(1000));
        process_.reset();
    }
    util::Log::info("probe", result_.ok ? "engine probe ok: " + result_.engineName + " bestmove " + result_.bestMove
                                        : "engine probe failed: " + result_.error);
    if (done_) done_(result_);
}

} // namespace pchess::engine
