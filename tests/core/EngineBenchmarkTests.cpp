// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/engine/EngineBenchmark.h"
#include "core/util/Yaml.h"
#include "support/FakeEngine.h"
#include "support/Test.h"

#include <optional>

using namespace pchess;
using namespace pchess::engine;

namespace {

struct BenchRig {
    test::ManualScheduler sched;
    test::FakeEngineFactory fake;
    std::vector<EngineDescriptor> engines;
    std::optional<BenchReport> report;
    int doneCalls = 0;
    int progressCalls = 0;
    std::unique_ptr<EngineBenchmark> bench;

    BenchRig() {
        EngineDescriptor big = test::fakeDescriptor("big", EnergyClass::High);
        StrengthLevel beginner{"beginner", {{"Skill Level", "0"}}, {}};
        beginner.limits.movetimeMs = 80;
        big.strength = {beginner, StrengthLevel{"maximum", {}, {}}};
        EngineDescriptor light = test::fakeDescriptor("light", EnergyClass::Low);
        light.strength.clear();  // analysis row only
        engines = {big, light};
    }
    void make(EngineBenchmark::EngineFactory factory = {}) {
        if (!factory)
            factory = [this](const EngineDescriptor& d, util::Scheduler& s) {
                return std::make_unique<UciEngine>(d, s, test::selfExe(), fake.factory());
            };
        bench = std::make_unique<EngineBenchmark>(sched, engines, PowerProfileId::Balanced, factory);
    }
    void start() {
        bench->start(1'790'000'000, "test-x64", [this](const std::string&, int, int) { ++progressCalls; },
                     [this](const BenchReport& r) {
                         ++doneCalls;
                         report = r;
                     });
    }
    void pump() {
        for (int i = 0; i < 10000 && !report; ++i)
            if (sched.runPending() == 0) break;
    }
};

} // namespace

TEST_CASE("engine benchmark: the fixed positions are legal FENs") {
    CHECK(benchmarkPositions().size() >= 6);
    for (const BenchPosition& p : benchmarkPositions()) {
        const auto pos = chess::parseFen(p.fen);
        if (!pos) MESSAGE(std::string(p.name) + ": " + pos.error().context);
        CHECK(pos.ok());
    }
}

TEST_CASE("engine benchmark: every engine x strength level x position, then Quick analysis") {
    BenchRig rig;
    rig.make();
    const int positions = static_cast<int>(benchmarkPositions().size());
    CHECK_EQ(rig.bench->totalSearches(), (3 + 1) * positions);  // big: 2 levels + analysis; light: analysis
    rig.start();
    CHECK(rig.bench->running());
    rig.pump();
    REQUIRE(rig.report.has_value());
    CHECK_EQ(rig.doneCalls, 1);
    CHECK_FALSE(rig.bench->running());
    CHECK_EQ(rig.progressCalls, rig.bench->totalSearches());

    const BenchReport& r = *rig.report;
    CHECK_FALSE(r.cancelled);
    CHECK_EQ(r.profile, std::string("Balanced"));
    CHECK_EQ(r.positions, positions);
    REQUIRE_EQ(r.engines.size(), size_t(2));
    const BenchEngine& big = r.engines[0];
    CHECK_EQ(big.id, std::string("big"));
    CHECK_EQ(big.name, std::string("FakeFish 1.0"));  // the engine's own id name
    CHECK(big.error.empty());
    REQUIRE_EQ(big.rows.size(), size_t(3));
    CHECK_EQ(big.rows[0].strength, std::string("beginner"));
    CHECK_EQ(big.rows[1].strength, std::string("maximum"));
    CHECK_EQ(big.rows[2].strength, std::string("analysis"));
    for (const BenchRow& row : big.rows) {
        CHECK_EQ(row.searches, positions);
        CHECK_EQ(row.failures, 0);
        CHECK_EQ(row.nodes, uint64_t(4242) * positions);
        CHECK(row.avgDepth() > 2.9);
    }
    REQUIRE_EQ(r.engines[1].rows.size(), size_t(1));
    // Each engine: one process, ucinewgame before every position, quit at the end.
    CHECK_EQ(rig.fake.spawned, 2);
    CHECK_EQ(rig.fake.count("ucinewgame"), rig.bench->totalSearches());
    CHECK_EQ(rig.fake.count("go movetime 80"), positions);         // beginner's own limit
    CHECK_EQ(rig.fake.count("go movetime 500"), positions);        // maximum: Balanced play budget
    CHECK_EQ(rig.fake.count("go movetime 1500"), 2 * positions);   // Quick analysis, both engines
    CHECK_EQ(rig.fake.count("quit"), 2);
    CHECK(rig.fake.lastNice == powerProfile(PowerProfileId::Balanced).nice);
}

TEST_CASE("engine benchmark: YAML report round-trips through the YAML reader") {
    BenchRig rig;
    rig.make();
    rig.start();
    rig.pump();
    REQUIRE(rig.report.has_value());
    const std::string text = toYaml(*rig.report);
    CHECK(text.find("date: 2026-09-21T") != std::string::npos);  // 1'790'000'000 s
    auto doc = util::parseYaml(text);
    if (!doc) MESSAGE(doc.error().context);
    REQUIRE(doc.ok());
    CHECK_EQ((*doc)["platform"].stringOr(""), std::string("test-x64"));
    CHECK_EQ((*doc)["engines"].size(), size_t(2));
    const util::YamlNode& row = (*doc)["engines"][0]["rows"][0];
    CHECK_EQ(row["strength"].stringOr(""), std::string("beginner"));
    CHECK_EQ(row["searches"].intOr(0), int64_t(benchmarkPositions().size()));
    CHECK_FALSE((*doc)["cancelled"].boolOr(true));
}

TEST_CASE("engine benchmark: an engine that cannot start is reported, the others still run") {
    BenchRig rig;
    rig.engines[0].binaries[currentPlatformKey()] = "Z:/no/such/engine.exe";
    rig.make([&rig](const EngineDescriptor& d, util::Scheduler& s) -> std::unique_ptr<IEngine> {
        if (d.id == "big") return std::make_unique<UciEngine>(d, s);  // real spawn of a missing file
        return std::make_unique<UciEngine>(d, s, test::selfExe(), rig.fake.factory());
    });
    rig.start();
    rig.pump();
    REQUIRE(rig.report.has_value());
    REQUIRE_EQ(rig.report->engines.size(), size_t(2));
    const BenchEngine& big = rig.report->engines[0];
    CHECK_FALSE(big.error.empty());
    CHECK(big.rows[0].failures > 0);
    CHECK_EQ(big.rows[1].searches, 0);  // skipped after the failed start
    CHECK_EQ(rig.report->engines[1].rows[0].failures, 0);
    CHECK_EQ(rig.report->engines[1].rows[0].searches, static_cast<int>(benchmarkPositions().size()));
    CHECK(toYaml(*rig.report).find("error: ") != std::string::npos);
}

TEST_CASE("engine benchmark: cancel ends the run once and quits the engine") {
    BenchRig rig;
    rig.fake.behaviour.bestmoveOnMovetime = false;  // searches never end on their own
    rig.make();
    rig.start();
    for (int i = 0; i < 20; ++i) rig.sched.runPending();
    CHECK(rig.bench->running());
    CHECK_FALSE(rig.report.has_value());
    rig.bench->cancel();
    rig.pump();
    REQUIRE(rig.report.has_value());
    CHECK(rig.report->cancelled);
    CHECK_EQ(rig.doneCalls, 1);
    CHECK_FALSE(rig.bench->running());
    rig.bench->cancel();  // no effect once finished
    for (int i = 0; i < 5; ++i) rig.sched.runPending();
    CHECK_EQ(rig.doneCalls, 1);
    CHECK(rig.fake.current == nullptr || !rig.fake.current->running());
}
