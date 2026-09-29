// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/chess/San.h"
#include "core/engine/EngineManager.h"
#include "support/FakeEngine.h"
#include "support/Test.h"

#include <optional>

using namespace pchess;
using namespace pchess::engine;
using namespace std::chrono_literals;

namespace {

struct ManagerRig {
    test::ManualScheduler sched;
    test::FakeEngineFactory fake;
    EngineRegistry registry;
    std::unique_ptr<EngineManager> mgr;
    std::vector<JobResult> results;
    std::vector<std::pair<JobId, SearchInfo>> infos;

    ManagerRig() {
        fake.behaviour.extraOptions = {"option name MultiPV type spin default 1 min 1 max 500",
                                       "option name Threads type spin default 1 min 1 max 512"};
        registry.add(test::fakeDescriptor("big", EnergyClass::High));
        registry.add(test::fakeDescriptor("light", EnergyClass::Low));
        mgr = std::make_unique<EngineManager>(sched, registry, [this](const EngineDescriptor& d, util::Scheduler& s) {
            return std::make_unique<UciEngine>(d, s, test::selfExe(), fake.factory());
        });
    }
    EngineManager::DoneCallback done() {
        return [this](const JobResult& r) { results.push_back(r); };
    }
    EngineManager::InfoCallback info() {
        return [this](JobId id, const SearchInfo& i) { infos.emplace_back(id, i); };
    }
    void setProfile(PowerProfileId p) {
        EngineSettings s = mgr->settings();
        s.profile = p;
        mgr->setSettings(s);
    }
};

} // namespace

TEST_CASE("power profiles: table values and names") {
    const PowerProfile& bal = powerProfile(PowerProfileId::Balanced);
    CHECK_EQ(std::string(bal.name), std::string("Balanced"));
    CHECK_EQ(bal.threads, 1);
    CHECK_EQ(bal.hashMb, 16);
    CHECK_EQ(bal.deepAnalysis.movetimeMs.value_or(0), 30000);
    CHECK_EQ(bal.deepAnalysis.depth.value_or(0), 22);
    CHECK(bal.idleTimeout == 60s);
    CHECK(powerProfile(PowerProfileId::Performance).deepAnalysis.infinite);
    CHECK_EQ(powerProfile(PowerProfileId::Performance).threads, 2);
    CHECK(powerProfile(PowerProfileId::Eco).idleTimeout == 0ms);
    CHECK_EQ(powerProfile(PowerProfileId::Eco).hint.nodes.value_or(0), uint64_t(50000));
    CHECK(powerProfile(PowerProfileId::Eco).lightEngine);
    CHECK(powerProfileFromString("eco") == PowerProfileId::Eco);
    CHECK_FALSE(powerProfileFromString("turbo").has_value());
}

TEST_CASE("engine manager: analysis job, accounting, idle shutdown") {
    ManagerRig rig;
    CHECK_EQ(rig.mgr->engineFor(Role::Analysis)->id, std::string("big"));
    CHECK_EQ(rig.mgr->engineFor(Role::Play)->id, std::string("big"));
    const JobId id =
        rig.mgr->analyze(chess::Position::startPosition(), {}, AnalysisMode::Quick, 2, rig.info(), rig.done());
    CHECK(rig.mgr->busy());
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    const JobResult& r = rig.results[0];
    CHECK_EQ(r.id, id);
    CHECK(r.ok);
    CHECK_FALSE(r.cancelled);
    CHECK(r.kind == JobKind::Analysis);
    CHECK_EQ(r.engineId, std::string("big"));
    CHECK_EQ(r.lines.size(), size_t(2));
    CHECK_EQ(r.score->value, 20);
    CHECK(r.stats.coldStart);
    CHECK(r.stats.cpuSeconds > 0.2);
    CHECK_EQ(r.stats.nodes, uint64_t(4242));
    CHECK_EQ(r.stats.peakRssKb, uint64_t(95 * 1024));
    CHECK_EQ(rig.infos.size(), size_t(3));
    CHECK_EQ(rig.infos[0].first, id);
    CHECK_EQ(rig.fake.count("go movetime 1500"), 1);  // Balanced Quick
    CHECK_EQ(rig.fake.count("setoption name MultiPV value 2"), 1);
    CHECK_EQ(rig.fake.count("setoption name Threads value 1"), 1);
    CHECK_EQ(rig.fake.count("setoption name Hash value 16"), 1);
    CHECK_FALSE(rig.mgr->busy());
    CHECK(rig.mgr->processRunning());
    CHECK_EQ(rig.mgr->totals().jobs, 1);
    CHECK_EQ(rig.mgr->totals().starts, 1);
    CHECK_EQ(rig.mgr->history().size(), size_t(1));
    CHECK_EQ(rig.fake.lastNice, 10);  // Balanced priority

    // A second job reuses the warm process.
    rig.mgr->hint(chess::Position::startPosition(), {}, rig.done());
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(2));
    CHECK_FALSE(rig.results[1].stats.coldStart);
    CHECK_EQ(rig.fake.spawned, 1);
    CHECK_EQ(rig.fake.count("go movetime 1000"), 1);                // Balanced hint budget
    CHECK_EQ(rig.fake.count("setoption name MultiPV value 1"), 1);  // reset to the default

    rig.sched.advance(59s);
    CHECK(rig.mgr->processRunning());
    rig.sched.advance(2s);
    CHECK_FALSE(rig.mgr->processRunning());
    CHECK_EQ(rig.fake.count("quit"), 1);
    CHECK_EQ(rig.sched.backgroundRefs, 0);
}

TEST_CASE("engine manager: a new job cancels the running one; cancel reports the best move so far") {
    ManagerRig rig;
    rig.setProfile(PowerProfileId::Performance);
    const JobId deep =
        rig.mgr->analyze(chess::Position::startPosition(), {}, AnalysisMode::Deep, 1, rig.info(), rig.done());
    rig.sched.runPending();
    CHECK_EQ(rig.fake.count("go infinite"), 1);
    CHECK(rig.results.empty());
    CHECK_EQ(rig.fake.count("setoption name Threads value 2"), 1);
    CHECK_EQ(rig.fake.count("setoption name Hash value 64"), 1);

    const JobId second =
        rig.mgr->analyze(chess::Position::startPosition(), {}, AnalysisMode::Deep, 1, rig.info(), rig.done());
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    CHECK_EQ(rig.results[0].id, deep);
    CHECK(rig.results[0].cancelled);
    CHECK_EQ(rig.mgr->activeJob(), second);

    const size_t infosBefore = rig.infos.size();
    rig.mgr->cancel(second);
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(2));
    CHECK_EQ(rig.results[1].id, second);
    CHECK(rig.results[1].cancelled);
    CHECK(rig.results[1].ok);
    CHECK_EQ(chess::toUci(rig.results[1].best), std::string("a2a3"));
    CHECK_EQ(rig.infos.size(), infosBefore);
    CHECK_FALSE(rig.mgr->busy());
    // Performance keeps the engine for 5 minutes.
    rig.sched.advance(4min);
    CHECK(rig.mgr->processRunning());
    rig.sched.advance(2min);
    CHECK_FALSE(rig.mgr->processRunning());
}

TEST_CASE("engine manager: play strength options and limits, reset for the next job") {
    ManagerRig rig;
    rig.mgr->bestMove(chess::Position::startPosition(), {}, "beginner", rig.done());
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    CHECK(rig.results[0].ok);
    CHECK(rig.results[0].kind == JobKind::BestMove);
    CHECK_FALSE(rig.results[0].best.isNone());
    CHECK_EQ(rig.fake.count("setoption name Skill Level value 0"), 1);
    CHECK_EQ(rig.fake.count("go movetime 80"), 1);

    rig.mgr->bestMove(chess::Position::startPosition(), {}, "maximum", rig.done());
    rig.sched.runPending();
    CHECK_EQ(rig.fake.count("setoption name Skill Level value 20"), 1);  // back to the default
    CHECK_EQ(rig.fake.count("go movetime 500"), 1);                      // Balanced play budget

    rig.mgr->bestMove(chess::Position::startPosition(), {}, "grandmaster", rig.done());
    rig.sched.runPending();
    CHECK_EQ(rig.results.size(), size_t(3));
    CHECK(rig.results[2].ok);
}

TEST_CASE("engine manager: Eco profile uses the light engine, one process at a time, quits after the job") {
    ManagerRig rig;
    rig.setProfile(PowerProfileId::Eco);
    CHECK_EQ(rig.mgr->engineFor(Role::Play)->id, std::string("light"));
    CHECK_EQ(rig.mgr->engineFor(Role::Hint)->id, std::string("light"));
    CHECK_EQ(rig.mgr->engineFor(Role::Analysis)->id, std::string("big"));  // Deep: started explicitly
    CHECK_EQ(rig.mgr->engineFor(Role::Analysis, AnalysisMode::Quick)->id, std::string("light"));

    // Quick analysis runs on the light engine (no big engine restart per position, ADR 0010).
    rig.mgr->analyze(chess::Position::startPosition(), {}, AnalysisMode::Quick, 1, rig.info(), rig.done());
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    CHECK_EQ(rig.results[0].engineId, std::string("light"));
    CHECK_FALSE(rig.mgr->processRunning());  // idle timeout 0
    rig.results.clear();
    rig.fake.spawned = 0;

    rig.mgr->hint(chess::Position::startPosition(), {}, rig.done());
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(1));
    CHECK_EQ(rig.results[0].engineId, std::string("light"));
    CHECK_EQ(rig.fake.count("go nodes 50000 movetime 300"), 1);
    CHECK_FALSE(rig.mgr->processRunning());  // idle timeout 0
    CHECK_EQ(rig.fake.lastNice, 19);

    // Analysis switches engines while the light one runs: the light process is quit first.
    rig.mgr->bestMove(chess::Position::startPosition(), {}, "casual", rig.done());
    CHECK(rig.mgr->processRunning());
    rig.mgr->analyze(chess::Position::startPosition(), {}, AnalysisMode::Deep, 1, rig.info(), rig.done());
    CHECK_EQ(rig.mgr->liveEngine()->descriptor().id, std::string("big"));
    rig.sched.runPending();
    REQUIRE_EQ(rig.results.size(), size_t(3));
    CHECK(rig.results[1].cancelled);
    CHECK_EQ(rig.results[1].engineId, std::string("light"));
    CHECK(rig.results[2].ok);
    CHECK_EQ(rig.results[2].engineId, std::string("big"));
    CHECK_EQ(rig.fake.count("go depth 16 movetime 10000"), 1);
    CHECK_EQ(rig.fake.spawned, 3);
    CHECK_EQ(rig.sched.backgroundRefs, 0);

    // The user's choice wins over the profile.
    EngineSettings s = rig.mgr->settings();
    s.engineForRole[static_cast<size_t>(Role::Play)] = "big";
    rig.mgr->setSettings(s);
    CHECK_EQ(rig.mgr->engineFor(Role::Play)->id, std::string("big"));
    s.engineForRole[static_cast<size_t>(Role::Play)] = "missing";
    rig.mgr->setSettings(s);
    CHECK_EQ(rig.mgr->engineFor(Role::Play)->id, std::string("light"));
}

TEST_CASE("engine manager: lifecycle stop, missing engine, validate") {
    {
        ManagerRig rig;
        rig.setProfile(PowerProfileId::Performance);
        rig.mgr->analyze(chess::Position::startPosition(), {}, AnalysisMode::Deep, 1, rig.info(), rig.done());
        rig.sched.runPending();
        CHECK(rig.mgr->processRunning());
        rig.mgr->onLifecycle(EngineManager::Lifecycle::Sleep);
        CHECK_FALSE(rig.mgr->processRunning());
        rig.sched.runPending();
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK(rig.results[0].cancelled);
        CHECK_EQ(rig.sched.backgroundRefs, 0);
        CHECK_EQ(rig.sched.timerCount(), size_t(0));
        rig.mgr->onLifecycle(EngineManager::Lifecycle::Wake);
        CHECK_FALSE(rig.mgr->processRunning());  // lazy: no restart until the next job
    }
    {
        test::ManualScheduler sched;
        EngineRegistry empty;
        EngineManager mgr(sched, empty);
        CHECK_FALSE(mgr.available(Role::Analysis));
        std::optional<JobResult> r;
        mgr.hint(chess::Position::startPosition(), {}, [&](const JobResult& x) { r = x; });
        CHECK_FALSE(r.has_value());  // delivered from the event loop, never inside the call
        sched.runPending();
        REQUIRE(r.has_value());
        CHECK_FALSE(r->ok);
        CHECK(r->errorCode == ErrorCode::EngineMissing);
    }
    {
        ManagerRig rig;
        const chess::Position start = chess::Position::startPosition();
        const chess::Move e4 = chess::parseUci(start, "e2e4").value();
        rig.mgr->validate(start, {}, e4, rig.done());
        rig.sched.runPending();
        REQUIRE_EQ(rig.results.size(), size_t(1));
        CHECK(rig.results[0].ok);
        CHECK_EQ(rig.results[0].score->value, -20);  // the fake's +20 for Black, seen from White
        CHECK_EQ(rig.fake.count("position startpos moves e2e4"), 1);
    }
    {
        // Destroying the manager with a job in flight drops its result quietly.
        ManagerRig rig;
        rig.mgr->analyze(chess::Position::startPosition(), {}, AnalysisMode::Deep, 1, rig.info(), rig.done());
        rig.mgr.reset();
        rig.sched.runPending();
        CHECK(rig.results.empty());
    }
}
