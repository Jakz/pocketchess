// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/HomeScreen.h"
#include "app/Script.h"
#include "app/TacticsScreen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/puzzles/PuzzleImport.h"
#include "core/util/Clock.h"
#include "core/util/Files.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <algorithm>
#include <filesystem>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;
using puzzles::PuzzleSession;

namespace {

constexpr int64_t kDay = 86400;

// <root>/content/puzzles/sample.db from tests/fixtures/puzzles-sample.csv (60 Lichess puzzles).
void buildSamplePack(const std::filesystem::path& root) {
    const auto file = root / "content" / "puzzles" / "sample.db";
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::remove(file, ec);
    auto csv = util::readTextFile(std::filesystem::path(test::fixtureDir()) / "puzzles-sample.csv");
    REQUIRE(csv.ok());
    puzzles::ImportFilter f;
    f.count = 1000;
    puzzles::PuzzleImporter imp(f);
    size_t i = 0;
    while (i < csv->size()) {
        size_t j = csv->find('\n', i);
        if (j == std::string::npos) j = csv->size();
        imp.addLine(std::string_view(*csv).substr(i, j - i));
        i = j + 1;
    }
    auto db = storage::Database::open(file, storage::Database::Mode::ReadWrite);
    REQUIRE(db.ok());
    REQUIRE(db->exec("PRAGMA journal_mode=DELETE").ok());
    REQUIRE(imp.write(*db).ok());
}

struct TacticsHarness {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    util::FakeClock clock{1'800'000'000};
    std::unique_ptr<Application> app;

    explicit TacticsHarness(const std::filesystem::path& root) {
        AppConfig cfg;
        cfg.root = root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = false;  // progress in memory for this run
        cfg.clock = &clock;
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
        app->settings().tacticsAutoOpponent = true;
    }
    TacticsScreen* tactics() const { return dynamic_cast<TacticsScreen*>(app->top()); }
    TacticsScreen* open(TacticsScreen::Mode mode) {
        app->push(std::make_unique<TacticsScreen>(*app, mode));
        pump(700ms);  // the opponent's first move
        return tactics();
    }
    void pump(std::chrono::milliseconds ms = 0ms) {
        host.advance(ms);
        app->flush();
    }
    const PuzzleSession& session() const {
        REQUIRE(tactics() != nullptr);
        REQUIRE(tactics()->session() != nullptr);
        return *tactics()->session();
    }
    // Plays the stored line to the end.
    void solve() {
        for (int guard = 0; guard < 20 && !session().finished(); ++guard) {
            REQUIRE(session().state() == PuzzleSession::State::AwaitUser);
            const chess::Move expected = session().puzzle().moves[session().played().size()];
            REQUIRE(tactics()->playUci(chess::toUci(expected)));
            pump(500ms);
        }
    }
    // A legal move that is neither the stored move nor a mate.
    std::string wrongMove() const {
        const chess::Position& pos = session().position();
        const chess::Move expected = session().puzzle().moves[session().played().size()];
        chess::MoveList legal;
        chess::generateLegalMoves(pos, legal);
        for (chess::Move m : legal) {
            if (m == expected) continue;
            chess::Position after = pos;
            after.makeMove(m);
            chess::MoveList replies;
            chess::generateLegalMoves(after, replies);
            if (!(replies.empty() && after.inCheck())) return chess::toUci(m);
        }
        return "";
    }
};

std::filesystem::path packRoot(const char* name) {
    const auto root = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    buildSamplePack(root);
    return root;
}

} // namespace

TEST_CASE("tactics: a puzzle is solved along its line and the rating goes up") {
    const auto root = packRoot("pchess-tactics-solve");
    TacticsHarness h(root);
    REQUIRE(h.open(TacticsScreen::Mode::Train) != nullptr);
    CHECK(h.session().state() == PuzzleSession::State::AwaitUser);
    CHECK(std::abs(h.session().puzzle().rating - 1500) <= 150);  // near the initial rating
    test::checkGolden("tactics", h.app->canvas());

    const std::string first = h.session().puzzle().id;
    h.solve();
    CHECK(h.session().state() == PuzzleSession::State::Solved);
    CHECK(h.tactics()->status().rfind("Solved · +", 0) == 0);
    CHECK(h.app->progress()->rating() > puzzles::kInitialRating);
    CHECK(h.tactics()->info().rfind("Puzzle ", 0) == 0);

    h.tactics()->next();
    h.pump(700ms);
    CHECK(h.session().puzzle().id != first);  // seen puzzles are not repeated
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("tactics: wrong move, retry, solution, then the puzzle is due for review the next day") {
    const auto root = packRoot("pchess-tactics-review");
    TacticsHarness h(root);
    REQUIRE(h.open(TacticsScreen::Mode::Train) != nullptr);
    const std::string id = h.session().puzzle().id;
    const std::string wrong = h.wrongMove();
    REQUIRE_FALSE(wrong.empty());
    REQUIRE(h.tactics()->playUci(wrong));
    CHECK_EQ(h.tactics()->status(), std::string("Wrong move"));
    CHECK(h.session().state() == PuzzleSession::State::Wrong);
    h.tactics()->retry();
    CHECK(h.session().state() == PuzzleSession::State::AwaitUser);
    h.tactics()->showSolution();
    CHECK(h.session().state() == PuzzleSession::State::Failed);
    CHECK(h.tactics()->status().rfind("Failed", 0) == 0);
    for (int i = 0; i < 10; ++i) h.pump(700ms);  // the solution plays out, one move per step
    CHECK_EQ(h.session().played().size(), h.session().puzzle().moves.size());
    CHECK(h.app->progress()->rating() < puzzles::kInitialRating);

    CHECK_EQ(h.app->progress()->dueCount(h.app->now()).value(), 0);
    h.clock.setUnixSeconds(h.clock.unixSeconds() + kDay + 60);
    CHECK_EQ(h.app->progress()->dueCount(h.app->now()).value(), 1);

    h.app->pop();
    REQUIRE(h.open(TacticsScreen::Mode::Review) != nullptr);
    CHECK_EQ(h.session().puzzle().id, id);
    const int rating = h.app->progress()->rating();
    h.solve();
    CHECK(h.tactics()->status().find("again in 3 days") != std::string::npos);
    CHECK_EQ(h.app->progress()->rating(), rating);  // reviews do not move the rating
    h.tactics()->next();
    CHECK_EQ(h.tactics()->status(), std::string("No reviews due"));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("tactics: without a pack the screen explains where packs go; Home shows rating and reviews") {
    const auto root = std::filesystem::temp_directory_path() / "pchess-tactics-nopack";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    TacticsHarness h(root);
    h.app->push(std::make_unique<TacticsScreen>(*h.app, TacticsScreen::Mode::Train));
    REQUIRE(h.tactics() != nullptr);
    CHECK_EQ(h.tactics()->status(), std::string("No puzzle pack installed"));
    CHECK(h.tactics()->session() == nullptr);
    h.app->pop();

    auto* home = dynamic_cast<HomeScreen*>(h.app->top());
    REQUIRE(home != nullptr);
    CHECK(home->summary().empty());  // no puzzles played yet
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("tactics: Settings -> Tactics -> Skip theme keeps that theme out of the selection") {
    const auto root = packRoot("pchess-tactics-skip");
    TacticsHarness h(root);
    REQUIRE(h.open(TacticsScreen::Mode::Train) != nullptr);
    puzzles::PuzzlePack* pack = h.app->puzzlePack();
    REQUIRE(pack != nullptr);
    const auto names = pack->themesOf(h.session().puzzle().themes);
    REQUIRE(!names.empty());
    const std::string skipped = names.front();
    const uint64_t bit = pack->themeMask({skipped});
    h.app->settings().tacticsSkipTheme = skipped;
    for (int i = 0; i < 10; ++i) {
        h.solve();  // Next only leaves a finished puzzle
        h.tactics()->next();
        h.pump(700ms);
        REQUIRE(h.tactics()->session() != nullptr);
        CHECK((h.session().puzzle().themes & bit) == 0);
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
