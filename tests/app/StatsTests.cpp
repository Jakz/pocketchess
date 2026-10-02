// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "app/StatsScreen.h"
#include "app/TacticsScreen.h"
#include "core/storage/ChessDb.h"
#include "core/util/Clock.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;

namespace {

const std::string* find(const StatsScreen::Page& p, const std::string& label) {
    for (const auto& [l, v] : p.rows)
        if (l == label) return &v;
    return nullptr;
}

} // namespace

TEST_CASE("stats: one page per mode, read from chess.db and progress.db") {
    const auto root = std::filesystem::temp_directory_path() / "pchess-stats";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    util::FakeClock clock{1'800'000'000};
    AppConfig cfg;
    cfg.root = root;
    cfg.fontDir = test::fontDir();
    cfg.logToFile = false;
    cfg.persistSettings = true;
    cfg.clock = &clock;
    auto app = std::make_unique<Application>(host, presenter, cfg);
    app->start();

    {
        app->push(std::make_unique<StatsScreen>(*app));
        app->flush();
        auto* s = dynamic_cast<StatsScreen*>(app->top());
        REQUIRE(s != nullptr);
        REQUIRE_EQ(s->pages().size(), size_t(6));
        for (const auto& p : s->pages()) CHECK(p.rows.empty());  // nothing recorded yet: the hints show
        app->pop();
        app->flush();
    }

    // Some history: 30 rated puzzles, a mistake, games, a guess run and a drill.
    puzzles::Progress* pr = app->progress();
    REQUIRE(pr != nullptr);
    for (int i = 0; i < 30; ++i) {
        puzzles::Puzzle p;
        p.id = "p" + std::to_string(i);
        p.rating = 1400 + 20 * i;
        REQUIRE(pr->record("sample", p, i % 3 != 0, true, 1'800'000'000 + i).ok());
    }
    puzzles::Puzzle m;
    m.id = "m1";
    REQUIRE(pr->record(TacticsScreen::kMistakesPack, m, true, false, 1'800'000'100).ok());
    storage::Database& db = *app->userDb();
    for (const char* r : {"1-0", "1-0", "0-1", "1/2-1/2"}) {
        storage::PlayedGame g;
        g.date = 1'800'000'000;
        g.pgn = "1. e4 *";
        g.result = r;
        g.human = "white";
        g.strength = std::string(r) == "0-1" ? "strong" : "casual";
        REQUIRE(storage::addPlayedGame(db, g).ok());
    }
    storage::GuessRun run;
    run.pack = "classics";
    run.gameId = 1;
    run.side = "white";
    run.title = "Anderssen – Kieseritzky, 1851";
    run.first = 6;
    run.second = 2;
    run.missed = 2;
    run.finished = true;
    REQUIRE(storage::saveGuessRun(db, run).ok());
    storage::DrillRun drill;
    drill.pack = "historical";
    drill.side = "black";
    drill.opening = "B90 Sicilian Defense: Najdorf Variation";
    drill.main = 3;
    drill.book = 1;
    drill.missed = 1;
    REQUIRE(storage::saveDrillRun(db, drill).ok());
    storage::StoredMistake mk;
    mk.fen = "x";
    mk.moves = "e2e4";
    mk.played = "d2d4";
    REQUIRE(storage::saveMistakeScan(db, 1, {mk}, "fake", 1).ok());

    app->push(std::make_unique<StatsScreen>(*app));
    app->flush();
    auto* s = dynamic_cast<StatsScreen*>(app->top());
    REQUIRE(s != nullptr);
    const auto& pages = s->pages();
    CHECK_EQ(*find(pages[0], "Puzzles tried"), std::string("30"));  // the mistake is not counted here
    CHECK_EQ(pages[0].chart.size(), size_t(30));
    CHECK_EQ(*find(pages[1], "Games"), std::string("4"));
    CHECK_EQ(*find(pages[1], "Won · drawn · lost"), std::string("2 · 1 · 1"));
    CHECK(find(pages[1], "Casual") != nullptr);
    CHECK_EQ(*find(pages[2], "Score"), std::string("70%"));
    CHECK(find(pages[2], "Anderssen – Kieseritzky, 1851") != nullptr);
    CHECK_EQ(*find(pages[3], "Main move"), std::string("3 (60%)"));
    CHECK_EQ(*find(pages[4], "Mistakes found"), std::string("1"));
    CHECK_EQ(*find(pages[4], "Tried"), std::string("1 of 1"));
    test::checkGolden("stats", app->canvas());

    REQUIRE(runScript(*app, "key:next;key:next").ok());
    CHECK_EQ(s->page(), 2);
    REQUIRE(runScript(*app, "key:menu;key:down;key:down;key:down;key:down;key:ok").ok());  // "My mistakes"
    CHECK_EQ(s->page(), 4);
    app.reset();
    std::filesystem::remove_all(root, ec);
}
