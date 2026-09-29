// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/chess/San.h"
#include "core/engine/UciEngine.h"
#include "support/FakeEngine.h"
#include "support/Test.h"

#include <filesystem>
#include <optional>
#include <thread>

using namespace pchess;
using namespace pchess::engine;
using namespace std::chrono_literals;

namespace {

struct Rig {
    test::ManualScheduler sched;
    test::FakeEngineFactory fake;
    std::unique_ptr<UciEngine> engine;
    std::vector<SearchInfo> infos;
    std::vector<SearchResult> results;

    explicit Rig(UciEngine::Timeouts t = {}) {
        engine = std::make_unique<UciEngine>(test::fakeDescriptor(), sched, test::selfExe(), fake.factory(), t);
    }
    void search(const chess::Position& p, SearchLimits l, std::vector<chess::Move> moves = {}) {
        engine->search(
            p, moves, l, [this](const SearchInfo& i) { infos.push_back(i); },
            [this](const SearchResult& r) { results.push_back(r); });
    }
    void run() { sched.runPending(); }
};

SearchLimits movetime(int ms) {
    SearchLimits l;
    l.movetimeMs = ms;
    return l;
}

SearchLimits infinite() {
    SearchLimits l;
    l.infinite = true;
    return l;
}

} // namespace

TEST_CASE("uci engine: lazy start, options, search with info and bestmove") {
    Rig rig;
    CHECK(rig.engine->state() == EngineState::Off);
    rig.engine->configure({{"Hash", "4096"}, {"Skill Level", "5"}, {"MultiPV", "2"}});
    rig.search(chess::Position::startPosition(), movetime(100));
    CHECK(rig.engine->state() == EngineState::Starting);
    CHECK_EQ(rig.fake.spawned, 1);
    CHECK_EQ(rig.sched.backgroundRefs, 1);
    rig.run();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    const SearchResult& r = rig.results[0];
    CHECK(r.ok);
    CHECK_FALSE(r.stopped);
    CHECK_EQ(chess::toUci(r.best), std::string("a2a3"));  // first legal move in UCI order
    CHECK_EQ(chess::toUci(r.ponder), std::string("a7a5"));
    REQUIRE_EQ(r.lines.size(), size_t(1));
    CHECK_EQ(r.lines[0].depth, 3);
    CHECK_EQ(r.lines[0].pv.size(), size_t(2));
    CHECK_EQ(r.lines[0].score->value, 20);
    CHECK_EQ(rig.infos.size(), size_t(2));
    CHECK(rig.engine->state() == EngineState::Idle);
    CHECK_EQ(rig.engine->capabilities().name, std::string("FakeFish 1.0"));
    // Threads (not offered) and MultiPV (not offered) are dropped; Hash is clamped.
    CHECK_EQ(rig.fake.count("setoption name Threads"), 0);
    CHECK_EQ(rig.fake.count("setoption name MultiPV"), 0);
    CHECK_EQ(rig.fake.count("setoption name Hash value 1024"), 1);
    CHECK_EQ(rig.fake.count("setoption name Skill Level value 5"), 1);
    CHECK_EQ(rig.fake.count("ucinewgame"), 1);
    CHECK_EQ(rig.fake.count("position startpos"), 1);
    CHECK_EQ(rig.fake.count("go movetime 100"), 1);
    CHECK_EQ(rig.engine->stats().starts, 1);
    CHECK(rig.engine->stats().cpuSeconds > 0.2);

    // Second search from a FEN with moves: no new process, no repeated options.
    auto p = chess::parseFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    REQUIRE(p.ok());
    const chess::Move e4 = chess::parseUci(*p, "e2e4").value();
    rig.search(*p, movetime(50), {e4});
    rig.run();
    REQUIRE_EQ(rig.results.size(), size_t(2));
    CHECK(rig.results[1].ok);
    CHECK_EQ(rig.fake.spawned, 1);
    CHECK_EQ(rig.fake.count("setoption"), 2);
    CHECK_EQ(rig.fake.count("position fen 4k3/8/8/8/8/8/4P3/4K3 w - - 0 1 moves e2e4"), 1);
    CHECK_EQ(chess::toUci(rig.results[1].best), std::string("e8d7"));

    // A changed option is sent once, confirmed with isready.
    rig.engine->configure({{"Skill Level", "7"}});
    const int readies = rig.fake.count("isready");
    rig.search(chess::Position::startPosition(), movetime(50));
    rig.run();
    CHECK_EQ(rig.fake.count("setoption name Skill Level value 7"), 1);
    CHECK_EQ(rig.fake.count("isready"), readies + 1);
    CHECK_EQ(rig.results.size(), size_t(3));

    rig.engine->shutdown();
    CHECK(rig.engine->state() == EngineState::Off);
    CHECK_FALSE(rig.engine->processRunning());
    CHECK_EQ(rig.fake.count("quit"), 1);
    CHECK_EQ(rig.sched.backgroundRefs, 0);
    // The next search restarts the process.
    rig.search(chess::Position::startPosition(), movetime(50));
    rig.run();
    CHECK_EQ(rig.fake.spawned, 2);
    CHECK_EQ(rig.results.size(), size_t(4));
    CHECK(rig.results[3].ok);
}

TEST_CASE("uci engine: infinite search, stop, superseding searches, multipv") {
    Rig rig;
    rig.fake.behaviour.extraOptions = {"option name MultiPV type spin default 1 min 1 max 500"};
    rig.engine->configure({{"MultiPV", "3"}});
    rig.search(chess::Position::startPosition(), infinite());
    rig.run();
    CHECK(rig.engine->state() == EngineState::Searching);
    CHECK(rig.results.empty());
    CHECK_EQ(rig.fake.count("go infinite"), 1);
    CHECK_EQ(rig.infos.size(), size_t(4));  // depth 1 + three multipv lines

    // A new search stops the running one first.
    rig.search(chess::Position::startPosition(), infinite(),
               {chess::parseUci(chess::Position::startPosition(), "e2e4").value()});
    CHECK(rig.engine->state() == EngineState::Stopping);
    rig.run();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    CHECK(rig.results[0].stopped);
    CHECK(rig.results[0].ok);
    CHECK_EQ(rig.results[0].lines.size(), size_t(3));
    CHECK_EQ(rig.results[0].lines[2].multipv, 3);
    CHECK(rig.engine->state() == EngineState::Searching);
    CHECK_EQ(rig.fake.count("position startpos moves e2e4"), 1);

    rig.engine->stop();
    rig.run();
    REQUIRE_EQ(rig.results.size(), size_t(2));
    CHECK(rig.results[1].stopped);
    CHECK_EQ(chess::toUci(rig.results[1].best), std::string("a7a5"));
    CHECK(rig.engine->state() == EngineState::Idle);

    // A request queued behind a start is superseded by the next one (done called once each).
    rig.engine->shutdown();
    rig.search(chess::Position::startPosition(), movetime(10));
    rig.search(chess::Position::startPosition(), movetime(20));
    CHECK_EQ(rig.results.size(), size_t(3));
    CHECK(rig.results[2].stopped);
    CHECK_FALSE(rig.results[2].ok);
    rig.run();
    REQUIRE_EQ(rig.results.size(), size_t(4));
    CHECK(rig.results[3].ok);
    CHECK_EQ(rig.fake.count("go movetime 10"), 0);
    CHECK_EQ(rig.fake.count("go movetime 20"), 1);
}

TEST_CASE("uci engine: mate and stalemate give bestmove (none); pv stops at an illegal move") {
    Rig rig;
    auto mated = chess::parseFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    REQUIRE(mated.ok());
    rig.search(*mated, movetime(10));
    rig.run();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    CHECK(rig.results[0].ok);
    CHECK(rig.results[0].best.isNone());

    // A PV is converted up to its first illegal move (e4e5 is blocked by ...e5).
    rig.engine->shutdown();  // behaviour changes apply to the next spawned process
    rig.fake.behaviour.extraGoLines = {"info depth 9 multipv 1 score mate -2 nodes 7 pv e2e4 e7e5 e4e5 g8f6"};
    rig.search(chess::Position::startPosition(), movetime(10));
    rig.run();
    REQUIRE_EQ(rig.results.size(), size_t(2));
    REQUIRE_EQ(rig.results[1].lines.size(), size_t(1));
    const SearchInfo& line = rig.results[1].lines[0];
    CHECK_EQ(line.depth, 9);
    CHECK_EQ(line.pv.size(), size_t(2));
    CHECK(line.score->kind == Score::Kind::Mate);
    CHECK_EQ(line.score->value, -2);
}

TEST_CASE("uci engine: start failures retry once, then fail; missing binary") {
    {
        Rig rig;
        rig.fake.readyFailuresLeft = 1;
        rig.search(chess::Position::startPosition(), movetime(10));
        rig.run();
        CHECK(rig.results.empty());
        rig.sched.advance(10'001ms);  // readyok timeout -> kill, restart once
        CHECK_EQ(rig.fake.spawned, 2);
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK(rig.results[0].ok);
        CHECK_EQ(rig.sched.backgroundRefs, 1);
    }
    {
        Rig rig;
        rig.fake.readyFailuresLeft = 5;
        rig.search(chess::Position::startPosition(), movetime(10));
        rig.sched.advance(10'001ms);
        rig.sched.advance(10'001ms);
        CHECK_EQ(rig.fake.spawned, 2);
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK_FALSE(rig.results[0].ok);
        CHECK(rig.results[0].error.find("readyok") != std::string::npos);
        CHECK(rig.engine->state() == EngineState::Failed);
        CHECK_FALSE(rig.engine->lastError().empty());
        CHECK_EQ(rig.sched.backgroundRefs, 0);
        // A later search tries again.
        rig.fake.readyFailuresLeft = 0;
        rig.search(chess::Position::startPosition(), movetime(10));
        rig.run();
        CHECK_EQ(rig.fake.spawned, 3);
        CHECK_EQ(rig.results.size(), size_t(2));
        CHECK(rig.results[1].ok);
        CHECK(rig.engine->lastError().empty());
    }
    {
        Rig rig;
        rig.fake.behaviour.answerUci = false;
        rig.search(chess::Position::startPosition(), movetime(10));
        rig.sched.advance(10'001ms);
        rig.sched.advance(10'001ms);
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK(rig.results[0].error.find("uciok") != std::string::npos);
    }
    {
        test::ManualScheduler sched;
        test::FakeEngineFactory fake;
        UciEngine e(test::fakeDescriptor(), sched, "does/not/exist/engine", fake.factory());
        std::optional<SearchResult> r;
        e.search(chess::Position::startPosition(), {}, movetime(10), nullptr, [&](const SearchResult& x) { r = x; });
        REQUIRE(r.has_value());
        CHECK_FALSE(r->ok);
        CHECK(r->error.find("Engine not found") != std::string::npos);
        CHECK_EQ(fake.spawned, 0);
        CHECK(e.state() == EngineState::Failed);
    }
}

TEST_CASE("uci engine: crash mid-search, stop timeout, movetime watchdog") {
    {
        Rig rig;
        rig.fake.behaviour.crashOnGo = true;
        rig.search(chess::Position::startPosition(), movetime(10));
        rig.run();
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK_FALSE(rig.results[0].ok);
        CHECK(rig.results[0].error.find("exited") != std::string::npos);
        CHECK(rig.engine->state() == EngineState::Off);
        rig.fake.behaviour.crashOnGo = false;
        rig.search(chess::Position::startPosition(), movetime(10));
        rig.run();
        CHECK_EQ(rig.fake.spawned, 2);
        CHECK(rig.results[1].ok);
    }
    {
        Rig rig;
        rig.fake.behaviour.answerStop = false;
        rig.search(chess::Position::startPosition(), infinite());
        rig.run();
        rig.engine->stop();
        rig.sched.advance(2'999ms);
        CHECK(rig.results.empty());
        rig.sched.advance(2ms);
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK_FALSE(rig.results[0].ok);
        CHECK(rig.results[0].error.find("did not stop") != std::string::npos);
        CHECK_FALSE(rig.engine->processRunning());
    }
    {
        Rig rig;
        rig.fake.behaviour.bestmoveOnMovetime = false;
        rig.search(chess::Position::startPosition(), movetime(1000));
        rig.run();
        rig.sched.advance(5'999ms);
        CHECK_EQ(rig.fake.count("stop"), 0);
        rig.sched.advance(2ms);  // movetime + 5 s grace: stop is sent, the fake answers it
        CHECK_EQ(rig.fake.count("stop"), 1);
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK(rig.results[0].ok);
    }
}

TEST_CASE("uci engine: shutdown during a search, destruction inside a callback") {
    Rig rig;
    rig.search(chess::Position::startPosition(), infinite());
    rig.run();
    rig.engine->shutdown();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    CHECK(rig.results[0].stopped);
    CHECK(rig.engine->state() == EngineState::Off);
    CHECK_EQ(rig.sched.backgroundRefs, 0);

    // The owner may destroy the engine from its done callback.
    rig.engine->search(chess::Position::startPosition(), {}, movetime(10), nullptr,
                       [&](const SearchResult&) { rig.engine.reset(); });
    rig.run();
    CHECK(rig.engine == nullptr);
    rig.run();
    CHECK_EQ(rig.sched.backgroundRefs, 0);
}

TEST_CASE("uci engine: real child process (--fake-engine) end to end") {
    // Real threads: a posting scheduler pumped by this test.
    test::ManualScheduler sched;
    UciEngine e(test::fakeDescriptor(), sched);
    std::optional<SearchResult> r;
    int infos = 0;
    e.search(
        chess::Position::startPosition(), {}, movetime(50), [&](const SearchInfo&) { ++infos; },
        [&](const SearchResult& x) { r = x; });
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (!r && std::chrono::steady_clock::now() < end) {
        if (sched.runPending() == 0) std::this_thread::sleep_for(2ms);
    }
    REQUIRE(r.has_value());
    if (!r->ok) MESSAGE(r->error);
    CHECK(r->ok);
    CHECK_EQ(chess::toUci(r->best), std::string("a2a3"));
    CHECK_EQ(infos, 2);
    CHECK(e.processRunning());
    e.shutdown();
    CHECK_FALSE(e.processRunning());
    CHECK(e.stats().starts == 1);
}
