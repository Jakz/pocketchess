// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/Process.h"
#include "core/engine/UciParser.h"
#include "core/engine/UciProbe.h"
#include "support/Test.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

using namespace pchess;
using namespace pchess::engine;

TEST_CASE("uci parser: id, options, readyok, bestmove") {
    CHECK(parseUciLine("uciok").type == UciMessage::Type::UciOk);
    CHECK(parseUciLine("  readyok \r").type == UciMessage::Type::ReadyOk);
    auto id = parseUciLine("id name Stockfish 19");
    CHECK(id.type == UciMessage::Type::IdName);
    CHECK_EQ(id.text, std::string("Stockfish 19"));
    auto opt = parseUciLine("option name Skill Level type spin default 20 min 0 max 20");
    CHECK(opt.type == UciMessage::Type::Option);
    CHECK_EQ(opt.option.name, std::string("Skill Level"));
    CHECK_EQ(opt.option.type, std::string("spin"));
    CHECK_EQ(opt.option.defaultValue, std::string("20"));
    CHECK_EQ(opt.option.min.value_or(-1), 0);
    CHECK_EQ(opt.option.max.value_or(-1), 20);
    auto combo = parseUciLine("option name Analysis Contempt type combo default Both var Off var White var Black var Both");
    CHECK_EQ(combo.option.vars.size(), size_t(4));
    auto bm = parseUciLine("bestmove e2e4 ponder e7e5");
    CHECK(bm.type == UciMessage::Type::BestMove);
    CHECK_EQ(bm.text, std::string("e2e4"));
    CHECK_EQ(bm.ponder, std::string("e7e5"));
    CHECK(parseUciLine("Stockfish 19 by the Stockfish developers").type == UciMessage::Type::Unknown);
    CHECK(parseUciLine("").type == UciMessage::Type::Unknown);
}

TEST_CASE("uci parser: info lines with scores, bounds, pv and extra tokens") {
    auto i = parseUciLine("info depth 22 seldepth 30 multipv 2 score cp -35 upperbound nodes 1234567 nps 890000 "
                          "hashfull 12 tbhits 0 time 1500 pv e7e5 g1f3 b8c6");
    REQUIRE(i.type == UciMessage::Type::Info);
    CHECK_EQ(i.info.depth.value_or(0), 22);
    CHECK_EQ(i.info.multipv.value_or(0), 2);
    REQUIRE(i.info.score.has_value());
    CHECK(i.info.score->kind == Score::Kind::Cp);
    CHECK_EQ(i.info.score->value, -35);
    CHECK(i.info.score->upperbound);
    CHECK_EQ(i.info.nodes.value_or(0), uint64_t(1234567));
    CHECK_EQ(i.info.pv.size(), size_t(3));
    CHECK_EQ(i.info.pv[2], std::string("b8c6"));

    auto mate = parseUciLine("info depth 5 score mate -3 wdl 0 0 1000 nodes 10 pv h7h8");
    REQUIRE(mate.info.score.has_value());
    CHECK(mate.info.score->kind == Score::Kind::Mate);
    CHECK_EQ(mate.info.score->value, -3);
    CHECK_EQ(mate.info.pv.size(), size_t(1));

    auto str = parseUciLine("info string NNUE evaluation using nn-abc.nnue enabled");
    CHECK_EQ(str.info.string, std::string("NNUE evaluation using nn-abc.nnue enabled"));
    auto cur = parseUciLine("info depth 10 currmove e2e4 currmovenumber 1");
    CHECK_EQ(cur.info.depth.value_or(0), 10);
    CHECK(cur.info.pv.empty());

    CHECK_EQ(formatScore({Score::Kind::Cp, 62}, true), std::string("+0.62"));
    CHECK_EQ(formatScore({Score::Kind::Cp, 62}, false), std::string("-0.62"));
    CHECK_EQ(formatScore({Score::Kind::Mate, 3}, true), std::string("#3"));
    CHECK_EQ(formatScore({Score::Kind::Mate, 3}, false), std::string("-#3"));
}

namespace {

// Minimal thread-safe task queue standing in for Host::post.
struct Pump {
    std::mutex m;
    std::deque<std::function<void()>> q;
    void post(std::function<void()> f) {
        std::lock_guard lock(m);
        q.push_back(std::move(f));
    }
    bool runUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            std::function<void()> f;
            {
                std::lock_guard lock(m);
                if (!q.empty()) {
                    f = std::move(q.front());
                    q.pop_front();
                }
            }
            if (f) f();
            else std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (std::chrono::steady_clock::now() > end) return false;
        }
        return true;
    }
};

} // namespace

TEST_CASE("process: spawn the fake engine, exchange lines, exit cleanly") {
    REQUIRE_FALSE(test::selfExe().empty());
    std::mutex m;
    std::vector<std::string> lines;
    std::atomic<int> exitCode{-999};
    std::string err;
    auto p = Process::spawn(test::selfExe(), {"--fake-engine"},
                            [&](std::string_view l) {
                                std::lock_guard lock(m);
                                lines.emplace_back(l);
                            },
                            [&](int code) { exitCode = code; }, &err);
    if (!p) MESSAGE(err);
    REQUIRE(p != nullptr);
    CHECK(p->running());
    CHECK(p->pid() > 0);
    p->writeLine("uci");
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        {
            std::lock_guard lock(m);
            if (!lines.empty() && lines.back() == "uciok") break;
        }
        REQUIRE(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(p->stats().valid);
    p->writeLine("quit");
    p->terminate(std::chrono::milliseconds(2000));
    CHECK_FALSE(p->running());
    CHECK_EQ(exitCode.load(), 0);
    std::lock_guard lock(m);
    CHECK_EQ(lines.front(), std::string("id name FakeFish 1.0"));
}

TEST_CASE("process: missing binary is reported, not a crash") {
    std::string err;
    auto p = Process::spawn("does/not/exist/engine", {}, nullptr, nullptr, &err);
    // POSIX forks before exec fails (the child exits 127); Windows fails in CreateProcess.
    if (p) {
        p->terminate(std::chrono::milliseconds(1000));
        CHECK_FALSE(p->running());
    } else {
        CHECK_FALSE(err.empty());
    }
}

TEST_CASE("probe: full uci -> isready -> go -> bestmove cycle against the fake engine") {
    Pump pump;
    std::optional<UciProbe::Result> result;
    UciProbe probe([&](std::function<void()> f) { pump.post(std::move(f)); },
                   [&](const UciProbe::Result& r) { result = r; });
    probe.start(test::selfExe(), 50, 1, 16, {"--fake-engine"});
    REQUIRE(pump.runUntil([&] { return result.has_value(); }, std::chrono::seconds(10)));
    if (!result->ok) MESSAGE(result->error);
    CHECK(result->ok);
    CHECK_EQ(result->engineName, std::string("FakeFish 1.0"));
    CHECK_EQ(result->optionCount, 2);
    // The fake plays the first legal move in UCI order: 1.e4 e5 2.Nf3 Nc6 3.Bb5, Black to move.
    CHECK_EQ(result->bestMove, std::string("a7a5"));
    CHECK_EQ(result->depth, 3);
    CHECK_EQ(result->nodes, uint64_t(4242));

    std::optional<UciProbe::Result> missing;
    UciProbe p2([&](std::function<void()> f) { pump.post(std::move(f)); },
                [&](const UciProbe::Result& r) { missing = r; });
    p2.start("does/not/exist/stockfish");
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->ok);
    CHECK(missing->error.find("Engine not found") != std::string::npos);
}
