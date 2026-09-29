// SPDX-License-Identifier: GPL-3.0-or-later
// [integration] tests against real engine binaries (architecture doc §8.5). Run with
// `PocketChessTests --integration`. Engines are taken from %PCHESS_TEST_ENGINE% (one binary)
// or from out/engines/<id>/<platform>/ for every data/engines/<id>/engine.yaml; engines that are
// not built are skipped with a message.
#include "core/chess/San.h"
#include "core/engine/EngineManager.h"
#include "core/engine/UciEngine.h"
#include "support/FakeEngine.h"
#include "support/Test.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <thread>

using namespace pchess;
using namespace pchess::engine;
using namespace std::chrono_literals;

namespace {

struct RealEngine {
    EngineDescriptor descriptor;
    std::filesystem::path exe;
};

std::vector<RealEngine> realEngines() {
    std::vector<RealEngine> out;
    // data/fonts -> repository root (desktop); on the device the bundle has engines/ at the root.
    const std::filesystem::path fonts(test::fontDir());
    const std::filesystem::path repo = fonts.parent_path().parent_path();
    std::error_code ec;
    for (const auto& dir : {repo / "data" / "engines", repo / "engines"}) {
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
            auto d = loadDescriptor(de.path() / "engine.yaml");
            if (!d) continue;
            std::filesystem::path exe = d->binaryPath(currentPlatformKey());
            const std::filesystem::path built = repo / "out" / "engines" / d->id / currentPlatformKey() /
                                                d->binaries[currentPlatformKey()];
            if (!std::filesystem::exists(exe, ec) && std::filesystem::exists(built, ec)) exe = built;
            if (std::filesystem::exists(exe, ec)) out.push_back({*d, exe});
        }
        if (!out.empty()) break;
    }
    if (const char* env = std::getenv("PCHESS_TEST_ENGINE"); env && *env) {
        EngineDescriptor d;
        d.id = "env-engine";
        d.name = env;
        d.defaults = {{"Threads", "1"}, {"Hash", "16"}};
        d.roles = {Role::Play, Role::Analysis, Role::Hint, Role::Validate};
        d.binaries[currentPlatformKey()] = env;
        StrengthLevel beginner;
        beginner.label = "beginner";
        beginner.options = {{"Skill Level", "0"}};
        beginner.limits.movetimeMs = 80;
        d.strength.push_back(beginner);
        out.insert(out.begin(), {d, env});
    }
    return out;
}

// Real process: pump the manual scheduler (posts from the reader thread) and move its clock with
// real time, so startup times are measured and the engine's timeouts can fire.
template <class Pred>
bool pumpUntil(test::ManualScheduler& s, Pred done, std::chrono::milliseconds timeout) {
    const auto begin = std::chrono::steady_clock::now();
    auto last = begin;
    while (!done()) {
        const auto t = std::chrono::steady_clock::now();
        if (t - begin > timeout) return false;
        if (const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t - last); ms.count() > 0) {
            last += ms;
            s.advance(ms);
        }
        if (s.runPending() == 0) std::this_thread::sleep_for(2ms);
    }
    return true;
}

} // namespace

TEST_CASE("[integration] real engines: start, options, search, stop, strength") {
    const auto engines = realEngines();
    if (engines.empty()) {
        MESSAGE("no engine binary built for " + std::string(currentPlatformKey()) +
                " (projects/pocketbook/build-engine.ps1, or set PCHESS_TEST_ENGINE)");
        return;
    }
    for (const RealEngine& re : engines) {
        MESSAGE("engine " + re.descriptor.id + ": " + re.exe.string());
        test::ManualScheduler sched;
        UciEngine e(re.descriptor, sched, re.exe);

        // Bounded search from the start position after 1.e4.
        std::optional<SearchResult> r;
        int infos = 0;
        const chess::Position start = chess::Position::startPosition();
        const chess::Move e4 = chess::parseUci(start, "e2e4").value();
        e.search(start, std::vector<chess::Move>{e4}, [] {
            SearchLimits l;
            l.movetimeMs = 300;
            return l;
        }(), [&](const SearchInfo&) { ++infos; }, [&](const SearchResult& x) { r = x; });
        REQUIRE(pumpUntil(sched, [&] { return r.has_value(); }, 30s));
        if (!r->ok) MESSAGE(r->error);
        CHECK(r->ok);
        CHECK_FALSE(r->best.isNone());
        CHECK(infos > 0);
        REQUIRE_FALSE(r->lines.empty());
        CHECK(r->lines[0].depth > 0);
        CHECK_FALSE(r->lines[0].pv.empty());
        const EngineStats st = e.stats();
        chess::Position afterE4 = start;
        afterE4.makeMove(e4);
        MESSAGE("  " + e.capabilities().name + ": ready in " + std::to_string(st.startup.count()) + " ms, depth " +
                std::to_string(r->lines[0].depth) + ", " + std::to_string(r->lines[0].nodes) + " nodes, best " +
                chess::toSan(afterE4, r->best) + ", RSS " +
                std::to_string(st.rssKb / 1024) + " MiB");
        CHECK(e.capabilities().has("Hash"));

        // The descriptor's options are accepted as sent (Threads/Hash, strength levels).
        for (const auto& level : re.descriptor.strength) {
            std::vector<std::string> warnings;
            validateOptions(level.options, e.capabilities(), &warnings);
            for (const auto& w : warnings) MESSAGE("  strength " + level.label + ": " + w);
            CHECK(warnings.empty());
        }

        // Infinite search + stop returns a move promptly.
        r.reset();
        SearchLimits inf;
        inf.infinite = true;
        e.search(start, {}, inf, nullptr, [&](const SearchResult& x) { r = x; });
        std::this_thread::sleep_for(200ms);
        sched.runPending();
        e.stop();
        REQUIRE(pumpUntil(sched, [&] { return r.has_value(); }, 5s));
        CHECK(r->stopped);
        CHECK_FALSE(r->best.isNone());

        e.shutdown();
        CHECK_FALSE(e.processRunning());
        CHECK(e.stats().cpuSeconds > 0);
    }
}

TEST_CASE("[integration] real engines: EngineManager analysis and play jobs") {
    const auto engines = realEngines();
    if (engines.empty()) return;
    const RealEngine& re = engines.front();
    test::ManualScheduler sched;
    EngineRegistry reg;
    EngineDescriptor d = re.descriptor;
    d.binaries[currentPlatformKey()] = re.exe.string();
    reg.add(d);
    EngineManager mgr(sched, reg);
    std::vector<JobResult> results;
    mgr.analyze(chess::Position::startPosition(), {}, AnalysisMode::Quick, 2, nullptr,
                [&](const JobResult& r) { results.push_back(r); });
    REQUIRE(pumpUntil(sched, [&] { return !results.empty(); }, 30s));
    CHECK(results[0].ok);
    CHECK_EQ(results[0].lines.size(), size_t(2));
    CHECK(results[0].stats.cpuSeconds > 0);
    mgr.bestMove(chess::Position::startPosition(), {}, "beginner", [&](const JobResult& r) { results.push_back(r); });
    REQUIRE(pumpUntil(sched, [&] { return results.size() == 2; }, 30s));
    CHECK(results[1].ok);
    CHECK_FALSE(results[1].best.isNone());
    CHECK(results[1].stats.wallMs < 5000);
    mgr.onLifecycle(EngineManager::Lifecycle::Exit);
    CHECK_FALSE(mgr.processRunning());
}
